/*
 * miniRPC - TCP RPC library with asynchronous operations
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
	if (setsockopt(fd, level, optname, &value, sizeof(value)))
		return errno;
	return 0;
}

static void conn_set_get(struct mrpc_conn_set *set)
{
	pthread_mutex_lock(&set->conns_lock);
	assert(set->refs > 0);
	set->refs++;
	pthread_mutex_unlock(&set->conns_lock);
}

static void conn_set_put(struct mrpc_conn_set *set)
{
	pthread_mutex_lock(&set->conns_lock);
	set->refs--;
	if (!set->refs)
		pthread_cond_broadcast(&set->refs_cond);
	pthread_mutex_unlock(&set->conns_lock);
}

static void conn_kill(struct mrpc_connection *conn,
			enum mrpc_disc_reason reason);
static void try_close_fd(struct mrpc_connection *conn);

/* Returns error code if the connection should be killed */
static mrpc_status_t process_incoming_header(struct mrpc_connection *conn)
{
	mrpc_status_t ret;

	ret=unserialize((xdrproc_t)xdr_mrpc_header, conn->recv_hdr_buf,
				MINIRPC_HEADER_LEN, &conn->recv_msg->hdr,
				sizeof(conn->recv_msg->hdr));
	if (ret) {
		queue_ioerr_event(conn, "Header deserialize failure");
		/* We are now desynchronized */
		return MINIRPC_ENCODING_ERR;
	}

	if (conn->recv_msg->hdr.datalen >
				get_config(conn->set, msg_max_buf_len)) {
		queue_ioerr_event(conn, "Payload over maximum, seq %u len %u",
					conn->recv_msg->hdr.sequence,
					conn->recv_msg->hdr.datalen);
		conn->recv_msg->recv_error=MINIRPC_ENCODING_ERR;
	} else if (conn->recv_msg->hdr.datalen) {
		mrpc_alloc_message_data(conn->recv_msg,
					conn->recv_msg->hdr.datalen);
	}
	return MINIRPC_OK;
}

/* We'd rather use atomic operations, but we need 64-bit counters */
#define conn_counter_inc(conn, ctr, amount) do {		\
		pthread_mutex_lock(&(conn)->counters_lock);	\
		(conn)->counters[ctr] += amount;		\
		pthread_mutex_unlock(&(conn)->counters_lock);	\
	} while (0)

static void try_read_conn(void *data)
{
	struct mrpc_connection *conn=data;
	size_t count;
	ssize_t rcount;
	char *buf;

	while (1) {
		if (conn->recv_msg == NULL) {
			conn->recv_msg=mrpc_alloc_message(conn);
			conn->recv_remaining=MINIRPC_HEADER_LEN;
		}

		switch (conn->recv_state) {
		case STATE_HEADER:
			buf=conn->recv_hdr_buf + MINIRPC_HEADER_LEN -
						conn->recv_remaining;
			count=conn->recv_remaining;
			break;
		case STATE_DATA:
			buf=conn->recv_msg->data +
						conn->recv_msg->hdr.datalen -
						conn->recv_remaining;
			count=conn->recv_remaining;
			break;
		case STATE_INVALID:
			/* We defer allocation of the trash buffer until the
			   first time we need it, since usually we won't */
			if (conn->set->trashbuf == NULL)
				conn->set->trashbuf=g_malloc(TRASHBUFSIZE);
			buf=conn->set->trashbuf;
			count=min(conn->recv_remaining, TRASHBUFSIZE);
			break;
		default:
			assert(0);
		}

		if (conn->recv_remaining) {
			assert(!(conn->sequence_flags & SEQ_FD_CLOSED));
			rcount=read(conn->fd, buf, count);
			if (rcount <= 0) {
				if (rcount == 0) {
					conn_kill(conn, MRPC_DISC_CLOSED);
				} else if (errno != EAGAIN && errno != EINTR) {
					queue_ioerr_event(conn, "Error %d "
							"on read", errno);
					conn_kill(conn, MRPC_DISC_IOERR);
				}
				return;
			}
			conn->recv_remaining -= rcount;
			conn_counter_inc(conn, MRPC_CONNCTR_RECV_BYTES,
						rcount);
		}

		if (!conn->recv_remaining) {
			switch (conn->recv_state) {
			case STATE_HEADER:
				if (process_incoming_header(conn)) {
					conn_kill(conn, MRPC_DISC_IOERR);
					return;
				}
				conn->recv_remaining =
						conn->recv_msg->hdr.datalen;
				if (conn->recv_msg->recv_error)
					conn->recv_state=STATE_INVALID;
				else
					conn->recv_state=STATE_DATA;
				break;
			case STATE_DATA:
			case STATE_INVALID:
				conn_counter_inc(conn, MRPC_CONNCTR_RECV_MSGS,
							1);
				process_incoming_message(conn->recv_msg);
				conn->recv_state=STATE_HEADER;
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
	assert(!(conn->sequence_flags & SEQ_FD_CLOSED));
	if (g_queue_is_empty(conn->send_msgs)) {
		pollset_modify(conn->set->pollset, conn->fd, POLLSET_READABLE);
		pthread_mutex_unlock(&conn->send_msgs_lock);
		try_close_fd(conn);
		return MINIRPC_OK;
	}
	conn->send_msg=g_queue_pop_head(conn->send_msgs);
	pthread_mutex_unlock(&conn->send_msgs_lock);

	ret=serialize((xdrproc_t)xdr_mrpc_header, &conn->send_msg->hdr,
				conn->send_hdr_buf, MINIRPC_HEADER_LEN);
	if (ret) {
		/* Message dropped on floor */
		queue_ioerr_event(conn, "Header serialize failure");
		mrpc_free_message(conn->send_msg);
		conn->send_msg=NULL;
		return ret;
	}
	return MINIRPC_OK;
}

static void try_write_conn(void *data)
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
					if (conn->is_tcp)
						setsockoptval(conn->fd,
								IPPROTO_TCP,
								TCP_CORK, 0);
					conn->send_state=STATE_IDLE;
				}
				break;
			}
			if (conn->send_state == STATE_IDLE) {
				if (conn->is_tcp)
					setsockoptval(conn->fd, IPPROTO_TCP,
							TCP_CORK, 1);
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
			assert(!(conn->sequence_flags & SEQ_FD_CLOSED));
			rcount=write(conn->fd, buf + conn->send_offset, count);
			if (rcount == -1 && errno != EAGAIN
						&& errno != EINTR) {
				queue_ioerr_event(conn, "Error %d on write",
							errno);
				conn_kill(conn, MRPC_DISC_IOERR);
				return;
			}
			conn->send_offset += rcount;
			conn_counter_inc(conn, MRPC_CONNCTR_SEND_BYTES,
						rcount);
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
				conn_counter_inc(conn, MRPC_CONNCTR_SEND_MSGS,
							1);
				break;
			default:
				assert(0);
			}
		}
	}
}

static void conn_hangup(void *data)
{
	struct mrpc_connection *conn=data;

	conn_kill(conn, MRPC_DISC_CLOSED);
}

static void conn_error(void *data)
{
	struct mrpc_connection *conn=data;

	queue_ioerr_event(conn, "Poll reported I/O error");
	conn_kill(conn, MRPC_DISC_IOERR);
}

mrpc_status_t send_message(struct mrpc_message *msg)
{
	struct mrpc_connection *conn=msg->conn;
	mrpc_status_t ret=MINIRPC_OK;

	pthread_mutex_lock(&conn->send_msgs_lock);
	pthread_mutex_lock(&conn->sequence_lock);
	if (!(conn->sequence_flags & SEQ_HAVE_FD)) {
		ret=MINIRPC_INVALID_ARGUMENT;
		goto out;
	}
	if ((conn->sequence_flags & SEQ_SHUT_STARTED) ||
				pollset_modify(conn->set->pollset, conn->fd,
				POLLSET_READABLE|POLLSET_WRITABLE)) {
		ret=MINIRPC_NETWORK_FAILURE;
		goto out;
	}
	g_queue_push_tail(conn->send_msgs, msg);
out:
	pthread_mutex_unlock(&conn->sequence_lock);
	pthread_mutex_unlock(&conn->send_msgs_lock);
	if (ret)
		mrpc_free_message(msg);
	return ret;
}

exported int mrpc_conn_create(struct mrpc_connection **new_conn,
			struct mrpc_conn_set *set, void *data)
{
	struct mrpc_connection *conn;

	if (new_conn == NULL)
		return EINVAL;
	*new_conn=NULL;
	if (set == NULL)
		return EINVAL;
	conn_set_get(set);
	conn=g_slice_new0(struct mrpc_connection);
	g_atomic_int_set(&conn->refs, 1);
	conn->send_msgs=g_queue_new();
	conn->events=g_queue_new();
	conn->running_event_ref=ref_alloc();
	conn->operations_ref=ref_alloc();
	conn->lh_conns=g_list_append(NULL, conn);
	pthread_mutex_init(&conn->send_msgs_lock, NULL);
	pthread_mutex_init(&conn->counters_lock, NULL);
	pthread_mutex_init(&conn->pending_replies_lock, NULL);
	pthread_mutex_init(&conn->sequence_lock, NULL);
	conn->send_state=STATE_IDLE;
	conn->recv_state=STATE_HEADER;
	conn->set=set;
	conn->private = (data != NULL) ? data : conn;
	conn->pending_replies=g_hash_table_new_full(g_int_hash, g_int_equal,
				NULL, (GDestroyNotify)pending_free);
	pthread_mutex_lock(&set->conns_lock);
	g_queue_push_tail_link(set->conns, conn->lh_conns);
	pthread_mutex_unlock(&set->conns_lock);
	*new_conn=conn;
	return 0;
}

static int _mrpc_bind_fd(struct mrpc_connection *conn, int addr_family, int fd)
{
	int ret;
	int keepalive=get_config(conn->set, keepalive_enabled);

	pthread_mutex_lock(&conn->sequence_lock);
	if (conn->sequence_flags & SEQ_HAVE_FD) {
		ret=EINVAL;
		goto out;
	}
	if (addr_family == AF_INET || addr_family == AF_INET6)
		conn->is_tcp=1;
	if (keepalive && conn->is_tcp) {
		ret=setsockoptval(fd, IPPROTO_TCP, TCP_KEEPIDLE,
					get_config(conn->set, keepalive_time));
		if (ret)
			goto out;
		ret=setsockoptval(fd, IPPROTO_TCP, TCP_KEEPCNT,
					get_config(conn->set,
					keepalive_count));
		if (ret)
			goto out;
		ret=setsockoptval(fd, IPPROTO_TCP, TCP_KEEPINTVL,
					get_config(conn->set,
					keepalive_interval));
		if (ret)
			goto out;
	}
	ret=setsockoptval(fd, SOL_SOCKET, SO_KEEPALIVE, keepalive);
	if (ret)
		goto out;
	ret=set_nonblock(fd);
	if (ret)
		goto out;
	conn->fd=fd;
	ret=pollset_add(conn->set->pollset, fd, POLLSET_READABLE, conn,
				try_read_conn, try_write_conn, conn_hangup,
				conn_error, NULL);
	if (!ret)
		conn->sequence_flags |= SEQ_HAVE_FD;
out:
	pthread_mutex_unlock(&conn->sequence_lock);
	return ret;
}

/* Sequence lock must be held */
static void conn_start_shutdown(struct mrpc_connection *conn,
			enum mrpc_disc_reason reason)
{
	int done;

	done=conn->sequence_flags & SEQ_SHUT_STARTED;
	conn->sequence_flags |= SEQ_SHUT_STARTED;
	if (!done)
		conn->disc_reason=reason;
}

/* Must be called from listener-thread context */
static void try_close_fd(struct mrpc_connection *conn)
{
	int do_event=0;

	pthread_mutex_lock(&conn->sequence_lock);
	if ((conn->sequence_flags & SEQ_SHUT_STARTED) &&
				!(conn->sequence_flags & SEQ_FD_CLOSED)) {
		pollset_del(conn->set->pollset, conn->fd);
		/* We are now guaranteed that the listener thread will not
		   process this connection further */
		close(conn->fd);
		conn->sequence_flags |= SEQ_FD_CLOSED;
		do_event=1;
	}
	pthread_mutex_unlock(&conn->sequence_lock);
	if (do_event)
		kick_event_shutdown_sequence(conn);
}

/* Must be called from listener-thread context */
static void conn_kill(struct mrpc_connection *conn,
			enum mrpc_disc_reason reason)
{
	pthread_mutex_lock(&conn->sequence_lock);
	conn_start_shutdown(conn, reason);
	pthread_mutex_unlock(&conn->sequence_lock);
	/* Squash send queue */
	try_close_fd(conn);
}

static int _mrpc_conn_close(struct mrpc_connection *conn, int wait)
{
	refserial_t serial;
	int ret=0;

	conn_get(conn);
	pthread_mutex_lock(&conn->sequence_lock);
	conn_start_shutdown(conn, MRPC_DISC_USER);
	/* Squash event queue */
	if (conn->sequence_flags & SEQ_SQUASH_EVENTS) {
		pthread_mutex_unlock(&conn->sequence_lock);
		ret=EALREADY;
		goto out;
	}
	conn->sequence_flags |= SEQ_SQUASH_EVENTS;
	if (!(conn->sequence_flags & SEQ_FD_CLOSED)) {
		if (!(conn->sequence_flags & SEQ_HAVE_FD)) {
			conn->sequence_flags |= SEQ_FD_CLOSED;
			pthread_mutex_unlock(&conn->sequence_lock);
			kick_event_shutdown_sequence(conn);
			goto out;
		}
		pollset_modify(conn->set->pollset, conn->fd,
					POLLSET_READABLE | POLLSET_WRITABLE);
	}
	pthread_mutex_unlock(&conn->sequence_lock);
	if (wait) {
		serial=ref_update(conn->running_event_ref);
		ref_wait(conn->running_event_ref, serial);
	}
out:
	conn_put(conn);
	return ret;
}

exported int mrpc_conn_close(struct mrpc_connection *conn)
{
	if (conn == NULL)
		return EINVAL;
	return _mrpc_conn_close(conn, 1);
}

static void mrpc_conn_free(struct mrpc_connection *conn)
{
	struct mrpc_message *msg;

	pthread_mutex_lock(&conn->set->conns_lock);
	g_queue_delete_link(conn->set->conns, conn->lh_conns);
	pthread_mutex_unlock(&conn->set->conns_lock);
	destroy_events(conn);
	g_queue_free(conn->events);
	ref_free(conn->running_event_ref);
	ref_free(conn->operations_ref);
	g_hash_table_destroy(conn->pending_replies);
	if (conn->send_msg)
		mrpc_free_message(conn->send_msg);
	if (conn->recv_msg)
		mrpc_free_message(conn->recv_msg);
	while ((msg=g_queue_pop_head(conn->send_msgs)) != NULL)
		mrpc_free_message(msg);
	g_queue_free(conn->send_msgs);
	conn_set_put(conn->set);
	g_slice_free(struct mrpc_connection, conn);
}

void conn_get(struct mrpc_connection *conn)
{
	gint old;

	old=g_atomic_int_exchange_and_add(&conn->refs, 1);
	assert(old > 0);
}

void conn_put(struct mrpc_connection *conn)
{
	if (g_atomic_int_dec_and_test(&conn->refs))
		mrpc_conn_free(conn);
}

static void restart_accept(void *data)
{
	struct mrpc_listener *lnr=data;

	pollset_modify(lnr->set->pollset, lnr->fd, POLLSET_READABLE);
}

static void try_accept(void *data)
{
	struct mrpc_listener *lnr=data;
	struct mrpc_conn_set *set=lnr->set;
	struct mrpc_connection *conn;
	struct mrpc_event *event;
	struct sockaddr_storage sa;
	socklen_t len;
	int fd;

	/* XXX error handling */
	while (1) {
		len=sizeof(sa);
		fd=accept(lnr->fd, (struct sockaddr *)&sa, &len);
		if (fd == -1) {
			if (errno != EAGAIN) {
				pollset_modify(set->pollset, lnr->fd, 0);
				pollset_set_timer(set->pollset, lnr->fd,
							get_config(set,
							accept_backoff));
			}
			break;
		}
		if (mrpc_conn_create(&conn, set, NULL)) {
			close(fd);
			continue;
		}
		if (_mrpc_bind_fd(conn, sa.ss_family, fd)) {
			mrpc_conn_close(conn);
			close(fd);
			continue;
		}
		event=mrpc_alloc_event(conn, EVENT_ACCEPT);
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
		return ENOMEM;
	if (passive)
		hints.ai_flags |= AI_PASSIVE;
	ret=getaddrinfo(host, portstr, &hints, res);
	free(portstr);

	switch (ret) {
	case 0:
		return 0;
	case EAI_ADDRFAMILY:
		return EADDRNOTAVAIL;
	case EAI_AGAIN:
		return EAGAIN;
	case EAI_BADFLAGS:
		return EINVAL;
	case EAI_FAIL:
		return EIO;
	case EAI_FAMILY:
		return EAFNOSUPPORT;
	case EAI_MEMORY:
		return ENOMEM;
	case EAI_NODATA:
		return ENOENT;
	case EAI_NONAME:
		return ENOENT;
	case EAI_SERVICE:
		return ENOENT;
	case EAI_SOCKTYPE:
		return EOPNOTSUPP;
	case EAI_SYSTEM:
		return errno;
	default:
		return EIO;
	}
}

exported int mrpc_connect(struct mrpc_connection *conn, const char *host,
			unsigned port)
{
	struct addrinfo *ai;
	struct addrinfo *cur;
	int fd;
	int ret;

	if (conn == NULL || conn->set->protocol->is_server)
		return EINVAL;
	ret=lookup_addr(&ai, host, port, 0);
	if (ret)
		return ret;
	if (ai == NULL)
		return EIO;
	for (cur=ai; cur != NULL; cur=cur->ai_next) {
		fd=socket(cur->ai_family, cur->ai_socktype, cur->ai_protocol);
		if (fd == -1) {
			/* Don't clobber a connect error with a less-important
			   one */
			if (!ret)
				ret=errno;
			continue;
		}
		if (!connect(fd, cur->ai_addr, cur->ai_addrlen))
			break;
		ret=errno;
		close(fd);
		fd=-1;
	}
	if (fd != -1) {
		ret=_mrpc_bind_fd(conn, cur->ai_family, fd);
		if (ret)
			close(fd);
	}
	freeaddrinfo(ai);
	return ret;
}

exported int mrpc_listen(struct mrpc_conn_set *set, const char *listenaddr,
			unsigned *port)
{
	struct addrinfo *ai;
	struct addrinfo *cur;
	struct mrpc_listener *lnr;
	int fd;
	int count=0;
	int ret;

	/* We require the accept function to exist.  Without it, accepted
	   connections will never have a non-NULL operations pointer and the
	   application will never be aware that they exist, so connecting
	   clients will be forever stuck in PROCEDURE_UNAVAIL limbo. */
	if (set == NULL || port == NULL || !set->protocol->is_server ||
				get_config(set, accept) == NULL)
		return EINVAL;
	ret=lookup_addr(&ai, listenaddr, *port, 1);
	if (ret)
		return ret;
	for (cur=ai; cur != NULL; cur=cur->ai_next) {
		if (cur->ai_family != AF_INET && cur->ai_family != AF_INET6) {
			ret=EPROTONOSUPPORT;
			continue;
		}
		fd=socket(cur->ai_family, cur->ai_socktype, cur->ai_protocol);
		if (fd == -1) {
			/* Don't clobber a more important error with a
			   socket-not-supported error */
			if (!ret)
				ret=errno;
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
			ret=errno;
			close(fd);
			continue;
		}
		if (listen(fd, get_config(set, listen_backlog))) {
			ret=errno;
			close(fd);
			continue;
		}
		if (getsockname(fd, cur->ai_addr, &cur->ai_addrlen)) {
			ret=errno;
			close(fd);
			continue;
		}
		lnr=g_slice_new0(struct mrpc_listener);
		lnr->set=set;
		lnr->fd=fd;
		ret=pollset_add(set->pollset, fd, POLLSET_READABLE, lnr,
					try_accept, NULL, NULL, NULL,
					restart_accept);
		if (ret) {
			close(fd);
			g_slice_free(struct mrpc_listener, lnr);
			continue;
		}
		pthread_mutex_lock(&set->conns_lock);
		g_queue_push_tail(set->listeners, lnr);
		pthread_mutex_unlock(&set->conns_lock);
		count++;
		if (!*port) {
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
	if (count)
		return 0;
	return ret;
}

exported void mrpc_listen_close(struct mrpc_conn_set *set)
{
	struct mrpc_listener *lnr;

	if (set == NULL)
		return;
	conn_set_get(set);
	pthread_mutex_lock(&set->conns_lock);
	while ((lnr=g_queue_pop_head(set->listeners)) != NULL) {
		pthread_mutex_unlock(&set->conns_lock);
		pollset_del(set->pollset, lnr->fd);
		close(lnr->fd);
		g_slice_free(struct mrpc_listener, lnr);
		pthread_mutex_lock(&set->conns_lock);
	}
	pthread_mutex_unlock(&set->conns_lock);
	conn_set_put(set);
}

exported int mrpc_bind_fd(struct mrpc_connection *conn, int fd)
{
	struct sockaddr_storage sa;
	int type;
	socklen_t len;
	int ret;

	if (conn == NULL)
		return EINVAL;

	/* Make sure this is a connected socket */
	len=sizeof(sa);
	if (getpeername(fd, (struct sockaddr *)&sa, &len))
		return errno;

	/* Make sure it's SOCK_STREAM */
	len=sizeof(type);
	if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &len))
		return errno;
	if (type != SOCK_STREAM)
		return EPROTONOSUPPORT;

	ret=_mrpc_bind_fd(conn, sa.ss_family, fd);
	if (ret)
		return ret;
	return 0;
}

exported int mrpc_conn_set_operations(struct mrpc_connection *conn,
			const struct mrpc_protocol *protocol, const void *ops)
{
	refserial_t serial;

	if (conn == NULL || conn->set->protocol != protocol)
		return EINVAL;
	g_atomic_pointer_set(&conn->operations, ops);
	serial=ref_update(conn->operations_ref);
	ref_wait(conn->operations_ref, serial);
	return 0;
}

exported int mrpc_conn_get_counter(struct mrpc_connection *conn,
			enum mrpc_conn_counter counter, uint64_t *result)
{
	if (conn == NULL || result == NULL || (int)counter < 0 ||
				counter >= MRPC_CONNCTR_NR)
		return EINVAL;
	pthread_mutex_lock(&conn->counters_lock);
	*result=conn->counters[counter];
	pthread_mutex_unlock(&conn->counters_lock);
	return 0;
}

static void *listener(void *data)
{
	struct mrpc_conn_set *set=data;

	block_signals();
	while (!selfpipe_is_set(set->shutdown_pipe))
		pollset_poll(set->pollset);
	return NULL;
}

static const struct mrpc_config default_config = {
	.msg_max_buf_len = 16384,
	.listen_backlog = 16,
	.accept_backoff = 1000,
	.keepalive_enabled = 1,
	.keepalive_time = 7200,
	.keepalive_count = 9,
	.keepalive_interval = 75,
};

exported int mrpc_conn_set_create(struct mrpc_conn_set **new_set,
			const struct mrpc_protocol *protocol, void *set_data)
{
	struct mrpc_conn_set *set;
	int ret;

	if (new_set == NULL || protocol == NULL)
		return EINVAL;
	*new_set=NULL;
	set=g_slice_new0(struct mrpc_conn_set);
	pthread_mutex_init(&set->config_lock, NULL);
	pthread_mutex_init(&set->conns_lock, NULL);
	pthread_mutex_init(&set->events_lock, NULL);
	pthread_cond_init(&set->refs_cond, NULL);
	pthread_cond_init(&set->events_threads_cond, NULL);
	set->config=default_config;
	set->protocol=protocol;
	set->refs=1;
	set->conns=g_queue_new();
	set->listeners=g_queue_new();
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
				assert_callback_func, NULL);
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

static void close_elem(void *elem, void *data)
{
	_mrpc_conn_close(elem, 0);
}

exported void mrpc_conn_set_destroy(struct mrpc_conn_set *set)
{
	if (set == NULL)
		return;
	mrpc_listen_close(set);
	pthread_mutex_lock(&set->conns_lock);
	g_queue_foreach(set->conns, close_elem, NULL);
	set->refs--;
	while (set->refs)
		pthread_cond_wait(&set->refs_cond, &set->conns_lock);
	pthread_mutex_unlock(&set->conns_lock);
	selfpipe_set(set->shutdown_pipe);
	selfpipe_set(set->events_notify_pipe);
	pthread_mutex_lock(&set->events_lock);
	while (set->events_threads)
		pthread_cond_wait(&set->events_threads_cond, &set->events_lock);
	pthread_mutex_unlock(&set->events_lock);
	pthread_join(set->thread, NULL);
	pollset_free(set->pollset);
	selfpipe_destroy(set->events_notify_pipe);
	selfpipe_destroy(set->shutdown_pipe);
	g_queue_free(set->conns);
	g_queue_free(set->listeners);
	g_queue_free(set->event_conns);
	g_free(set->trashbuf);
	g_slice_free(struct mrpc_conn_set, set);
}
