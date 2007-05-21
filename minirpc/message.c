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

exported mrpc_status_t mrpc_send_request(const struct mrpc_protocol *protocol,
			struct mrpc_connection *conn, int cmd, void *in,
			void **out)
{
	struct mrpc_message *request;
	struct mrpc_message *reply=NULL;
	struct pending_reply *pending;
	pthread_cond_t cond=PTHREAD_COND_INITIALIZER;
	mrpc_status_t ret;
	
	if (protocol != conn->set->config.protocol)
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
	ret=unformat_reply(reply, out);
	mrpc_free_message(reply);
	return ret;
}

exported mrpc_status_t mrpc_send_request_async(
			const struct mrpc_protocol *protocol,
			struct mrpc_connection *conn, int cmd,
			reply_callback_fn *callback, void *private, void *in)
{
	struct mrpc_message *msg;
	struct pending_reply *pending;
	mrpc_status_t ret;
	
	if (protocol != conn->set->config.protocol)
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

exported mrpc_status_t mrpc_send_request_noreply(
			const struct mrpc_protocol *protocol,
			struct mrpc_connection *conn, int cmd, void *in)
{
	struct mrpc_message *msg;
	mrpc_status_t ret;
	
	if (protocol != conn->set->config.protocol)
		return MINIRPC_INVALID_PROTOCOL;
	if (cmd >= 0)
		return MINIRPC_INVALID_ARGUMENT;
	ret=format_request(conn, cmd, in, &msg);
	if (ret)
		return ret;
	return send_message(msg);
}

exported mrpc_status_t mrpc_send_reply(const struct mrpc_protocol *protocol,
			struct mrpc_message *request, void *data)
{
	struct mrpc_message *reply;
	mrpc_status_t ret;
	
	if (protocol != request->conn->set->config.protocol)
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

exported mrpc_status_t mrpc_send_reply_error(
			const struct mrpc_protocol *protocol,
			struct mrpc_message *request, mrpc_status_t status)
{
	struct mrpc_message *reply;
	mrpc_status_t ret;
	
	if (protocol != request->conn->set->config.protocol)
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
