#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include "protocol.h"
#include "list.h"
#include "errno.h"

static int epoll_fd;

static struct {
	pthread_mutex_t lock;
	struct htable *table;
	unsigned buflen;
} conn_set;

struct connection {
	struct list_head *lh_hash;
	int fd;
	char *send_buf;
	unsigned send_offset;
	unsigned send_length;
	struct list_head *send_msgs;
	char *recv_buf;
	unsigned recv_offset;
	struct ISRMessage *recv_msg;
};

struct message {
	struct list_head *lh_msgs;
	struct ISRMessage *msg;	
};

static unsigned conn_hash(struct list_head *entry, unsigned buckets)
{
	struct connection *conn=list_entry(entry, struct connection, lh_hash);
	return conn->fd % buckets;
}

static int conn_match(struct list_head *entry, void *data)
{
	struct connection *conn=list_entry(entry, struct connection, lh_hash);
	int fd=(int)data;
	return (fd == conn->fd);
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

int add_fd(int fd)
{
	struct connection *conn;
	struct epoll_event event;
	int ret;
	
	ret=set_nonblock(fd);
	if (ret)
		return ret;
	conn=malloc(sizeof(*conn));
	if (conn == NULL)
		return -ENOMEM;
	memset(conn, 0, sizeof(*conn));
	INIT_LIST_HEAD(&conn->lh_hash);
	INIT_LIST_HEAD(&conn->send_msgs);
	conn->fd=fd;
	conn->send_buf=malloc(conn_set.buflen);
	if (conn->send_buf == NULL) {
		free(conn);
		return -ENOMEM;
	}
	conn->recv_buf=malloc(conn_set.buflen);
	if (conn->recv_buf == NULL) {
		free(conn->send_buf);
		free(conn);
		return -ENOMEM;
	}
	event->events=EPOLLIN|EPOLLERR|EPOLLHUP;
	event->data.ptr=conn;
	if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event)) {
		ret=-errno;
		free(conn->recv_buf);
		free(conn->send_buf);
		free(con);
		return ret;
	}
	pthread_mutex_lock(&conn_set->lock);
	hash_add(conn_set->table, &conn->lh_hash);
	pthread_mutex_unlock(&conn_set->lock);
	return 0;
}

int remove_fd(int fd)
{
	struct connection *conn;
	
	/* XXX data already in buffer? */
	pthread_mutex_lock(&conn_set->lock);
	conn=hash_get(conn_set.table, conn_match, fd, (void*)fd);
	if (conn == NULL) {
		pthread_mutex_unlock(&conn_set->lock);
		return -EINVAL;
	}
	hash_remove(conn_set.table, &conn->lh_hash);
	pthread_mutex_unlock(&conn_set->lock);
	free(conn->recv_buf);
	free(conn->send_buf);
	free(conn);
	return 0;
}

static void conn_kill(struct connection *conn)
{
	/* XXX */
}

static int process_buffer(struct connection *conn, unsigned *start)
{
	asn_dec_rval_t rval;
	int ret=0;
	
	rval=ber_decode(NULL, &asn_DEF_ISRMessage, &conn->recv_msg,
				buf + *start, len);
	switch (rval.code) {
	case RC_OK:
		if (asn_check_constraints(&asn_DEF_ISRMessage, conn->recv_msg,
					NULL, NULL)) {
			ASN_STRUCT_FREE(&asn_DEF_ISRMessage, conn->recv_msg);
			conn->recv_msg=NULL;
			ret=-EINVAL;
			break;
		}
		process_incoming_message(conn->recv_msg);
		conn->recv_msg=NULL;
		break;
	case RC_WMORE:
		ret=-EAGAIN;
		break;
	case RC_FAIL:
		ASN_STRUCT_FREE(&asn_DEF_ISRMessage, conn->recv_msg);
		conn->recv_msg=NULL;
		ret=-EINVAL;
		break;
	}
	*start += rval.consumed;
	return ret;
}

static void try_read_conn(struct connection *conn)
{
	ssize_t count;
	unsigned start;
	int ret;
	
	while (1) {
		count=read(conn->fd, conn->recv_buf + conn->recv_offset,
					conn_set.buflen - conn->recv_offset);
		if (count == -1 && errno == EINTR) {
			continue;
		} else if (count == -1 && errno == EAGAIN) {
			return;
		} else if (count == 0 || count == -1) {
			conn_kill(conn);
			return;
		}
		conn->recv_offset += count;
		
		start=0;
		while (1) {
			ret=process_buffer(conn, &start);
			if (ret == -EINVAL) {
				conn_kill(conn);
				return;
			}
			if (ret == -EAGAIN) {
				memmove(conn->recv_buf, conn->recv_buf + start,
						conn->recv_offset - start);
				conn->recv_offset -= start;
				break;
			}
		}
		
		if (conn->recv_offset == conn_set.buflen) {
			conn_kill(conn);
			break;
		}
	}
}

static int form_buffer(struct connection *conn)
{
	asn_enc_rval_t rval;
	struct message msg;
	
	/* XXX race with queueing new messages? */
	if (list_is_empty(&conn->send_msgs))
		return -EAGAIN;
	msg=list_entry(conn->send_msgs.next, struct message, lh_msgs);
	list_del_init(&msg->lh_msgs);
	
	rval=der_encode_to_buffer(&asn_DEF_ISRMessage, msg->msg,
				conn->recv_buf, conn_set.buflen);
	if (rval.encoded == -1)
		return -EINVAL;
	conn->send_offset=0;
	conn->send_length=rval.encoded;
	return 0;
}

/* XXX need to enable and disable writable bit in epoll depending on whether
   we have anything to write or not */
static void try_write_conn(struct connection *conn)
{
	ssize_t count;
	int ret;
	
	while (1) {
		if (conn->send_offset == conn->send_length) {
			ret=form_buffer(conn);
			if (ret == -EAGAIN)
				break;
			else if (ret == -EINVAL) {
				conn_kill(conn);
				break;
			}
		}
		
		count=write(conn->fd, conn->send_buf + conn->send_offset,
					conn->send_length - conn->send_offset);
		if (count == 0 || (count == -1 && errno == EAGAIN)) {
			break;
		} else if (count == -1 && errno == EINTR) {
			continue;
		} else if (count == -1) {
			conn_kill(conn);
			break;
		}
		conn->send_offset += count;
	}
}

void listener(int maxevents)
{
	struct epoll_events events[maxevents];
	int count;
	int i;
	
	while (1) {
		count=epoll_wait(epoll_fd, &events, maxevents, -1);
		for (i=0; i<count; i++) {
			if (events[i].events & (EPOLLERR | EPOLLHUP)) {
				conn_kill(events[i].data.ptr);
				continue;
			}
			if (events[i].events & EPOLLOUT)
				try_write_conn(events[i].data.ptr);
			if (events[i].events & EPOLLIN)
				try_read_conn(events[i].data.ptr);
		}
	}
}

int messenger_init(int fds, unsigned buckets, unsigned buflen)
{
	epoll_fd=epoll_create(fds);
	if (epoll_fd < 0)
		return -errno;
	pthread_mutex_init(&conn_set.lock, NULL);
	conn_set.table=hash_alloc(buckets, conn_hash);
	if (conn_set.table == NULL)
		return -ENOMEM;
	conn_set.buflen=buflen;
	return 0;
}
