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
			int async, int *willReply)
{
	const struct flow_params *params;
	
	params=ISRMessageBody_get_flow(request->body.present);
	if (params == NULL)
		return -EINVAL;
	if (request->direction != MessageDirection_request)
		return -EINVAL;  /* XXX necessary? */
	if (!fromServer && !(params->initiators & INITIATOR_CLIENT))
		return -EINVAL;
	if (fromServer && !(params->initiators & INITIATOR_SERVER))
		return -EINVAL;
	if (params->multi && !async)
		return -EINVAL;
	if (willReply != NULL)
		*willReply = params->nr_reply_types ? 1 : 0;
	return 0;
}

/* XXX this isn't safe if genflow processed multiple choice types, since
   the enum definitions may overlap */
static int validate_reply(struct ISRMessage *request, struct ISRMessage *reply)
{
	const struct flow_params *params;
	int i;
	
	params=ISRMessageBody_get_flow(request->body.present);
	if (params == NULL)
		return -EINVAL;
	for (i=0; i<params->nr_reply_types; i++)
		if (reply->body.present == params->reply_types[i])
			break;
	if (i == params->nr_reply_types)
		return -EINVAL;
	if (reply->direction != MessageDirection_last_reply && !(params->multi
				&& reply->direction == MessageDirection_reply))
		return -EINVAL;
	return 0;
}

/* If callback != NULL, returns with pending_replies_lock held, except on
   error */
static int _send_request_async(struct isr_connection *conn,
			struct ISRMessage *msg, reply_callback_fn *callback,
			void *data)
{
	struct pending_reply *pending;
	int ret;
	
	if (callback != NULL) {
		pending=malloc(sizeof(*pending));
		if (pending == NULL)
			return -ENOMEM;
		INIT_LIST_HEAD(&pending->lh_pending);
		pending->request=msg;
		pending->callback=callback;
		pending->data=data;
		pthread_mutex_lock(&conn->pending_replies_lock);
		hash_add(conn->pending_replies, &pending->lh_pending);
	}
	/* XXX check lock ordering */
	ret=send_message(conn, msg);
	if (ret && callback != NULL) {
		hash_remove(conn->pending_replies, &pending->lh_pending);
		pthread_mutex_unlock(&conn->pending_replies_lock);
		free(pending);
	}
	return ret;
}

int isr_send_request_async(struct isr_connection *conn, struct ISRMessage *msg,
			reply_callback_fn *callback, void *data)
{
	int ret;
	int willReply;
	
	ret=validate_request(msg, conn->set->is_server, 1, &willReply);
	if (ret)
		return ret;
	if (!willReply) {
		/* The callback will never be called */
		return -EINVAL;
	}
	/* Locks pending_replies_lock except on error */
	ret=_send_request_async(conn, msg, callback, data);
	if (ret)
		return ret;
	pthread_mutex_unlock(&conn->pending_replies_lock);
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
	
	ret=validate_request(request, conn->set->is_server, 0, &willReply);
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

int isr_send_reply(struct isr_connection *conn, struct ISRMessage *request,
			struct ISRMessage *reply)
{
	int ret;
	
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
	int last;
	
	if (msg->direction == MessageDirection_request) {
		if (validate_request(msg, !conn->set->is_server, 1, NULL)) {
			/*XXX*/;
		}
		conn->set->request(conn, conn->data, msg);
	} else {
		last=(msg->direction == MessageDirection_last_reply);
		pthread_mutex_lock(&conn->pending_replies_lock);
		pending=request_lookup(conn, msg->sequence);
		if (last && pending != NULL)
			hash_remove(conn->pending_replies,
						&pending->lh_pending);
		pthread_mutex_unlock(&conn->pending_replies_lock);
		if (pending == NULL || validate_reply(pending->request, msg)) {
			free_message(msg);
			if (last && pending != NULL) {
				free_message(pending->request);
				free(pending);
			}
			return;
		}
		pending->callback(conn, conn->data, pending->request, msg,
					pending->data);
		if (last)
			free(pending);
	}
}
