/*
 * miniRPC - TCP RPC library with asynchronous operations and TLS support
 *
 * Copyright (C) 2007-2008 Carnegie Mellon University
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
#define MINIRPC_INTERNAL
#include "internal.h"

static int setsockoptval(int fd, int level, int optname, int value)
{
	return setsockopt(fd, level, optname, &value, sizeof(value));
}

static void conn_kill(struct mrpc_connection *conn,
			enum mrpc_disc_reason reason)
{
	struct mrpc_event *event;
	int done;

	pthread_mutex_lock(&conn->send_msgs_lock);
	done=conn->shutdown;
	conn->shutdown=1;
	pthread_mutex_unlock(&conn->send_msgs_lock);
	if (done)
		return;
	pollset_del(conn->set->pollset, conn->fd);
	/* We are now guaranteed that the listener thread will not process
	   this connection further */
	close(conn->fd);
	conn->fd=-1;
	event=mrpc_alloc_event(conn, EVENT_DISCONNECT);
	event->disc_reason=reason;
	queue_event(event);
}

static mrpc_status_t process_incoming_header(struct mrpc_connection *conn)
{
	mrpc_status_t ret;

	ret=unserialize((xdrproc_t)xdr_mrpc_header, conn->recv_hdr_buf,
				MINIRPC_HEADER_LEN, &conn->recv_msg->hdr,
				sizeof(conn->recv_msg->hdr));
	if (ret)
		return ret;
	if (conn->recv_msg->hdr.datalen > conn->set->conf.msg_max_buf_len) {
		/* XXX doesn't get returned to client if request */
  		return MINIRPC_ENCODING_ERR;
	}

	if (conn->recv_msg->hdr.datalen)
		mrpc_alloc_message_data(conn->recv_msg,
					conn->recv_msg->hdr.datalen);

	return MINIRPC_OK;
}

static void try_read_conn(void *data, int fd)
{
	struct mrpc_connection *conn=data;
	size_t count;
	ssize_t rcount;
	char *buf;
	unsigned len;

	while (1) {
		if (conn->recv_msg == NULL)
			conn->recv_msg=mrpc_alloc_message(conn);

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
			assert(conn->fd != -1);
			rcount=read(conn->fd, buf + conn->recv_offset, count);
			if (rcount <= 0) {
				if (rcount == 0)
					conn_kill(conn, MRPC_DISC_CLOSED);
				else if (errno != EAGAIN && errno != EINTR)
					conn_kill(conn, MRPC_DISC_IOERR);
				return;
			}
			conn->recv_offset += rcount;
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
	assert(conn->fd != -1);
	if (g_queue_is_empty(conn->send_msgs)) {
		pollset_modify(conn->set->pollset, conn->fd, POLLSET_READABLE);
		pthread_mutex_unlock(&conn->send_msgs_lock);
		return MINIRPC_OK;
	}
	conn->send_msg=g_queue_pop_head(conn->send_msgs);
	pthread_mutex_unlock(&conn->send_msgs_lock);

	ret=serialize((xdrproc_t)xdr_mrpc_header, &conn->send_msg->hdr,
				conn->send_hdr_buf, MINIRPC_HEADER_LEN);
	if (ret) {
		/* XXX message dropped on floor */
		mrpc_free_message(conn->send_msg);
		conn->send_msg=NULL;
		return ret;
	}
	return MINIRPC_OK;
}

static void try_write_conn(void *data, int fd)
{
	struct mrpc_connection *conn=data;
	size_t count;
	ssize_t rcount;
	char *buf;
	unsigned len;

	while (1) {
		if (conn->send_msg == NULL) {
			if (get_next_message(conn)) {
				/* Message dropped on floor.  Better luck
				   next time? */
				continue;
			}
			if (conn->send_msg == NULL) {
				if (conn->send_state != STATE_IDLE) {
					setsockoptval(conn->fd, IPPROTO_TCP,
							TCP_CORK, 0);
					conn->send_state=STATE_IDLE;
				}
				break;
			}
			if (conn->send_state == STATE_IDLE) {
				setsockoptval(conn->fd, IPPROTO_TCP, TCP_CORK,
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
			assert(conn->fd != -1);
			rcount=write(conn->fd, buf + conn->send_offset, count);
			if (rcount == -1 && errno != EAGAIN
						&& errno != EINTR) {
				conn_kill(conn, MRPC_DISC_IOERR);
				return;
			}
			conn->send_offset += rcount;
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

static void conn_hangup(void *data, int fd)
{
	struct mrpc_connection *conn=data;

	/* XXX we may not get this */
	conn_kill(conn, MRPC_DISC_CLOSED);
}

static void conn_error(void *data, int fd)
{
	struct mrpc_connection *conn=data;

	conn_kill(conn, MRPC_DISC_IOERR);
}

static int mrpc_conn_add(struct mrpc_connection **new_conn,
			struct mrpc_conn_set *set, int fd)
{
	struct mrpc_connection *conn;
	pthread_mutexattr_t attr;
	int ret;

	*new_conn=NULL;
	ret=setsockoptval(fd, SOL_SOCKET, SO_KEEPALIVE, 1);
	if (ret)
		return ret;
	ret=set_nonblock(fd);
	if (ret)
		return ret;
	conn=g_slice_new0(struct mrpc_connection);
	conn->send_msgs=g_queue_new();
	conn->events=g_queue_new();
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
	conn->fd=fd;
	conn->pending_replies=g_hash_table_new_full(g_int_hash, g_int_equal,
				NULL, pending_kill);
	ret=pollset_add(set->pollset, fd, POLLSET_READABLE, conn,
				try_read_conn, try_write_conn, conn_hangup,
				conn_error);
	if (ret) {
		g_hash_table_destroy(conn->pending_replies);
		g_queue_free(conn->events);
		g_queue_free(conn->send_msgs);
		g_slice_free(struct mrpc_connection, conn);
		return ret;
	}
	*new_conn=conn;
	return 0;
}
/* XXX unregister sock from pollset at pool release */

void mrpc_conn_free(struct mrpc_connection *conn)
{
	struct mrpc_message *msg;

	destroy_events(conn);
	g_queue_free(conn->events);
	if (conn->send_msg)
		mrpc_free_message(conn->send_msg);
	if (conn->recv_msg)
		mrpc_free_message(conn->recv_msg);
	while ((msg=g_queue_pop_head(conn->send_msgs)) != NULL)
		mrpc_free_message(msg);
	g_queue_free(conn->send_msgs);
	g_slice_free(struct mrpc_connection, conn);
}

static void try_accept(void *data, int listenfd)
{
	struct mrpc_conn_set *set=data;
	struct mrpc_connection *conn;
	struct mrpc_event *event;
	struct sockaddr_storage sa;
	socklen_t len;
	int fd;

	/* XXX error handling */
	while (1) {
		len=sizeof(sa);
		fd=accept(listenfd, (struct sockaddr *)&sa, &len);
		if (fd == -1)
			break;
		if (mrpc_conn_add(&conn, set, fd)) {
			close(fd);
			continue;
		}
		event=mrpc_alloc_event(conn, EVENT_ACCEPT);
		if (event == NULL)
			/* XXX */;
		event->addr=g_memdup(&sa, len);
		event->addrlen=len;
		queue_event(event);
	}
}

static int lookup_addr(struct addrinfo **res, const char *host, unsigned port,
			int passive)
{
	char *portstr;
	int ret;
	struct addrinfo hints = {
		.ai_family = AF_UNSPEC,
		.ai_socktype = SOCK_STREAM,
		.ai_flags = AI_NUMERICSERV
	};

	if (asprintf(&portstr, "%u", port) == -1)
		return -ENOMEM;
	if (passive)
		hints.ai_flags |= AI_PASSIVE;
	ret=getaddrinfo(host, portstr, &hints, res);
	free(portstr);

	switch (ret) {
	case 0:
		return 0;
	case EAI_ADDRFAMILY:
		return -EADDRNOTAVAIL;
	case EAI_AGAIN:
		return -EAGAIN;
	case EAI_BADFLAGS:
		return -EINVAL;
	case EAI_FAIL:
		return -EIO;
	case EAI_FAMILY:
		return -EAFNOSUPPORT;
	case EAI_MEMORY:
		return -ENOMEM;
	case EAI_NODATA:
		return -EIO;
	case EAI_NONAME:
		return -EIO;
	case EAI_SERVICE:
		return -EOPNOTSUPP;
	case EAI_SOCKTYPE:
		return -ESOCKTNOSUPPORT;
	case EAI_SYSTEM:
		return -errno;
	default:
		return -EIO;
	}
}

exported int mrpc_connect(struct mrpc_connection **new_conn,
			struct mrpc_conn_set *set, const char *host,
			unsigned port, void *data)
{
	struct mrpc_connection *conn;
	struct addrinfo *ai;
	struct addrinfo *cur;
	int fd;
	int ret;

	*new_conn=NULL;
	if (set->conf.protocol->is_server)
		return -EINVAL;
	ret=lookup_addr(&ai, host, port, 0);
	if (ret)
		return ret;
	if (ai == NULL)
		return -EIO;
	for (cur=ai; cur != NULL; cur=cur->ai_next) {
		fd=socket(cur->ai_family, cur->ai_socktype, cur->ai_protocol);
		if (fd == -1) {
			ret=-errno;
			continue;
		}
		if (!connect(fd, cur->ai_addr, cur->ai_addrlen))
			break;
		ret=-errno;
		close(fd);
		fd=-1;
	}
	freeaddrinfo(ai);
	if (fd == -1)
		return ret;
	ret=mrpc_conn_add(&conn, set, fd);
	if (ret) {
		close(fd);
		return ret;
	}
	conn->private = (data != NULL) ? data : conn;
	*new_conn=conn;
	return 0;
}

exported int mrpc_listen(struct mrpc_conn_set *set, const char *listenaddr,
			unsigned *port, int *bound)
{
	struct addrinfo *ai;
	struct addrinfo *cur;
	int fd;
	int count=0;
	int ret;

	if (bound)
		*bound=0;
	if (port == NULL || !set->conf.protocol->is_server)
		return -EINVAL;
	ret=lookup_addr(&ai, listenaddr, *port, 1);
	if (ret)
		return ret;
	for (cur=ai; cur != NULL; cur=cur->ai_next) {
		if (cur->ai_family != AF_INET && cur->ai_family != AF_INET6) {
			ret=-EPROTONOSUPPORT;
			continue;
		}
		fd=socket(cur->ai_family, cur->ai_socktype, cur->ai_protocol);
		if (fd == -1) {
			ret=-errno;
			continue;
		}
		ret=setsockoptval(fd, SOL_SOCKET, SO_REUSEADDR, 1);
		if (ret) {
			close(fd);
			continue;
		}
		ret=set_nonblock(fd);
		if (ret) {
			close(fd);
			continue;
		}
		if (bind(fd, cur->ai_addr, cur->ai_addrlen)) {
			ret=-errno;
			close(fd);
			continue;
		}
		if (listen(fd, set->conf.listen_backlog)) {
			ret=-errno;
			close(fd);
			continue;
		}
		ret=pollset_add(set->pollset, fd, POLLSET_READABLE,
					set, try_accept, NULL, NULL, NULL);
		if (ret) {
			close(fd);
			continue;
		}
		count++;
		if (!*port) {
			if (getsockname(fd, cur->ai_addr, &cur->ai_addrlen)) {
				ret=-errno;
				pollset_del(set->pollset, fd);
				close(fd);
				continue;
			}
			if (cur->ai_family == AF_INET)
				*port=ntohs(((struct sockaddr_in *)
						cur->ai_addr)->sin_port);
			else
				*port=ntohs(((struct sockaddr_in6 *)
						cur->ai_addr)->sin6_port);
			/* Stop after binding to the first random port */
			break;
		}
	}
	freeaddrinfo(ai);
	if (bound)
		*bound=count;
	if (count == 0)
		return ret;
	return 0;
}

/* The provided @fd must be a connected socket (i.e., not a listener).
   Ownership of @fd transfers to miniRPC. */
exported int mrpc_bind_fd(struct mrpc_connection **new_conn,
			struct mrpc_conn_set *set, int fd, void *data)
{
	struct mrpc_connection *conn;
	int ret;

	*new_conn=NULL;
	ret=mrpc_conn_add(&conn, set, fd);
	if (ret)
		return ret;
	conn->private = (data != NULL) ? data : conn;
	*new_conn=conn;
	return 0;
}

exported void mrpc_conn_close(struct mrpc_connection *conn)
{
	conn_kill(conn, MRPC_DISC_USER);
}

exported mrpc_status_t mrpc_conn_set_operations(struct mrpc_connection *conn,
			struct mrpc_protocol *protocol, const void *ops)
{
	if (conn->set->conf.protocol != protocol)
		return MINIRPC_INVALID_ARGUMENT;
	pthread_mutex_lock(&conn->operations_lock);
	conn->operations=ops;
	pthread_mutex_unlock(&conn->operations_lock);
	return MINIRPC_OK;
}

mrpc_status_t send_message(struct mrpc_message *msg)
{
	struct mrpc_connection *conn=msg->conn;

	pthread_mutex_lock(&conn->send_msgs_lock);
	if (conn->shutdown || pollset_modify(conn->set->pollset, conn->fd,
				POLLSET_READABLE|POLLSET_WRITABLE)) {
		pthread_mutex_unlock(&conn->send_msgs_lock);
		mrpc_free_message(msg);
		return MINIRPC_NETWORK_FAILURE;
	}
	g_queue_push_tail(conn->send_msgs, msg);
	pthread_mutex_unlock(&conn->send_msgs_lock);
	return MINIRPC_OK;
}

/* XXX signal handling */
static void *listener(void *data)
{
	struct mrpc_conn_set *set=data;

	while (!selfpipe_is_set(set->shutdown_pipe)) {
		if (pollset_poll(set->pollset))
			/* XXX */;
	}
	return NULL;
}

#define copy_default(from, to, field, default) do { \
		to->field=from->field ? from->field : default; \
	} while (0)
static int validate_copy_config(const struct mrpc_config *from,
			struct mrpc_config *to)
{
	if (from == NULL || from->protocol == NULL)
		return -EINVAL;
	to->protocol=from->protocol;

	if (from->protocol->is_server) {
		/* We require the accept method to exist.  Without it, the
		   connection will never have a non-NULL operations pointer and
		   the application will never be aware that the connection
		   exists, so the connecting client will be forever stuck in
		   PROCEDURE_UNAVAIL limbo. */
		if (from->accept == NULL)
			return -EINVAL;
	} else {
		/* The accept method is irrelevant for clients.  Tell the
		   application if its assumptions are wrong. */
		if (from->accept != NULL)
			return -EINVAL;
	}
	to->accept=from->accept;
	to->disconnect=from->disconnect;
	to->ioerr=from->ioerr;
	copy_default(from, to, msg_max_buf_len, 16000);
	copy_default(from, to, listen_backlog, 16);
	return 0;
}
#undef copy_default

static void pipe_error(void *data, int fd)
{
	assert(0);
}

exported int mrpc_conn_set_alloc(struct mrpc_conn_set **new_set,
			const struct mrpc_config *config, void *set_data)
{
	struct mrpc_conn_set *set;
	int ret;

	if (new_set == NULL)
		return -EINVAL;
	*new_set=NULL;
	set=g_slice_new0(struct mrpc_conn_set);
	ret=validate_copy_config(config, &set->conf);
	if (ret)
		goto bad;
	pthread_mutex_init(&set->events_lock, NULL);
	pthread_cond_init(&set->events_threads_cond, NULL);
	set->event_conns=g_queue_new();
	set->private = (set_data != NULL) ? set_data : set;
	ret=selfpipe_create(&set->shutdown_pipe);
	if (ret)
		goto bad;
	ret=selfpipe_create(&set->events_notify_pipe);
	if (ret)
		goto bad;
	ret=pollset_alloc(&set->pollset);
	if (ret)
		goto bad;
	ret=pollset_add(set->pollset, selfpipe_fd(set->shutdown_pipe),
				POLLSET_READABLE, NULL, NULL, NULL, NULL,
				pipe_error);
	if (ret)
		goto bad;
	ret=pthread_create(&set->thread, NULL, listener, set);
	if (ret)
		goto bad;
	*new_set=set;
	return 0;

bad:
	if (set->pollset)
		pollset_free(set->pollset);
	if (set->events_notify_pipe)
		selfpipe_destroy(set->events_notify_pipe);
	if (set->shutdown_pipe)
		selfpipe_destroy(set->shutdown_pipe);
	g_slice_free(struct mrpc_conn_set, set);
	return ret;
}

/* XXX drops lots of stuff on the floor */
exported void mrpc_conn_set_free(struct mrpc_conn_set *set)
{
	selfpipe_set(set->shutdown_pipe);
	pthread_mutex_lock(&set->events_lock);
	while (set->events_threads)
		pthread_cond_wait(&set->events_threads_cond, &set->events_lock);
	pthread_mutex_unlock(&set->events_lock);
	pthread_join(set->thread, NULL);
	pollset_free(set->pollset);
	selfpipe_destroy(set->events_notify_pipe);
	selfpipe_destroy(set->shutdown_pipe);
	g_slice_free(struct mrpc_conn_set, set);
}
