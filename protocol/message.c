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
			pthread_cond_t cond;
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

static int pending_alloc(struct mrpc_message *request,
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

static int send_request_pending(struct mrpc_message *request,
			struct pending_reply *pending)
{
	struct mrpc_connection *conn=request->conn;
	int ret;
	
	pthread_mutex_lock(&conn->pending_replies_lock);
	hash_add(&conn->pending_replies, &pending->lh_pending);
	pthread_mutex_unlock(&conn->pending_replies_lock);
	ret=send_message(conn, request);
	if (ret) {
		pthread_mutex_lock(&conn->pending_replies_lock);
		hash_remove(conn->pending_replies, &pending->lh_pending);
		pthread_mutex_unlock(&conn->pending_replies_lock);
		free(pending);
	}
	return ret;
}

int mrpc_send_request(struct mrpc_connection *conn, unsigned cmd, void *in,
			void **out)
{
	struct mrpc_message *request;
	struct mrpc_message *reply=NULL;
	struct pending_reply *pending;
	int ret;
	
	ret=format_request(conn, cmd, in, &request);
	if (ret)
		return ret;
	ret=pending_alloc(request, &pending);
	if (ret) {
		mrpc_free_message(request);
		return ret;
	}
	pending->async=0;
	pthread_cond_init(&pending->data.sync.cond, NULL);
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
	return MINIRPC_OK;
}

int mrpc_send_request_async(struct mrpc_connection *conn, unsigned cmd,
			void *in, reply_callback_fn *callback, void *private)
{
	struct mrpc_message *msg;
	struct pending_reply *pending;
	int ret;
	
	if (callback == NULL)
		return MINIRPC_INVALID_ARGUMENT;
	ret=format_request(conn, cmd, in, &msg);
	if (ret)
		return ret;
	ret=pending_alloc(request, &pending);
	if (ret) {
		mrpc_free_message(msg);
		return ret;
	}
	pending->async=1;
	pending->data.async.callback=callback;
	pending->data.async.private=private;
	return send_request_pending(msg, pending);
}

int mrpc_send_request_noreply(struct mrpc_connection *conn, unsigned cmd,
			void *in)
{
	struct mrpc_message *msg;
	int ret;
	
	ret=format_request(conn, cmd, in, &msg);
	if (ret)
		return ret;
	return send_message(msg);
}

int mrpc_send_reply(struct mrpc_message *request, int status, void *data)
{
	struct mrpc_message *reply;
	
	if (status == MINIRPC_REQUEST)
		return MINIRPC_INVALID_ARGUMENT;
	if (status == MINIRPC_DEFER)
		return MINIRPC_DEFER;
	if (status) {
		ret=format_reply_error(request, status, &reply);
	} else {
		ret=format_reply(request, data, &reply);
	}
	mrpc_free_message(request);
	if (ret)
		return ret;
	return send_message(reply);
}

static void queue_event(struct mrpc_message *msg)
{
	pthread_mutex_lock(&set->callback_queue_lock);
	list_add_tail(&msg->lh_msgs, &set->callback_queue);
	pthread_cond_signal(&set->callback_queue_cond);
	pthread_mutex_unlock(&set->callback_queue_lock);
}

/* XXX what happens if we get a bad reply?  close the connection? */
void process_incoming_message(struct mrpc_connection *conn)
{
	struct mrpc_message *msg=conn->recv_msg;
	struct pending_reply *pending;
	
	if (msg->hdr.status == MINIRPC_REQUEST) {
		queue_event(msg);
	} else {
		pthread_mutex_lock(&conn->pending_replies_lock);
		pending=request_lookup(conn, msg->sequence);
		if (pending != NULL)
			hash_remove(conn->pending_replies,
						&pending->lh_pending);
		pthread_mutex_unlock(&conn->pending_replies_lock);
		if (pending == NULL || pending->cmd != msg->hdr.cmd ||
					(pending->status != 0 &&
					pending->datalen != 0)) {
			/* XXX what is this thing we received? */
			mrpc_free_message(msg);
			if (pending != NULL) {
				mrpc_free_message(pending->request);
				free(pending);
			}
			return;
		}
		if (pending->async) {
			msg->callback=pending->data.async.callback;
			msg->private=pending->data.async.private;
			queue_event(msg);
		} else {
			pthread_mutex_lock(&conn->sync_wakeup_lock);
			*pending->data.sync.reply=msg;
			pthread_cond_signal(pending->data.sync.cond);
			pthread_mutex_unlock(&conn->sync_wakeup_lock);
		}
		free(pending);
	}
}

static void dispatch_request(struct mrpc_message *request)
{
	struct mrpc_connection *conn=request->conn;
	struct mrpc_message *reply;
	void *request_data;
	void *reply_data=NULL;
	int ret;
	int result;
	xdrproc_t request_type;
	xdrproc_t reply_type;
	unsigned request_size;
	unsigned reply_size;
	int doreply=0;
	
	BUG_ON(request->hdr.status != MINIRPC_REQUEST);
	
	if (conn->set->protocol->request_info(request->hdr.cmd, &request_type,
				&request_size)) {
		mrpc_free_message(request);
		return;
	}
	if (conn->set->protocol->reply_info(request->hdr.cmd, &reply_type,
				&reply_size) == MINIRPC_OK)
		doreply=1;
	
	request_data=malloc(request_size);
	if (request_data == NULL) {
		/* XXX */
	}
	if (doreply) {
		reply_data=malloc(reply_size);
		if (reply_data == NULL) {
			free(request_data);
			/* XXX */
		}
	}
	ret=unserialize(request_type, request->data, request->hdr.datalen,
				request_data, request_size);
	
	pthread_rwlock_rdlock(&conn->operations_lock);
	result=conn->set->protocol->request(conn, request->hdr.cmd,
				request_data, reply_data);
	pthread_rwlock_unlock(&conn->operations_lock);
	free(request_data);
	
	if (doreply) {
		if (result == MINIRPC_DEFER) {
			free(reply_data);
			/* This struct is going to stay around for a while,
			   but we won't need the serialized request data
			   anymore.  Free up some memory. */
			free(request->data);
			request->data=NULL;
		}
		ret=mrpc_send_reply(conn, request, result, reply_data);
		free(reply_data);
		if (ret)
			XXX;
	} else {
		mrpc_free_message(request);
	}
}

static void run_reply_callback(struct mrpc_message *reply)
{
	void *out=NULL;
	int ret;
	
	ret=reply->hdr.status;
	if (!ret)
		ret=unformat_request(msg, &out);
	reply->callback(reply->conn->private, reply->private, ret, out);
	mrpc_free_message(reply);
}

static void dispatch_event(struct mrpc_message *msg)
{
	if (msg->callback) {
		run_reply_callback(msg);
	} else if (msg->hdr.status == MINIRPC_REQUEST) {
		dispatch_request(msg);
	} else {
		BUG();
	}
}

void mrpc_dispatch_loop(struct mrpc_conn_set *set)
{
	struct mrpc_message *msg;
	
	pthread_mutex_lock(&set->event_queue_lock);
	set->event_queue_threads++
	while (!set->dying) {
		if (list_is_empty(&set->event_queue)) {
			pthread_cond_wait(&set->event_queue_cond,
						&set->event_queue_lock);
			continue;
		}
		msg=list_first_entry(&set->event_queue, struct mrpc_message,
					lh_msgs);
		list_del_init(&msg->lh_msgs);
		pthread_mutex_unlock(&set->event_queue_lock);
		dispatch_event(msg);
		pthread_mutex_lock(&set->event_queue_lock);
	}
	set->event_queue_threads--;
	pthread_cond_broadcast(&set->event_queue_cond);
	pthread_mutex_unlock(&set->event_queue_lock);
}

int mrpc_dispatch_nonblock(struct mrpc_conn_set *set)
{
	struct mrpc_message *msg=NULL;
	
	pthread_mutex_lock(&set->event_queue_lock);
	if (!list_is_empty(&set->event_queue)) {
		msg=list_first_entry(&set->event_queue, struct mrpc_message,
					lh_msgs);
		list_del_init(&msg->lh_msgs);
	}
	pthread_mutex_unlock(&set->event_queue_lock);
	if (msg != NULL)
		dispatch_event(msg);
	return (msg != NULL);
}
