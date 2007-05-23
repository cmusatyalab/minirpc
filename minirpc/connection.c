#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <pthread.h>
#define MINIRPC_INTERNAL
#include "internal.h"

#define POLLEVENTS (EPOLLIN|EPOLLERR|EPOLLHUP)

static unsigned conn_hash(struct list_head *entry, unsigned buckets)
{
	struct mrpc_connection *conn=list_entry(entry, struct mrpc_connection,
				lh_conns);
	return conn->fd % buckets;
}

static int conn_match(struct list_head *entry, void *data)
{
	struct mrpc_connection *conn=list_entry(entry, struct mrpc_connection,
				lh_conns);
	int *fd=data;
	return (*fd == conn->fd);
}

static struct mrpc_connection *conn_lookup(struct mrpc_conn_set *set, int fd)
{
	struct list_head *head;
	
	head=hash_get(set->conns, conn_match, fd, &fd);
	if (head == NULL)
		return NULL;
	return list_entry(head, struct mrpc_connection, lh_conns);
}

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

exported int mrpc_conn_add(struct mrpc_connection **new_conn,
			struct mrpc_conn_set *set, int fd, void *data)
{
	struct mrpc_connection *conn;
	struct epoll_event event={0};
	pthread_mutexattr_t attr;
	int ret;
	
	ret=set_nonblock(fd);
	if (ret)
		return ret;
	conn=malloc(sizeof(*conn));
	if (conn == NULL)
		return -ENOMEM;
	memset(conn, 0, sizeof(*conn));
	INIT_LIST_HEAD(&conn->lh_conns);
	INIT_LIST_HEAD(&conn->send_msgs);
	INIT_LIST_HEAD(&conn->lh_event_conns);
	INIT_LIST_HEAD(&conn->events);
	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE_NP);
	pthread_mutex_init(&conn->operations_lock, &attr);
	pthread_mutexattr_destroy(&attr);
	pthread_mutex_init(&conn->send_msgs_lock, NULL);
	pthread_mutex_init(&conn->pending_replies_lock, NULL);
	pthread_mutex_init(&conn->sync_wakeup_lock, NULL);
	pthread_mutex_init(&conn->next_sequence_lock, NULL);
	conn->send_state=STATE_IDLE;
	conn->recv_state=STATE_HEADER;
	conn->set=set;
	conn->fd=fd;
	conn->private = (data != NULL) ? data : conn;
	conn->pending_replies=hash_alloc(set->config.msg_buckets, request_hash);
	if (conn->pending_replies == NULL) {
		free(conn);
		return -ENOMEM;
	}
	event.events=POLLEVENTS;
	event.data.fd=fd;
	if (epoll_ctl(set->epoll_fd, EPOLL_CTL_ADD, fd, &event)) {
		ret=-errno;
		hash_free(conn->pending_replies);
		free(conn);
		return ret;
	}
	pthread_mutex_lock(&set->conns_lock);
	hash_add(set->conns, &conn->lh_conns);
	pthread_mutex_unlock(&set->conns_lock);
	*new_conn=conn;
	return 0;
}

exported void mrpc_conn_remove(struct mrpc_connection *conn)
{
	struct mrpc_conn_set *set=conn->set;
	
	/* XXX data already in buffer? */
	pthread_mutex_lock(&set->conns_lock);
	hash_remove(set->conns, &conn->lh_conns);
	pthread_mutex_unlock(&set->conns_lock);
	hash_free(conn->pending_replies);
	free(conn);
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

static int need_writable(struct mrpc_connection *conn, int writable)
{
	struct epoll_event event={0};
	
	event.data.fd=conn->fd;
	event.events=POLLEVENTS;
	if (writable)
		event.events |= EPOLLOUT;
	return epoll_ctl(conn->set->epoll_fd, EPOLL_CTL_MOD, conn->fd, &event);
}

static void conn_kill(struct mrpc_connection *conn,
			enum mrpc_disc_reason reason)
{
	struct mrpc_event *event;
	
	/* XXX */
	close(conn->fd);
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
	ssize_t count;
	char *buf;
	unsigned len;
	
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
			count=read(conn->fd, buf + conn->recv_offset,
						len - conn->recv_offset);
			if (count == -1 && errno == EINTR) {
				continue;
			} else if (count == -1 && errno == EAGAIN) {
				return;
			} else if (count == 0) {
				conn_kill(conn, MRPC_DISC_CLOSED);
				return;
			} else if (count == -1) {
				conn_kill(conn, MRPC_DISC_IOERR);
				return;
			}
			printf("Read %d bytes\n", count);
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
	if (list_is_empty(&conn->send_msgs)) {
		need_writable(conn, 0);
		pthread_mutex_unlock(&conn->send_msgs_lock);
		return MINIRPC_OK;
	}
	conn->send_msg=list_first_entry(&conn->send_msgs, struct mrpc_message,
				lh_msgs);
	list_del_init(&conn->send_msg->lh_msgs);
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
	ssize_t count;
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
					setsockoptval(conn->fd, SOL_TCP,
								TCP_CORK, 0);
					conn->send_state=STATE_IDLE;
				}
				break;
			}
			if (conn->send_state == STATE_IDLE) {
				setsockoptval(conn->fd, SOL_TCP, TCP_CORK, 1);
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
			count=write(conn->fd, buf + conn->send_offset,
						len - conn->send_offset);
			if (count == 0 || (count == -1 && errno == EAGAIN)) {
				break;
			} else if (count == -1 && errno == EINTR) {
				continue;
			} else if (count == -1) {
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

/* XXX signal handling */
static void *listener(void *data)
{
	struct mrpc_conn_set *set=data;
	struct mrpc_connection *conn;
	struct epoll_event events[set->config.expected_fds];
	int count;
	int i;
	
	while (1) {
		count=epoll_wait(set->epoll_fd, events,
					set->config.expected_fds, -1);
		for (i=0; i<count; i++) {
			if (events[i].data.fd == set->shutdown_pipe[0])
				return NULL;
			
			pthread_mutex_lock(&set->conns_lock);
			conn=conn_lookup(set, events[i].data.fd);
			pthread_mutex_unlock(&set->conns_lock);
			if (conn == NULL) {
				/* XXX */
				continue;
			}
			if (events[i].events & EPOLLERR) {
				conn_kill(conn, MRPC_DISC_IOERR);
				continue;
			}
			if (events[i].events & EPOLLHUP) {
				conn_kill(conn, MRPC_DISC_CLOSED);
				continue;
			}
			if (events[i].events & EPOLLOUT)
				try_write_conn(conn);
			if (events[i].events & EPOLLIN)
				try_read_conn(conn);
		}
	}
}

mrpc_status_t send_message(struct mrpc_message *msg)
{
	struct mrpc_connection *conn=msg->conn;
	
	pthread_mutex_lock(&conn->send_msgs_lock);
	/* XXX extra syscall even when we don't need it */
	if (need_writable(conn, 1)) {
		pthread_mutex_unlock(&conn->send_msgs_lock);
		mrpc_free_message(msg);
		return MINIRPC_NETWORK_FAILURE;
	}
	list_add_tail(&msg->lh_msgs, &conn->send_msgs);
	pthread_mutex_unlock(&conn->send_msgs_lock);
	return MINIRPC_OK;
}

#define copy_default(from, to, field, default) do { \
		to->field=from->field ? from->field : default; \
	} while (0)
static void validate_copy_config(const struct mrpc_config *from,
			struct mrpc_config *to)
{
	copy_default(from, to, protocol, from->protocol);
	copy_default(from, to, expected_fds, 16);
	copy_default(from, to, conn_buckets, 16);
	copy_default(from, to, msg_buckets, 16);
	copy_default(from, to, msg_max_buf_len, 16000);
}
#undef copy_default

exported int mrpc_conn_set_alloc(const struct mrpc_config *config,
			const struct mrpc_set_operations *ops,
			void *set_data,	struct mrpc_conn_set **new_set)
{
	struct mrpc_conn_set *set;
	struct epoll_event event={0};
	int ret=-ENOMEM;
	
	if (config == NULL || ops == NULL || new_set == NULL)
		return -EINVAL;
	set=malloc(sizeof(*set));
	if (set == NULL)
		goto bad_alloc;
	memset(set, 0, sizeof(*set));
	validate_copy_config(config, &set->config);
	pthread_mutex_init(&set->conns_lock, NULL);
	pthread_mutex_init(&set->events_lock, NULL);
	pthread_cond_init(&set->events_threads_cond, NULL);
	INIT_LIST_HEAD(&set->event_conns);
	set->ops=ops;
	set->private = (set_data != NULL) ? set_data : set;
	set->conns=hash_alloc(set->config.conn_buckets, conn_hash);
	if (set->conns == NULL)
		goto bad_conns;
	if (pipe(set->shutdown_pipe)) {
		ret=-errno;
		goto bad_shutdown_pipe;
	}
	if (pipe(set->events_notify_pipe)) {
		ret=-errno;
		goto bad_events_pipe;
	}
	if (set_nonblock(set->events_notify_pipe[0]))
		goto bad_nonblock;
	if (set_nonblock(set->events_notify_pipe[1]))
		goto bad_nonblock;
	set->epoll_fd=epoll_create(set->config.expected_fds);
	if (set->epoll_fd < 0) {
		ret=-errno;
		goto bad_epoll;
	}
	event.events=EPOLLIN;
	event.data.fd=set->shutdown_pipe[0];
	if (epoll_ctl(set->epoll_fd, EPOLL_CTL_ADD, set->shutdown_pipe[0],
				&event)) {
		ret=-errno;
		goto bad_epoll_pipe;
	}
	ret=pthread_create(&set->thread, NULL, listener, set);
	if (ret) {
		ret=-ret;
		goto bad_pthread;
	}
	*new_set=set;
	return 0;

bad_pthread:
bad_epoll_pipe:
	close(set->epoll_fd);
bad_epoll:
bad_nonblock:
	close(set->events_notify_pipe[0]);
	close(set->events_notify_pipe[1]);
bad_events_pipe:
	close(set->shutdown_pipe[0]);
	close(set->shutdown_pipe[1]);
bad_shutdown_pipe:
	hash_free(set->conns);
bad_conns:
	free(set);
bad_alloc:
	return ret;
}

/* XXX drops lots of stuff on the floor */
exported void mrpc_conn_set_free(struct mrpc_conn_set *set)
{
	write(set->shutdown_pipe[1], "s", 1);
	pthread_mutex_lock(&set->events_lock);
	while (set->events_threads)
		pthread_cond_wait(&set->events_threads_cond, &set->events_lock);
	pthread_mutex_unlock(&set->events_lock);
	pthread_join(set->thread, NULL);
	close(set->epoll_fd);
	hash_free(set->conns);
	free(set);
}
