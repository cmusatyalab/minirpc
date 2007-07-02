/*
 * miniRPC - TCP RPC library with asynchronous operations and TLS support
 *
 * Copyright (C) 2007 Carnegie Mellon University
 *
 * This software is distributed under the terms of the Eclipse Public License,
 * Version 1.0 which can be found in the file named LICENSE.  ANY USE,
 * REPRODUCTION OR DISTRIBUTION OF THIS SOFTWARE CONSTITUTES RECIPIENT'S
 * ACCEPTANCE OF THIS AGREEMENT
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <pthread.h>
#include <apr_portable.h>  /* XXX */
#define MINIRPC_INTERNAL
#include "internal.h"

#define POLLEVENTS (APR_POLLIN|APR_POLLERR|APR_POLLHUP)

static int setsockoptval(int fd, int level, int optname, int value)
{
	return setsockopt(fd, level, optname, &value, sizeof(value));
}

static int set_nonblock(int fd)
{
	int flags;

	flags=fcntl(fd, F_GETFL);
	if (flags == -1)
		return -errno;
	if (fcntl(fd, F_SETFL, flags | O_NONBLOCK))
		return -errno;
	return 0;
}

static apr_status_t update_poll(struct mrpc_conn_set *set, apr_socket_t *sock,
			unsigned reqevents, void *data)
{
	apr_pollfd_t pollfd;

	pollfd.p=set->pool;
	pollfd.desc_type=APR_POLL_SOCKET;
	pollfd.reqevents=reqevents;
	pollfd.desc.s=sock;
	pollfd.client_data=data;
	/* We can't update an existing reqevents set, so we remove the fd
	   (which may fail) and re-add it. */
	apr_pollset_remove(set->pollset, &pollfd);
	return apr_pollset_add(set->pollset, &pollfd);
}

static apr_status_t remove_poll(struct mrpc_conn_set *set, apr_socket_t *sock)
{
	apr_pollfd_t pollfd={0};

	pollfd.desc_type=APR_POLL_SOCKET;
	pollfd.desc.s=sock;
	return apr_pollset_remove(set->pollset, &pollfd);
}

static int mrpc_conn_add(struct mrpc_conn_set *set, int fd,
			struct mrpc_connection **new_conn)
{
	struct mrpc_connection *conn;
	pthread_mutexattr_t attr;
	apr_status_t stat;
	apr_socket_t *sock=NULL;
	apr_pool_t *pool;

	stat=apr_pool_create(&pool, set->pool);
	if (stat)
		return -APR_TO_OS_ERROR(stat);
	stat=apr_os_sock_put(&sock, &fd, pool);
	if (stat) {
		apr_pool_destroy(pool);
		return -APR_TO_OS_ERROR(stat);
	}
	apr_socket_opt_set(sock, APR_SO_KEEPALIVE, 1);
	stat=apr_socket_opt_set(sock, APR_SO_NONBLOCK, 1);
	if (stat) {
		apr_pool_destroy(pool);
		return -APR_TO_OS_ERROR(stat);
	}
	conn=malloc(sizeof(*conn));
	if (conn == NULL) {
		apr_pool_destroy(pool);
		return -ENOMEM;
	}
	memset(conn, 0, sizeof(*conn));
	APR_RING_INIT(&conn->send_msgs, mrpc_message, lh_msgs);
	APR_RING_ELEM_INIT(conn, lh_event_conns);
	APR_RING_INIT(&conn->events, mrpc_event, lh_events);
	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE_NP);
	pthread_mutex_init(&conn->operations_lock, &attr);
	pthread_mutexattr_destroy(&attr);
	pthread_mutex_init(&conn->send_msgs_lock, NULL);
	pthread_mutex_init(&conn->pending_replies_lock, NULL);
	pthread_mutex_init(&conn->sync_wakeup_lock, NULL);
	conn->send_state=STATE_IDLE;
	conn->recv_state=STATE_HEADER;
	conn->set=set;
	conn->sock=sock;
	conn->pool=pool;
	conn->pending_replies=apr_hash_make_custom(conn->pool, numeric_hash_fn);
	stat=update_poll(set, sock, POLLEVENTS, conn);
	if (stat) {
		apr_pool_destroy(conn->pool);
		free(conn);
		return -APR_TO_OS_ERROR(stat);
	}
	*new_conn=conn;
	return 0;
}
/* XXX unregister sock from pollset at pool release */

static void mrpc_conn_remove(struct mrpc_connection *conn)
{
	struct mrpc_conn_set *set=conn->set;

	/* XXX data already in buffer? */
	remove_poll(set, conn->sock);
	apr_pool_destroy(conn->pool);
	free(conn);
}

/* returns error description or NULL on success */
exported const char *mrpc_connect(struct mrpc_conn_set *set, char *host,
			unsigned port, void *data,
			struct mrpc_connection **new_conn)
{
	struct mrpc_connection *conn;
	struct addrinfo *info;
	struct addrinfo *curinfo;
	struct addrinfo hints={0};
	char portbuf[8];
	int fd=-1;
	int ret;

	if (set->config.protocol->is_server)
		return "Servers cannot make outbound connections";

	ret=snprintf(portbuf, sizeof(portbuf), "%u", port);
	if (ret == -1 || ret >= sizeof(portbuf))
		return "Port number too long for buffer";
	hints.ai_socktype=SOCK_STREAM;
	ret=getaddrinfo(host, portbuf, &hints, &info);
	if (ret)
		return gai_strerror(ret);

	for (curinfo=info; curinfo != NULL; curinfo=curinfo->ai_next) {
		fd=socket(info->ai_family, info->ai_socktype,
					info->ai_protocol);
		if (fd == -1) {
			ret=errno;
			continue;
		}
		ret=connect(fd, info->ai_addr, info->ai_addrlen);
		if (ret == 0)
			break;
		ret=errno;
		close(fd);
		fd=-1;
	}
	freeaddrinfo(info);
	if (fd == -1)
		return strerror(ret);
	ret=mrpc_conn_add(set, fd, &conn);
	if (ret)
		return strerror(-ret);
	conn->private = (data != NULL) ? data : conn;
	*new_conn=conn;
	return NULL;
}

/* returns number of successfully bound listening sockets */
exported int mrpc_listen(struct mrpc_conn_set *set, char *listenaddr,
			unsigned port, const char **err)
{
	struct addrinfo *info;
	struct addrinfo *curinfo;
	struct addrinfo hints={0};
	char portbuf[8];
	int fd;
	int ret;
	apr_status_t stat;
	apr_socket_t *sock=NULL;
	int count=0;

	if (!set->config.protocol->is_server) {
		if (err != NULL)
			*err="Clients cannot accept inbound connections";
		return 0;
	}

	ret=snprintf(portbuf, sizeof(portbuf), "%u", port);
	if (ret == -1 || ret >= sizeof(portbuf)) {
		if (err != NULL)
			*err="Port number too long for buffer";
		return 0;
	}
	hints.ai_flags=AI_PASSIVE;
	hints.ai_socktype=SOCK_STREAM;
	ret=getaddrinfo(listenaddr, portbuf, &hints, &info);
	if (ret) {
		if (err != NULL)
			*err=gai_strerror(ret);
		return 0;
	}

	for (curinfo=info; curinfo != NULL; curinfo=curinfo->ai_next) {
		fd=socket(curinfo->ai_family, curinfo->ai_socktype,
					curinfo->ai_protocol);
		if (fd == -1) {
			ret=errno;
			continue;
		}
		setsockoptval(fd, SOL_SOCKET, SO_REUSEADDR, 1);
		ret=set_nonblock(fd);
		if (ret) {
			ret=-ret;
			close(fd);
			continue;
		}
		if (bind(fd, curinfo->ai_addr, curinfo->ai_addrlen)) {
			ret=errno;
			close(fd);
			continue;
		}
		if (listen(fd, set->config.listen_backlog)) {
			ret=errno;
			close(fd);
			continue;
		}
		stat=apr_os_sock_put(&sock, &fd, set->pool);
		if (stat) {
			/* XXX shouldn't indirect through ret */
			ret=-APR_TO_OS_ERROR(stat);
			close(fd);
			continue;
		}
		stat=update_poll(set, sock, APR_POLLIN, NULL);
		if (stat) {
			/* XXX shouldn't indirect through ret */
			ret=-APR_TO_OS_ERROR(stat);
			apr_socket_close(sock);
			continue;
		}
		count++;
	}
	if (count == 0 && err != NULL)
		*err=strerror(ret);
	return count;
}

/* returns -errno */
exported int mrpc_bind_fd(struct mrpc_conn_set *set, int fd, void *data,
			struct mrpc_connection **new_conn)
{
	struct mrpc_connection *conn;
	int accepting;
	socklen_t arglen=sizeof(accepting);
	int ret;

	if (getsockopt(fd, SOL_SOCKET, SO_ACCEPTCONN, &accepting, &arglen))
		return -errno;
	if (accepting)
		return -EINVAL;
	ret=mrpc_conn_add(set, fd, &conn);
	if (ret)
		return ret;
	conn->private = (data != NULL) ? data : conn;
	*new_conn=conn;
	return 0;
}

static void _conn_close(struct mrpc_connection *conn)
{
	/* XXX */
	apr_socket_close(conn->sock);
}

exported void mrpc_conn_close(struct mrpc_connection *conn)
{
	_conn_close(conn);
	mrpc_conn_remove(conn);
}

exported mrpc_status_t mrpc_conn_set_operations(struct mrpc_connection *conn,
			struct mrpc_protocol *protocol, const void *ops)
{
	if (conn->set->config.protocol != protocol)
		return MINIRPC_INVALID_ARGUMENT;
	pthread_mutex_lock(&conn->operations_lock);
	conn->operations=ops;
	pthread_mutex_unlock(&conn->operations_lock);
	return MINIRPC_OK;
}

static void conn_kill(struct mrpc_connection *conn,
			enum mrpc_disc_reason reason)
{
	struct mrpc_event *event;

	mrpc_conn_close(conn);
	event=mrpc_alloc_event(conn, EVENT_DISCONNECT);
	if (event != NULL) {
		event->disc_reason=reason;
		queue_event(event);
	}
}

static mrpc_status_t process_incoming_header(struct mrpc_connection *conn)
{
	mrpc_status_t ret;

	ret=unserialize((xdrproc_t)xdr_mrpc_header, conn->recv_hdr_buf,
				MINIRPC_HEADER_LEN, &conn->recv_msg->hdr,
				sizeof(conn->recv_msg->hdr));
	if (ret)
		return ret;
	if (conn->recv_msg->hdr.datalen > conn->set->config.msg_max_buf_len) {
		/* XXX doesn't get returned to client if request */
  		return MINIRPC_ENCODING_ERR;
	}

	if (conn->recv_msg->hdr.datalen) {
		conn->recv_msg->data=malloc(conn->recv_msg->hdr.datalen);
		if (conn->recv_msg->data == NULL) {
			/* XXX */
			return MINIRPC_NOMEM;
		}
	} else {
		conn->recv_msg->data=NULL;
	}
	return MINIRPC_OK;
}

static void try_read_conn(struct mrpc_connection *conn)
{
	apr_size_t count;
	char *buf;
	unsigned len;
	apr_status_t stat;

	while (1) {
		if (conn->recv_msg == NULL) {
			conn->recv_msg=mrpc_alloc_message(conn);
			if (conn->recv_msg == NULL) {
				/* XXX */
			}
		}

		switch (conn->recv_state) {
		case STATE_HEADER:
			buf=conn->recv_hdr_buf;
			len=MINIRPC_HEADER_LEN;
			break;
		case STATE_DATA:
			buf=conn->recv_msg->data;
			len=conn->recv_msg->hdr.datalen;
			break;
		default:
			assert(0);
		}

		if (conn->recv_offset < len) {
			count = len - conn->recv_offset;
			stat=apr_socket_recv(conn->sock,
						buf + conn->recv_offset,
						&count);
			/* Errors can be returned even if count > 0.  Ignore
			   them and pick them up on the next pass. */
			if (count == 0) {
				if (stat == APR_EOF) {
					conn_kill(conn, MRPC_DISC_CLOSED);
				} else if (stat != APR_EAGAIN) {
					conn_kill(conn, MRPC_DISC_IOERR);
				}
				return;
			}
			printf("Read %d bytes\n", (int)count);
			conn->recv_offset += count;
		}

		if (conn->recv_offset == len) {
			switch (conn->recv_state) {
			case STATE_HEADER:
				if (process_incoming_header(conn)) {
					/* XXX */
					;
				} else {
					conn->recv_state=STATE_DATA;
				}
				conn->recv_offset=0;
				break;
			case STATE_DATA:
				process_incoming_message(conn->recv_msg);
				conn->recv_state=STATE_HEADER;
				conn->recv_offset=0;
				conn->recv_msg=NULL;
				break;
			default:
				assert(0);
			}
		}
	}
}

static mrpc_status_t get_next_message(struct mrpc_connection *conn)
{
	mrpc_status_t ret;

	pthread_mutex_lock(&conn->send_msgs_lock);
	if (APR_RING_EMPTY(&conn->send_msgs, mrpc_message, lh_msgs)) {
		update_poll(conn->set, conn->sock, POLLEVENTS, conn);
		pthread_mutex_unlock(&conn->send_msgs_lock);
		return MINIRPC_OK;
	}
	conn->send_msg=APR_RING_FIRST(&conn->send_msgs);
	APR_RING_REMOVE_INIT(conn->send_msg, lh_msgs);
	pthread_mutex_unlock(&conn->send_msgs_lock);

	ret=serialize_len((xdrproc_t)xdr_mrpc_header, &conn->send_msg->hdr,
				conn->send_hdr_buf, MINIRPC_HEADER_LEN);
	if (ret) {
		/* XXX message dropped on floor */
		mrpc_free_message(conn->send_msg);
		conn->send_msg=NULL;
		return ret;
	}
	return MINIRPC_OK;
}

static void try_write_conn(struct mrpc_connection *conn)
{
	apr_size_t count;
	char *buf;
	unsigned len;
	apr_status_t stat;

	while (1) {
		if (conn->send_msg == NULL) {
			if (get_next_message(conn)) {
				/* Message dropped on floor.  Better luck
				   next time? */
				continue;
			}
			if (conn->send_msg == NULL) {
				if (conn->send_state != STATE_IDLE) {
					apr_socket_opt_set(conn->sock,
							APR_TCP_NOPUSH, 0);
					conn->send_state=STATE_IDLE;
				}
				break;
			}
			if (conn->send_state == STATE_IDLE) {
				apr_socket_opt_set(conn->sock, APR_TCP_NOPUSH,
							1);
				conn->send_state=STATE_HEADER;
			}
		}

		switch (conn->send_state) {
		case STATE_HEADER:
			buf=conn->send_hdr_buf;
			len=MINIRPC_HEADER_LEN;
			break;
		case STATE_DATA:
			buf=conn->send_msg->data;
			len=conn->send_msg->hdr.datalen;
			break;
		default:
			assert(0);
		}

		if (conn->send_offset < len) {
			count = len - conn->send_offset;
			stat=apr_socket_send(conn->sock, buf +
						conn->send_offset, &count);
			/* Errors can be returned even if count > 0.  Ignore
			   them and pick them up on the next pass. */
			if (count == 0) {
				if (stat != APR_EAGAIN)
					conn_kill(conn, MRPC_DISC_IOERR);
				break;
			}
			conn->send_offset += count;
		}

		if (conn->send_offset == len) {
			switch (conn->send_state) {
			case STATE_HEADER:
				conn->send_state=STATE_DATA;
				conn->send_offset=0;
				break;
			case STATE_DATA:
				conn->send_state=STATE_HEADER;
				conn->send_offset=0;
				mrpc_free_message(conn->send_msg);
				conn->send_msg=NULL;
				break;
			default:
				assert(0);
			}
		}
	}
}

static void try_accept(struct mrpc_conn_set *set, int listenfd)
{
	struct mrpc_connection *conn;
	struct mrpc_event *event;
	struct sockaddr *addr;
	socklen_t addrlen;
	int fd;

	while (1) {
		addrlen=sizeof(struct sockaddr_storage);
		addr=malloc(addrlen);
		if (addr == NULL)
			/* XXX */;
		fd=accept(listenfd, addr, &addrlen);
		if (fd < 0) {
			switch (errno) {
			case EAGAIN:
				free(addr);
				return;
			case EMFILE:
			case ENFILE:
			case ENOMEM:
				/* XXX fail */
				break;
			default:
				continue;
			}
		}
		if (mrpc_conn_add(set, fd, &conn)) {
			/* XXX */
			close(fd);
			free(addr);
			continue;
		}
		event=mrpc_alloc_event(conn, EVENT_ACCEPT);
		if (event == NULL)
			/* XXX */;
		event->addr=addr;
		event->addrlen=addrlen;
		queue_event(event);
	}
}

/* XXX signal handling */
static void *listener(void *data)
{
	struct mrpc_conn_set *set=data;
	struct mrpc_connection *conn;
	const apr_pollfd_t *events;
	int count;
	int i;
	int fd;

	while (1) {
		if (apr_pollset_poll(set->pollset, -1, &count, &events))
			/* XXX */;
		for (i=0; i<count; i++) {
			if (events[i].client_data == set) {
				/* Shutdown pipe */
				return NULL;
			}
			if (events[i].client_data == NULL) {
				/* Listening socket */
				if (apr_os_sock_get(&fd, events[i].desc.s))
					/* XXX */;
				try_accept(set, fd);
				continue;
			}
			conn=events[i].client_data;
			if (events[i].rtnevents & APR_POLLERR) {
				conn_kill(conn, MRPC_DISC_IOERR);
				continue;
			}
			if (events[i].rtnevents & APR_POLLHUP) {
				/* XXX select() won't give us this; maybe
				   others too */
				conn_kill(conn, MRPC_DISC_CLOSED);
				continue;
			}
			if (events[i].rtnevents & APR_POLLOUT)
				try_write_conn(conn);
			if (events[i].rtnevents & APR_POLLIN)
				try_read_conn(conn);
		}
	}
}

mrpc_status_t send_message(struct mrpc_message *msg)
{
	struct mrpc_connection *conn=msg->conn;

	pthread_mutex_lock(&conn->send_msgs_lock);
	if (update_poll(conn->set, conn->sock, POLLEVENTS|APR_POLLOUT, conn)) {
		pthread_mutex_unlock(&conn->send_msgs_lock);
		mrpc_free_message(msg);
		return MINIRPC_NETWORK_FAILURE;
	}
	APR_RING_INSERT_TAIL(&conn->send_msgs, msg, mrpc_message, lh_msgs);
	pthread_mutex_unlock(&conn->send_msgs_lock);
	return MINIRPC_OK;
}

#define copy_default(from, to, field, default) do { \
		to->field=from->field ? from->field : default; \
	} while (0)
static void validate_copy_config(const struct mrpc_config *from,
			struct mrpc_config *to)
{
	to->protocol=from->protocol;
	copy_default(from, to, max_fds, 1024);
	copy_default(from, to, msg_max_buf_len, 16000);
	copy_default(from, to, listen_backlog, 16);
}
#undef copy_default

exported int mrpc_conn_set_alloc(const struct mrpc_config *config,
			const struct mrpc_set_operations *ops,
			void *set_data,	struct mrpc_conn_set **new_set)
{
	struct mrpc_conn_set *set;
	apr_pollfd_t pollfd;
	int ret;
	apr_status_t stat;

	if (config == NULL || config->protocol == NULL || ops == NULL ||
				new_set == NULL)
		return -EINVAL;
	if (config->protocol->is_server) {
		/* We require the accept method to exist.  Without it, the
		   connection will never have a non-NULL operations pointer and
		   the application will never be aware that the connection
		   exists, so the connecting client will be forever stuck in
		   PROCEDURE_UNAVAIL limbo. */
		if (ops->accept == NULL)
			return -EINVAL;
	} else {
		/* The accept method is irrelevant for clients.  Tell the
		   application if its assumptions are wrong. */
		if (ops->accept != NULL)
			return -EINVAL;
	}

	ret=mrpc_get();
	if (ret)
		goto bad_init;
	set=malloc(sizeof(*set));
	if (set == NULL) {
		ret=-ENOMEM;
		goto bad_alloc;
	}
	memset(set, 0, sizeof(*set));
	validate_copy_config(config, &set->config);
	pthread_mutex_init(&set->events_lock, NULL);
	pthread_cond_init(&set->events_threads_cond, NULL);
	APR_RING_INIT(&set->event_conns, mrpc_connection, lh_event_conns);
	set->ops=ops;
	set->private = (set_data != NULL) ? set_data : set;
	stat=apr_pool_create(&set->pool, mrpc_pool);
	if (stat) {
		ret=-APR_TO_OS_ERROR(stat);
		goto bad_pool;
	}
	stat=apr_file_pipe_create(&set->shutdown_pipe_read,
				&set->shutdown_pipe_write, set->pool);
	if (stat)
		goto bad_apr;
	stat=apr_file_pipe_create(&set->events_notify_pipe_read,
				&set->events_notify_pipe_write, set->pool);
	if (stat)
		goto bad_apr;
	stat=apr_file_pipe_timeout_set(set->events_notify_pipe_write, 0);
	if (stat)
		goto bad_apr;
	stat=apr_file_pipe_timeout_set(set->events_notify_pipe_read, 0);
	if (stat)
		goto bad_apr;
	/* XXX do we need an explicit check for APR_ENOTIMPL? */
	stat=apr_pollset_create(&set->pollset, set->config.max_fds, set->pool,
				APR_POLLSET_THREADSAFE);
	if (stat)
		goto bad_apr;
	pollfd.p=set->pool;
	pollfd.desc_type=APR_POLL_FILE;
	pollfd.reqevents=APR_POLLIN;
	pollfd.desc.f=set->shutdown_pipe_read;
	pollfd.client_data=set;
	stat=apr_pollset_add(set->pollset, &pollfd);
	if (stat)
		goto bad_apr;
	ret=pthread_create(&set->thread, NULL, listener, set);
	if (ret) {
		ret=-ret;
		goto bad_posix;
	}
	*new_set=set;
	return 0;

bad_apr:
	ret=-APR_TO_OS_ERROR(stat);
bad_posix:
	apr_pool_destroy(set->pool);
bad_pool:
	free(set);
bad_alloc:
	mrpc_put();
bad_init:
	return ret;
}

/* XXX drops lots of stuff on the floor */
exported void mrpc_conn_set_free(struct mrpc_conn_set *set)
{
	apr_file_putc('s', set->shutdown_pipe_write);
	pthread_mutex_lock(&set->events_lock);
	while (set->events_threads)
		pthread_cond_wait(&set->events_threads_cond, &set->events_lock);
	pthread_mutex_unlock(&set->events_lock);
	pthread_join(set->thread, NULL);
	apr_pool_destroy(set->pool);
	free(set);
	mrpc_put();
}
