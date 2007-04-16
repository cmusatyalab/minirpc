#include <pthread.h>
#define LIBPROTOCOL
#include "internal.h"
#include "flow.h"

struct pending_reply {
	struct list_head lh_pending;
	struct ISRMessage *request;
	reply_callback_fn *callback;
	void *data;
};

struct sync_data {
	pthread_cond_t *cond;
	struct ISRMessage **reply;
};

/* XXX deal with sequence number wraparound */

struct ISRMessage *isr_alloc_message(void)
{
	struct ISRMessage *ret=malloc(sizeof(struct ISRMessage));
	if (ret == NULL)
		return ret;
	memset(ret, 0, sizeof(struct ISRMessage));
	return ret;
}

void isr_free_message(struct ISRMessage *msg)
{
	if (msg == NULL)
		return;
	ASN_STRUCT_FREE(asn_DEF_ISRMessage, msg);
}

/* XXX It would be nice to be able to make this static */
unsigned request_hash(struct list_head *head, unsigned buckets)
{
	struct pending_reply *pending=list_entry(head, struct pending_reply,
				lh_pending);
	return pending->request->sequence % buckets;
}

static int request_match(struct list_head *head, void *data)
{
	struct pending_reply *pending=list_entry(head, struct pending_reply,
				lh_pending);
	int *sequence=data;
	return (*sequence == pending->request->sequence);
}

static struct pending_reply *request_lookup(struct isr_connection *conn,
			int sequence)
{
	struct list_head *head;
	
	head=hash_get(conn->pending_replies, request_match, sequence,
				&sequence);
	if (head == NULL)
		return NULL;
	return list_entry(head, struct pending_reply, lh_pending);
}

static int validate_request(struct ISRMessage *request, int fromServer,
			int *willReply)
{
	const struct flow_params *params;
	
	params=MessageBody_get_flow(request->body.present);
	if (params == NULL)
		return -EINVAL;
	if (!fromServer && !(params->initiators & INITIATOR_CLIENT))
		return -EINVAL;
	if (fromServer && !(params->initiators & INITIATOR_SERVER))
		return -EINVAL;
	if (willReply != NULL)
		*willReply = params->nr_reply_types ? 1 : 0;
	return 0;
}

static int validate_reply(struct ISRMessage *request, struct ISRMessage *reply)
{
	const struct flow_params *params;
	int i;
	
	params=MessageBody_get_flow(request->body.present);
	if (params == NULL)
		return -EINVAL;
	for (i=0; i<params->nr_reply_types; i++)
		if (reply->body.present == params->reply_types[i])
			break;
	if (i == params->nr_reply_types)
		return -EINVAL;
	return 0;
}

/* If callback != NULL, returns with pending_replies_lock held, except on
   error */
static int _send_request_async(struct isr_connection *conn,
			struct ISRMessage *request,
			reply_callback_fn *callback, void *data)
{
	struct pending_reply *pending=NULL;  /* make gcc happy */
	int ret;
	
	pthread_mutex_lock(&conn->next_sequence_lock);
	request->sequence=conn->next_sequence++;
	pthread_mutex_unlock(&conn->next_sequence_lock);
	request->isReply=0;
	
	if (callback != NULL) {
		pending=malloc(sizeof(*pending));
		if (pending == NULL)
			return -ENOMEM;
		INIT_LIST_HEAD(&pending->lh_pending);
		pending->request=request;
		pending->callback=callback;
		pending->data=data;
		pthread_mutex_lock(&conn->pending_replies_lock);
		hash_add(conn->pending_replies, &pending->lh_pending);
	}
	
	ret=send_message(conn, request);
	if (ret && callback != NULL) {
		hash_remove(conn->pending_replies, &pending->lh_pending);
		pthread_mutex_unlock(&conn->pending_replies_lock);
		free(pending);
	}
	return ret;
}

int isr_send_request_async(struct isr_connection *conn,
			struct ISRMessage *request,
			reply_callback_fn *callback, void *data)
{
	int ret;
	int willReply;
	
	ret=validate_request(request, conn->set->is_server, &willReply);
	if (ret)
		return ret;
	if (!willReply) {
		/* The callback will never be called */
		return -EINVAL;
	}
	/* Locks pending_replies_lock except on error */
	ret=_send_request_async(conn, request, callback, data);
	if (ret)
		return ret;
	pthread_mutex_unlock(&conn->pending_replies_lock);
	return 0;
}

static void sync_callback(struct isr_connection *conn, void *conn_data,
			struct ISRMessage *request, struct ISRMessage *reply,
			void *msg_data)
{
	struct sync_data *sdata=msg_data;
	
	*sdata->reply=reply;
	pthread_cond_signal(sdata->cond);
}

int isr_send_request(struct isr_connection *conn, struct ISRMessage *request,
			struct ISRMessage **reply)
{
	pthread_cond_t cond=PTHREAD_COND_INITIALIZER;
	struct sync_data sdata;
	int ret;
	int willReply;
	
	ret=validate_request(request, conn->set->is_server, &willReply);
	if (ret)
		return ret;
	if (willReply) {
		sdata.cond=&cond;
		sdata.reply=reply;
		/* Locks pending_replies_lock except on error */
		ret=_send_request_async(conn, request, sync_callback, &sdata);
		if (ret)
			return ret;
		pthread_cond_wait(&cond, &conn->pending_replies_lock);
		pthread_mutex_unlock(&conn->pending_replies_lock);
		return 0;
	} else {
		*reply=NULL;
		/* Does not lock pending_replies_lock */
		return _send_request_async(conn, request, NULL, NULL);
	}
}

int isr_send_reply(struct isr_connection *conn,
			struct ISRMessage *request, struct ISRMessage *reply)
{
	int ret;
	
	reply->sequence=request->sequence;
	reply->isReply=1;
	ret=validate_reply(request, reply);
	if (ret)
		return ret;
	return send_message(conn, reply);
}

/* XXX what happens if we get a bad reply?  close the connection? */
void process_incoming_message(struct isr_connection *conn)
{
	struct ISRMessage *msg=conn->recv_msg;
	struct pending_reply *pending;
	
	if (!msg->isReply) {
		if (validate_request(msg, !conn->set->is_server, NULL)) {
			/*XXX*/;
		}
		conn->set->request(conn, conn->data, msg);
	} else {
		pthread_mutex_lock(&conn->pending_replies_lock);
		pending=request_lookup(conn, msg->sequence);
		if (pending != NULL)
			hash_remove(conn->pending_replies,
						&pending->lh_pending);
		pthread_mutex_unlock(&conn->pending_replies_lock);
		if (pending == NULL || validate_reply(pending->request, msg)) {
			isr_free_message(msg);
			if (pending != NULL) {
				isr_free_message(pending->request);
				free(pending);
			}
			return;
		}
		pending->callback(conn, conn->data, pending->request, msg,
					pending->data);
		free(pending);
	}
}
