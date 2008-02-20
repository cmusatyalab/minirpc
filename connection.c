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

static void try_read_conn(void *data, int fd);
static void try_write_conn(void *data, int fd);
static void try_accept(void *data, int fd);
static void conn_hangup(void *data, int fd);
static void conn_error(void *data, int fd);

/* Ownership of the @conn_pool passes to the connection */
static apr_status_t mrpc_conn_add(struct mrpc_connection **new_conn,
			struct mrpc_conn_set *set, apr_socket_t *sock,
			apr_pool_t *conn_pool)
{
	struct mrpc_connection *conn;
	pthread_mutexattr_t attr;
	apr_status_t stat;
	int fd;
	int ret;

	*new_conn=NULL;
	apr_socket_opt_set(sock, APR_SO_KEEPALIVE, 1);
	stat=apr_socket_opt_set(sock, APR_SO_NONBLOCK, 1);
	if (stat)
		return stat;
	conn=apr_pcalloc(conn_pool, sizeof(*conn));
	if (conn == NULL)
		return APR_ENOMEM;
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
	conn->sock=sock;
	conn->pool=conn_pool;
	conn->pending_replies=g_hash_table_new(g_int_hash, g_int_equal);
	apr_os_sock_get(&fd, sock);
	ret=pollset_add(set->pollset, fd, POLLSET_READABLE, conn,
				try_read_conn, try_write_conn, conn_hangup,
				conn_error);
	if (ret) {
		stat=APR_FROM_OS_ERROR(ret);
		return stat;
	}
	*new_conn=conn;
	return APR_SUCCESS;
}
/* XXX unregister sock from pollset at pool release */

void mrpc_conn_free(struct mrpc_connection *conn)
{
	/* XXX data already in buffer? */
	apr_pool_destroy(conn->pool);
}

exported apr_status_t mrpc_connect(struct mrpc_connection **new_conn,
			struct mrpc_conn_set *set, char *host, unsigned port,
			void *data)
{
	struct mrpc_connection *conn;
	apr_pool_t *conn_pool;
	apr_pool_t *lookup_pool;
	apr_sockaddr_t *sa;
	apr_socket_t *sock;
	apr_status_t stat;

	*new_conn=NULL;

	if (set->config.protocol->is_server)
		return APR_EINVAL;

	stat=apr_pool_create(&conn_pool, set->pool);
	if (stat)
		return stat;
	stat=apr_pool_create(&lookup_pool, set->pool);
	if (stat)
		goto bad;
	stat=apr_sockaddr_info_get(&sa, host, APR_UNSPEC, port, 0, lookup_pool);
	for (; sa != NULL; sa=sa->next) {
		apr_pool_clear(conn_pool);
		stat=apr_socket_create(&sock, sa->family, SOCK_STREAM, 0,
					conn_pool);
		if (stat)
			continue;
		stat=apr_socket_connect(sock, sa);
		if (stat == APR_SUCCESS)
			break;
	}
	apr_pool_destroy(lookup_pool);
	if (stat)
		goto bad;
	stat=mrpc_conn_add(&conn, set, sock, conn_pool);
	if (stat)
		goto bad;
	conn->private = (data != NULL) ? data : conn;
	*new_conn=conn;
	return APR_SUCCESS;

bad:
	apr_pool_destroy(conn_pool);
	return stat;
}

exported apr_status_t mrpc_listen(struct mrpc_conn_set *set, char *listenaddr,
			unsigned port, int *bound)
{
	apr_pool_t *pool;
	apr_sockaddr_t *sa;
	apr_socket_t *sock;
	apr_status_t stat;
	int count=0;
	int ret;
	int fd;

	if (bound)
		*bound=0;
	if (!set->config.protocol->is_server)
		return APR_EINVAL;
	stat=apr_pool_create(&pool, set->pool);
	if (stat)
		return stat;
	stat=apr_sockaddr_info_get(&sa, listenaddr, APR_UNSPEC, port, 0, pool);
	for (; sa != NULL; sa=sa->next) {
		stat=apr_socket_create(&sock, sa->family, SOCK_STREAM, 0,
					set->pool);
		if (stat)
			continue;
		stat=apr_socket_opt_set(sock, APR_SO_REUSEADDR, 1);
		if (stat) {
			apr_socket_close(sock);
			continue;
		}
		stat=apr_socket_opt_set(sock, APR_SO_NONBLOCK, 1);
		if (stat) {
			apr_socket_close(sock);
			continue;
		}
		stat=apr_socket_bind(sock, sa);
		if (stat) {
			apr_socket_close(sock);
			continue;
		}
		if (apr_socket_listen(sock, set->config.listen_backlog)) {
			apr_socket_close(sock);
			continue;
		}
		apr_os_sock_get(&fd, sock);
		ret=pollset_add(set->pollset, fd, POLLSET_READABLE,
					set, try_accept, NULL, NULL, NULL);
		if (ret) {
			apr_socket_close(sock);
			stat=APR_FROM_OS_ERROR(ret);
			continue;
		}
		count++;
	}
	apr_pool_destroy(pool);
	if (bound)
		*bound=count;
	if (count == 0)
		return stat;
	return APR_SUCCESS;
}

exported apr_status_t mrpc_conn_set_alloc_subpool(apr_pool_t **new_pool,
			struct mrpc_conn_set *set)
{
	return apr_pool_create(new_pool, set->pool);
}

/* The provided @sock must be a connected socket (i.e., not a listener).
   @conn_pool must be a subpool of the set pool.  Ownership of @conn_pool
   and @sock transfers to miniRPC. */
exported apr_status_t mrpc_bind_fd(struct mrpc_connection **new_conn,
			struct mrpc_conn_set *set, apr_socket_t *sock,
			apr_pool_t *conn_pool, void *data)
{
	struct mrpc_connection *conn;
	apr_status_t stat;

	*new_conn=NULL;
	stat=mrpc_conn_add(&conn, set, sock, conn_pool);
	if (stat)
		return stat;
	conn->private = (data != NULL) ? data : conn;
	*new_conn=conn;
	return 0;
}

static void _conn_close(struct mrpc_connection *conn)
{
	int fd;

	/* XXX */
	apr_os_sock_get(&fd, conn->sock);
	pollset_del(conn->set->pollset, fd);
	apr_socket_close(conn->sock);
}

exported void mrpc_conn_close(struct mrpc_connection *conn)
{
	_conn_close(conn);
	mrpc_conn_free(conn);
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

	_conn_close(conn);
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

static void try_read_conn(void *data, int fd)
{
	struct mrpc_connection *conn=data;
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
				if (APR_STATUS_IS_EOF(stat)) {
					conn_kill(conn, MRPC_DISC_CLOSED);
				} else if (!APR_STATUS_IS_EAGAIN(stat)) {
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
	int fd;

	pthread_mutex_lock(&conn->send_msgs_lock);
	if (g_queue_is_empty(conn->send_msgs)) {
		apr_os_sock_get(&fd, conn->sock);
		pollset_modify(conn->set->pollset, fd, POLLSET_READABLE);
		pthread_mutex_unlock(&conn->send_msgs_lock);
		return MINIRPC_OK;
	}
	conn->send_msg=g_queue_pop_head(conn->send_msgs);
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

static void try_write_conn(void *data, int fd)
{
	struct mrpc_connection *conn=data;
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
				if (!APR_STATUS_IS_EAGAIN(stat))
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

static void try_accept(void *data, int fd)
{
	struct mrpc_conn_set *set=data;
	struct mrpc_connection *conn;
	struct mrpc_event *event;
	apr_pool_t *pool;
	apr_socket_t *listensock=NULL;
	apr_socket_t *sock;
	apr_status_t stat;

	/* XXX error handling */
	while (1) {
		stat=apr_pool_create(&pool, set->pool);
		if (stat)
			return;
		stat=apr_os_sock_put(&listensock, &fd, pool);
		if (stat)
			return;
		stat=apr_socket_accept(&sock, listensock, pool);
		if (stat)
			break;
		if (mrpc_conn_add(&conn, set, sock, pool)) {
			/* XXX */
			apr_pool_destroy(pool);
			continue;
		}
		event=mrpc_alloc_event(conn, EVENT_ACCEPT);
		if (event == NULL)
			/* XXX */;
		stat=apr_socket_addr_get(&event->addr, APR_REMOTE, sock);
		if (stat)
			event->addr=NULL;
		queue_event(event);
	}
	apr_pool_destroy(pool);
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

static void listener_shutdown(void *data, int fd)
{
	struct mrpc_conn_set *set=data;

	set->shutdown=1;
}

static void pipe_error(void *data, int fd)
{
	assert(0);
}

/* XXX signal handling */
static void *listener(void *data)
{
	struct mrpc_conn_set *set=data;

	while (!set->shutdown) {
		if (pollset_poll(set->pollset))
			/* XXX */;
	}
	return NULL;
}

mrpc_status_t send_message(struct mrpc_message *msg)
{
	struct mrpc_connection *conn=msg->conn;
	int fd;

	pthread_mutex_lock(&conn->send_msgs_lock);
	apr_os_sock_get(&fd, conn->sock);
	if (pollset_modify(conn->set->pollset, fd,
				POLLSET_READABLE|POLLSET_WRITABLE)) {
		pthread_mutex_unlock(&conn->send_msgs_lock);
		mrpc_free_message(msg);
		return MINIRPC_NETWORK_FAILURE;
	}
	g_queue_push_tail(conn->send_msgs, msg);
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
	copy_default(from, to, msg_max_buf_len, 16000);
	copy_default(from, to, listen_backlog, 16);
}
#undef copy_default

exported apr_status_t mrpc_conn_set_alloc(struct mrpc_conn_set **new_set,
			const struct mrpc_config *config,
			const struct mrpc_set_operations *ops,
			void *set_data, apr_pool_t *parent_pool)
{
	struct mrpc_conn_set *set;
	apr_pool_t *pool;
	int ret;
	apr_status_t stat;

	*new_set=NULL;
	if (config == NULL || config->protocol == NULL || ops == NULL ||
				new_set == NULL)
		return APR_EINVAL;
	if (config->protocol->is_server) {
		/* We require the accept method to exist.  Without it, the
		   connection will never have a non-NULL operations pointer and
		   the application will never be aware that the connection
		   exists, so the connecting client will be forever stuck in
		   PROCEDURE_UNAVAIL limbo. */
		if (ops->accept == NULL)
			return APR_EINVAL;
	} else {
		/* The accept method is irrelevant for clients.  Tell the
		   application if its assumptions are wrong. */
		if (ops->accept != NULL)
			return APR_EINVAL;
	}

	stat=mrpc_init();
	if (stat)
		return stat;
	stat=apr_pool_create(&pool, parent_pool);
	if (stat)
		return stat;
	set=apr_pcalloc(pool, sizeof(*set));
	if (set == NULL) {
		stat=APR_ENOMEM;
		goto bad;
	}
	validate_copy_config(config, &set->config);
	pthread_mutex_init(&set->events_lock, NULL);
	pthread_cond_init(&set->events_threads_cond, NULL);
	set->event_conns=g_queue_new();
	set->pool=pool;
	set->ops=ops;
	set->private = (set_data != NULL) ? set_data : set;
	set->shutdown_pipe=selfpipe_create();
	set->events_notify_pipe=selfpipe_create();
	if (set->shutdown_pipe == NULL || set->events_notify_pipe == NULL) {
		stat=APR_ENOMEM;
		goto bad;
	}
	set->pollset=pollset_alloc();
	if (set->pollset == NULL) {
		stat=APR_ENOMEM;
		goto bad;
	}
	ret=pollset_add(set->pollset, selfpipe_fd(set->shutdown_pipe),
				POLLSET_READABLE, set, listener_shutdown,
				NULL, NULL, pipe_error);
	if (ret) {
		stat=APR_FROM_OS_ERROR(ret);
		goto bad;
	}
	ret=pthread_create(&set->thread, NULL, listener, set);
	if (ret) {
		stat=APR_FROM_OS_ERROR(ret);
		goto bad;
	}
	*new_set=set;
	return APR_SUCCESS;

bad:
	if (set->pollset)
		pollset_free(set->pollset);
	if (set->events_notify_pipe)
		selfpipe_destroy(set->events_notify_pipe);
	if (set->shutdown_pipe)
		selfpipe_destroy(set->shutdown_pipe);
	apr_pool_destroy(pool);
	return stat;
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
	apr_pool_destroy(set->pool);
}
