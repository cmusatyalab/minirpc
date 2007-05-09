#include <pthread.h>
#define LIBPROTOCOL
#include "internal.h"

struct pending_reply {
	struct list_head lh_pending;
	unsigned sequence;
	unsigned cmd;
	int async;
	union {
		struct {
			pthread_cond_t cond;
			struct minirpc_message **reply;
		} sync;
		struct {
			reply_callback_fn *callback;
			void *private;
		} async;
	} data;
};

/* XXX deal with sequence number wraparound */

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

static struct pending_reply *request_lookup(struct minirpc_connection *conn,
			int sequence)
{
	struct list_head *head;
	
	head=hash_get(conn->pending_replies, request_match, sequence,
				&sequence);
	if (head == NULL)
		return NULL;
	return list_entry(head, struct pending_reply, lh_pending);
}

static int send_request_setup(struct minirpc_connection *conn,
			struct minirpc_message *request,
			struct pending_reply **pending_reply)
{
	struct pending_reply *pending;
	
	if (pending_reply != NULL) {
		pending=malloc(sizeof(*pending));
		if (pending == NULL)
			return MINIRPC_NOMEM;
		INIT_LIST_HEAD(&pending->lh_pending);
		pending->sequence=request->hdr.sequence;
		pending->cmd=request->hdr.cmd;
		*pending_reply=pending;
	}
	return MINIRPC_OK;
}

static int send_request_pending(struct minirpc_connection *conn,
			struct minirpc_message *request,
			struct pending_reply *pending)
{
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

/* msg is an inout parameter */
int minirpc_send_request(struct minirpc_connection *conn,
			struct minirpc_message **msg)
{
	struct pending_reply *pending;
	struct minirpc_message *reply=NULL;
	int ret;
	
	ret=send_request_setup(conn, *msg, &pending);
	if (ret)
		return ret;
	pending->async=0;
	pthread_cond_init(&pending->data.sync.cond, NULL);
	pending->data.sync.reply=&reply;
	ret=send_request_pending(conn, request, pending);
	if (ret)
		return ret;
	pthread_mutex_lock(&conn->sync_wakeup_lock);
	while (reply == NULL)
		pthread_cond_wait(&cond, &conn->sync_wakeup_lock);
	pthread_mutex_unlock(&conn->sync_wakeup_lock);
	*msg=reply;
	return 0;
}

int minirpc_send_request_async(struct minirpc_connection *conn,
			struct minirpc_message *request,
			reply_callback_fn *callback, void *private)
{
	struct pending_reply *pending;
	int ret;
	
	ret=send_request_setup(conn, request, &pending);
	if (ret)
		return ret;
	pending->async=1;
	pending->data.async.callback=callback;
	pending->data.async.private=private;
	return send_request_pending(conn, request, pending);
}

int minirpc_send_request_noreply(struct minirpc_connection *conn,
			struct minirpc_message *request)
{
	int ret;
	
	ret=send_request_setup(conn, request, NULL);
	if (ret)
		return ret;
	return send_message(conn, request);
}

int minirpc_send_reply(struct minirpc_connection *conn,
			struct minirpc_message *reply)
{
	int ret;
	
	reply->sequence=request->sequence;
	reply->isReply=1;
	return send_message(conn, reply);
}

/* XXX what happens if we get a bad reply?  close the connection? */
void process_incoming_message(struct minirpc_connection *conn)
{
	struct minirpc_message *msg=conn->recv_msg;
	struct pending_reply *pending;
	
	if (msg->hdr.status == MINIRPC_REQUEST) {
		conn->set->request(conn, conn->data, msg);
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
			minirpc_free_message(msg);
			if (pending != NULL) {
				minirpc_free_message(pending->request);
				free(pending);
			}
			return;
		}
		if (pending->async) {
			msg->callback=pending->data.async.callback;
			msg->private=pending->data.async.private;
			pthread_mutex_lock(&set->callback_queue_lock);
			list_add_tail(&msg->lh_msgs, &set->callback_queue);
			pthread_cond_signal(&set->callback_queue_cond);
			pthread_mutex_unlock(&set->callback_queue_lock);
		} else {
			pthread_mutex_lock(&conn->sync_wakeup_lock);
			*pending->data.sync.reply=msg;
			pthread_cond_signal(pending->data.sync.cond);
			pthread_mutex_unlock(&conn->sync_wakeup_lock);
		}
		free(pending);
	}
}
