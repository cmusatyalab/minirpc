#include <sys/poll.h>
#include <unistd.h>
#include <assert.h>
#include <pthread.h>
#define MINIRPC_INTERNAL
#include "internal.h"

struct pending_reply {
	struct list_head lh_pending;
	unsigned sequence;
	unsigned cmd;
	int async;
	union {
		struct {
			pthread_cond_t *cond;
			struct mrpc_message **reply;
		} sync;
		struct {
			reply_callback_fn *callback;
			void *private;
		} async;
	} data;
};

/* XXX It would be nice to be able to make this static */
unsigned request_hash(struct list_head *head, unsigned buckets)
{
	struct pending_reply *pending=list_entry(head, struct pending_reply,
				lh_pending);
	return pending->sequence % buckets;
}

static int request_match(struct list_head *head, void *data)
{
	struct pending_reply *pending=list_entry(head, struct pending_reply,
				lh_pending);
	int *sequence=data;
	return (*sequence == pending->sequence);
}

static struct pending_reply *request_lookup(struct mrpc_connection *conn,
			int sequence)
{
	struct list_head *head;
	
	head=hash_get(conn->pending_replies, request_match, sequence,
				&sequence);
	if (head == NULL)
		return NULL;
	return list_entry(head, struct pending_reply, lh_pending);
}

static mrpc_status_t pending_alloc(struct mrpc_message *request,
			struct pending_reply **pending_reply)
{
	struct pending_reply *pending;
	
	pending=malloc(sizeof(*pending));
	if (pending == NULL)
		return MINIRPC_NOMEM;
	INIT_LIST_HEAD(&pending->lh_pending);
	pending->sequence=request->hdr.sequence;
	pending->cmd=request->hdr.cmd;
	*pending_reply=pending;
	return MINIRPC_OK;
}

static mrpc_status_t send_request_pending(struct mrpc_message *request,
			struct pending_reply *pending)
{
	struct mrpc_connection *conn=request->conn;
	mrpc_status_t ret;
	
	pthread_mutex_lock(&conn->pending_replies_lock);
	hash_add(conn->pending_replies, &pending->lh_pending);
	pthread_mutex_unlock(&conn->pending_replies_lock);
	ret=send_message(request);
	if (ret) {
		pthread_mutex_lock(&conn->pending_replies_lock);
		hash_remove(conn->pending_replies, &pending->lh_pending);
		pthread_mutex_unlock(&conn->pending_replies_lock);
		free(pending);
	}
	return ret;
}

mrpc_status_t mrpc_send_request(const struct mrpc_protocol *protocol,
			struct mrpc_connection *conn, int cmd, void *in,
			void **out)
{
	struct mrpc_message *request;
	struct mrpc_message *reply=NULL;
	struct pending_reply *pending;
	pthread_cond_t cond=PTHREAD_COND_INITIALIZER;
	mrpc_status_t ret;
	
	if (protocol != conn->set->protocol)
		return MINIRPC_INVALID_PROTOCOL;
	if (cmd <= 0)
		return MINIRPC_INVALID_ARGUMENT;
	ret=format_request(conn, cmd, in, &request);
	if (ret)
		return ret;
	ret=pending_alloc(request, &pending);
	if (ret) {
		mrpc_free_message(request);
		return ret;
	}
	pending->async=0;
	pending->data.sync.cond=&cond;
	pending->data.sync.reply=&reply;
	ret=send_request_pending(request, pending);
	if (ret)
		return ret;
	
	pthread_mutex_lock(&conn->sync_wakeup_lock);
	while (reply == NULL)
		pthread_cond_wait(&cond, &conn->sync_wakeup_lock);
	pthread_mutex_unlock(&conn->sync_wakeup_lock);
	ret=reply->hdr.status;
	if (!ret)
		ret=unformat_reply(reply, out);
	mrpc_free_message(reply);
	return ret;
}

mrpc_status_t mrpc_send_request_async(const struct mrpc_protocol *protocol,
			struct mrpc_connection *conn, int cmd,
			reply_callback_fn *callback, void *private, void *in)
{
	struct mrpc_message *msg;
	struct pending_reply *pending;
	mrpc_status_t ret;
	
	if (protocol != conn->set->protocol)
		return MINIRPC_INVALID_PROTOCOL;
	if (callback == NULL || cmd <= 0)
		return MINIRPC_INVALID_ARGUMENT;
	ret=format_request(conn, cmd, in, &msg);
	if (ret)
		return ret;
	ret=pending_alloc(msg, &pending);
	if (ret) {
		mrpc_free_message(msg);
		return ret;
	}
	pending->async=1;
	pending->data.async.callback=callback;
	pending->data.async.private=private;
	return send_request_pending(msg, pending);
}

mrpc_status_t mrpc_send_request_noreply(const struct mrpc_protocol *protocol,
			struct mrpc_connection *conn, int cmd, void *in)
{
	struct mrpc_message *msg;
	mrpc_status_t ret;
	
	if (protocol != conn->set->protocol)
		return MINIRPC_INVALID_PROTOCOL;
	if (cmd >= 0)
		return MINIRPC_INVALID_ARGUMENT;
	ret=format_request(conn, cmd, in, &msg);
	if (ret)
		return ret;
	return send_message(msg);
}

mrpc_status_t mrpc_send_reply(const struct mrpc_protocol *protocol,
			struct mrpc_message *request, void *data)
{
	struct mrpc_message *reply;
	mrpc_status_t ret;
	
	if (protocol != request->conn->set->protocol)
		return MINIRPC_INVALID_PROTOCOL;
	ret=format_reply(request, data, &reply);
	if (ret)
		return ret;
	ret=send_message(reply);
	if (ret)
		return ret;
	mrpc_free_message(request);
	return MINIRPC_OK;
}

mrpc_status_t mrpc_send_reply_error(const struct mrpc_protocol *protocol,
			struct mrpc_message *request, mrpc_status_t status)
{
	struct mrpc_message *reply;
	mrpc_status_t ret;
	
	if (protocol != request->conn->set->protocol)
		return MINIRPC_INVALID_PROTOCOL;
	if (status == MINIRPC_OK || status == MINIRPC_PENDING)
		return MINIRPC_INVALID_ARGUMENT;
	ret=format_reply_error(request, status, &reply);
	if (ret)
		return ret;
	ret=send_message(reply);
	if (ret)
		return ret;
	mrpc_free_message(request);
	return MINIRPC_OK;
}

static void queue_event(struct mrpc_message *msg)
{
	struct mrpc_conn_set *set=msg->conn->set;
	
	pthread_mutex_lock(&set->events_lock);
	if (list_is_empty(&set->events))
		write(set->events_notify_pipe[1], "a", 1);
	list_add_tail(&msg->lh_msgs, &set->events);
	pthread_mutex_unlock(&set->events_lock);
}

static int empty_pipe(int fd)
{
	char buf[8];
	int count;
	ssize_t ret;
	
	for (count=0; (ret=read(fd, buf, sizeof(buf))) >= 0; count += ret);
	return count;
}

static struct mrpc_message *unqueue_event(struct mrpc_conn_set *set)
{
	struct mrpc_message *msg=NULL;
	
	pthread_mutex_lock(&set->events_lock);
	if (list_is_empty(&set->events)) {
		if (empty_pipe(set->events_notify_pipe[0]))
			/* XXX */;
	} else {
		msg=list_first_entry(&set->events, struct mrpc_message,
					lh_msgs);
		list_del_init(&msg->lh_msgs);
		if (list_is_empty(&set->events))
			if (empty_pipe(set->events_notify_pipe[0]) != 1)
				/* XXX */;
	}
	pthread_mutex_unlock(&set->events_lock);
	return msg;
}

/* Return a copy of the notify fd.  This ensures that the application can never
   inadvertently cause SIGPIPE by closing the read end of the notify pipe, and
   also ensures that we don't close the fd out from under the application when
   the connection set is destroyed.  The application must close the fd when
   done with it.  The application must not read or write the fd, only select()
   on it.  When an event is ready to be processed, the fd will be readable. */
int mrpc_get_event_fd(struct mrpc_conn_set *set)
{
	return dup(set->events_notify_pipe[0]);
}

/* XXX what happens if we get a bad reply?  close the connection? */
void process_incoming_message(struct mrpc_message *msg)
{
	struct mrpc_connection *conn=msg->conn;
	struct pending_reply *pending;
	
	if (msg->hdr.status == MINIRPC_PENDING) {
		queue_event(msg);
	} else {
		pthread_mutex_lock(&conn->pending_replies_lock);
		pending=request_lookup(conn, msg->hdr.sequence);
		if (pending != NULL)
			hash_remove(conn->pending_replies,
						&pending->lh_pending);
		pthread_mutex_unlock(&conn->pending_replies_lock);
		if (pending == NULL || pending->cmd != msg->hdr.cmd ||
					(msg->hdr.status != 0 &&
					msg->hdr.datalen != 0)) {
			/* XXX what is this thing we received? */
			mrpc_free_message(msg);
			if (pending != NULL)
				free(pending);
			return;
		}
		if (pending->async) {
			msg->callback=pending->data.async.callback;
			msg->private=pending->data.async.private;
			queue_event(msg);
		} else {
			pthread_mutex_lock(&conn->sync_wakeup_lock);
			*pending->data.sync.reply=msg;
			pthread_mutex_unlock(&conn->sync_wakeup_lock);
			pthread_cond_signal(pending->data.sync.cond);
		}
		free(pending);
	}
}

static void fail_request(struct mrpc_message *request, mrpc_status_t err)
{
	if (request->hdr.cmd >= 0) {
		if (mrpc_send_reply_error(request->conn->set->protocol,
					request, err))
			mrpc_free_message(request);
	} else {
		mrpc_free_message(request);
	}
}

/* XXX notifications to application */
static void dispatch_request(struct mrpc_message *request)
{
	struct mrpc_connection *conn=request->conn;
	void *request_data;
	void *reply_data=NULL;
	mrpc_status_t ret;
	mrpc_status_t result=MINIRPC_PROCEDURE_UNAVAIL;
	xdrproc_t request_type;
	xdrproc_t reply_type;
	unsigned reply_size;
	int doreply;
	
	assert(request->hdr.status == MINIRPC_PENDING);
	
	if (conn->set->protocol->receiver_request_info(request->hdr.cmd,
				&request_type, NULL)) {
		/* Unknown opcode */
		fail_request(request, MINIRPC_PROCEDURE_UNAVAIL);
		return;
	}
	
	doreply=(request->hdr.cmd >= 0);
	if (doreply) {
		if (conn->set->protocol->receiver_reply_info(request->hdr.cmd,
					&reply_type, &reply_size)) {
			/* Can't happen if the info tables are well-formed */
			fail_request(request, MINIRPC_ENCODING_ERR);
			return;
		}
		
		if (reply_size) {
			reply_data=malloc(reply_size);
			if (reply_data == NULL) {
				fail_request(request, MINIRPC_NOMEM);
				return;
			}
			memset(reply_data, 0, reply_size);
		}
	}
	ret=unformat_request(request, &request_data);
	if (ret) {
		/* Invalid datalen, etc. */
		fail_request(request, ret);
		cond_free(reply_data);
		return;
	}
	
	pthread_rwlock_rdlock(&conn->operations_lock);
	if (conn->set->protocol->request != NULL)
		result=conn->set->protocol->request(conn->operations,
					conn->private, request,
					request->hdr.cmd, request_data,
					reply_data);
	pthread_rwlock_unlock(&conn->operations_lock);
	xdr_free(request_type, request_data);
	cond_free(request_data);
	
	if (doreply) {
		if (result == MINIRPC_PENDING) {
			xdr_free(reply_type, reply_data);
			cond_free(reply_data);
			/* This struct is going to stay around for a while,
			   but we won't need the serialized request data
			   anymore.  Free up some memory. */
			cond_free(request->data);
			request->data=NULL;
			return;
		}
		if (result)
			ret=mrpc_send_reply_error(conn->set->protocol, request,
						result);
		else
			ret=mrpc_send_reply(conn->set->protocol, request,
						reply_data);
		xdr_free(reply_type, reply_data);
		cond_free(reply_data);
		if (ret) {
			/* XXX reply failed! */
			mrpc_free_message(request);
		}
	} else {
		mrpc_free_message(request);
	}
}

static void run_reply_callback(struct mrpc_message *reply)
{
	void *out=NULL;
	long_reply_callback_fn *longfn = reply->callback;
	short_reply_callback_fn *shortfn = reply->callback;
	unsigned size;
	mrpc_status_t ret;
	
	ret=reply->conn->set->protocol->sender_reply_info(reply->hdr.cmd,
				NULL, &size);
	if (ret) {
		/* XXX */
		mrpc_free_message(reply);
		return;
	}
	ret=reply->hdr.status;
	if (!ret)
		ret=unformat_reply(reply, &out);
	/* On x86, we could unconditionally call the four-argument form, even
	   if the function we're calling only expects three arguments, since
	   the extra argument would merely languish on the stack.  But I don't
	   want to make assumptions about the calling convention of the machine
	   architecture.  This should ensure that a function expecting three
	   arguments gets three, and a function expecting four arguments gets
	   four. */
	if (size)
		longfn(reply->conn->private, reply->private, ret, out);
	else
		shortfn(reply->conn->private, reply->private, ret);
	mrpc_free_message(reply);
}

static void dispatch_event(struct mrpc_message *msg)
{
	if (msg->callback) {
		run_reply_callback(msg);
	} else if (msg->hdr.status == MINIRPC_PENDING) {
		dispatch_request(msg);
	} else {
		assert(0);
	}
}

int mrpc_dispatch_one(struct mrpc_conn_set *set)
{
	struct mrpc_message *msg;
	
	msg=unqueue_event(set);
	if (msg != NULL)
		dispatch_event(msg);
	return (msg != NULL);
}

int mrpc_dispatch_all(struct mrpc_conn_set *set)
{
	int i;
	
	for (i=0; mrpc_dispatch_one(set); i++);
	return i;
}

int mrpc_dispatch_loop(struct mrpc_conn_set *set)
{
	struct mrpc_message *msg;
	struct pollfd poll_s[2] = {{0}};
	int ret=0;
	
	pthread_mutex_lock(&set->events_lock);
	set->events_threads++;
	pthread_cond_broadcast(&set->events_threads_cond);
	pthread_mutex_unlock(&set->events_lock);
	
	poll_s[0].fd=set->events_notify_pipe[0];
	poll_s[0].events=POLLIN;
	poll_s[1].fd=set->shutdown_pipe[0];
	poll_s[1].events=POLLIN;
	
	while (poll_s[1].revents == 0) {
		msg=unqueue_event(set);
		if (msg == NULL) {
			if (poll(poll_s, 2, -1) == -1 && errno != EINTR) {
				ret=-errno;
				break;
			}
		} else {
			dispatch_event(msg);
		}
	}
	
	pthread_mutex_lock(&set->events_lock);
	set->events_threads--;
	pthread_cond_broadcast(&set->events_threads_cond);
	pthread_mutex_unlock(&set->events_lock);
	return ret;
}
