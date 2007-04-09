#include "protocol.h"
#include "hash.h"

typedef (void)(struct ISRMessage *request, struct ISRMessage *reply,
			void *data) reply_callback_t;

/* XXX need to deal with fd reuse on teardown -- separate linked list per-fd */
struct pending_entry {
	struct list_head lh_hash;
	int fd;
	struct ISRMessage *request;
	reply_callback_t callback;
	void *data;
};

static struct {
	pthread_mutex_t lock;
	struct htable *hash;
} pending;

struct sync_data {
	pthread_cond_t *cond;
	struct ISRMessage **reply;
}

/* XXX sequence wraparound */
struct match_data {
	int fd,
	int sequence;
}

static unsigned mux_hash(struct list_head *head, unsigned buckets)
{
	struct pending_entry *entry=list_entry(head, struct pending_entry,
				lh_hash);
	return (entry->fd + entry->request->sequence) % buckets;
}

static int mux_match(struct list_head *head, void *data)
{
	struct pending_entry *entry=list_entry(head, struct pending_entry,
				lh_hash);
	struct match_data *mdata=data;
	
	return (mdata->fd == entry->fd &&
				mdata->sequence == entry->request->sequence);
}

static struct pending_entry *request_lookup(int fd, int sequence)
{
	struct match_data mdata;
	struct list_head *head;
	
	mdata.fd=fd;
	mdata.sequence=sequence;
	head=hash_get(pending.hash, mux_match, fd + sequence, &mdata);
	if (head == NULL)
		return NULL;
	return list_entry(head, struct pending_entry, lh_hash);
}

static void noreply_callback(struct ISRMessage *request,
			struct ISRMessage *reply, void *data)
{
	return;
}

static void sync_callback(struct ISRMessage *request,
			struct ISRMessage *reply, void *data)
{
	struct sync_data *sdata=data;
	
	*sdata->reply=reply;
	pthread_cond_signal(sdata->cond);
}

/* Returns with pending.lock held, except on error */
static int _send_request_async(struct ISRMessage *msg,
			reply_callback_t callback, void *data)
{
	struct pending_entry *entry;
	int ret;
	
	ret=validate_request(msg, fromClient, isAsync);
	if (ret)
		return ret;
	entry=malloc(sizeof(*entry));
	if (entry == NULL)
		return -ENOMEM;
	INIT_LIST_HEAD(&entry->lh_hash);
	entry->request=msg;
	entry->callback=callback;
	entry->data=data;
	pthread_mutex_lock(&pending.lock);
	hash_add(pending.hash, &entry->lh_hash);
	return 0;
}

void free_message(struct ISRMessage *msg)
{
	if (msg == NULL)
		return;
	ASN_STRUCT_FREE(&asn_DEF_ISRMessage, msg);
}

int send_request_async(struct ISRMessage *msg, reply_callback_t callback,
			void *data)
{
	_send_request_async(msg, callback, data);
	pthread_mutex_unlock(&pending.lock);
	return 0;
}

int send_request(struct ISRMessage *request, struct ISRMessage **reply)
{
	pthread_cond_t cond=PTHREAD_COND_INITIALIZER;
	struct sync_data sdata;
	int ret;
	
	sdata.cond=&cond;
	sdata.reply=reply;
	/* Locks pending.lock */
	ret=_send_request_async(request, sync_callback, &sdata);
	if (ret)
		return ret;
	pthread_cond_wait(&cond, &pending.lock);
	pthread_mutex_unlock(&pending.lock);
	return 0;
}

int send_request_dropreply(struct ISRMessage *request)
{
	return send_request_async(request, noreply_callback, NULL);
}

/* XXX need to stop using "response" instead of "reply" */
int send_reply(struct ISRMessage *request, struct ISRMessage *reply)
{
	int ret;
	
	ret=validate_response(request, reply);
	if (ret)
		return ret;
	XXX;
}

/* XXX what happens if we get a bad reply?  close the connection? */
void process_incoming_message(struct ISRMessage *msg)
{
	if (msg->direction == MessageDirection_request) {
		if (validate_request(msg, fromClient, async)) {
			XXX;
		}
		XXX;
	} else {
		struct pending_entry *entry;
		int last = (msg->direction == MessageDirection_last_response);
		
		pthread_mutex_lock(&pending.lock);
		entry=request_lookup(fd, sequence);
		if (last && entry != NULL)
			hash_remove(pending.hash, &entry->lh_hash);
		pthread_mutex_unlock(&pending.lock);
		if (entry == NULL || validate_response(request, msg)) {
			free_message(msg);
			if (last && entry != NULL) {
				free_message(entry->request);
				free(entry);
			}
			return;
		}
		entry->callback(entry->request, msg, entry->data);
		if (last)
			free(entry);
	}
}

static int validate_request(struct ISRMessage *request, int fromClient,
			int async)
{
	const struct flow_params *params;
	
	params=ISRMessageBody_get_flow(request->body.present);
	if (params == NULL)
		return -EINVAL;
	if (request->direction != MessageDirection_request)
		return -EINVAL;  /* XXX necessary? */
	if (fromClient && !(params->initiators & INITIATOR_CLIENT))
		return -EINVAL;
	if (!fromClient && !(params->initiators & INITIATOR_SERVER))
		return -EINVAL;
	if (params->multi && !async)
		return -EINVAL;
	return 0;
}

static int validate_response(struct ISRMessage *request,
			struct ISRMessage *response)
{
	const struct flow_params *params;
	int *rtype;
	
	params=ISRMessageBody_get_flow(request->body.present);
	if (params == NULL)
		return -EINVAL;
	for (rtype=params->response_types; *rtype; rtype++)
		if (response->body.present == *rtype)
			break;
	if (*rtype == 0)
		return -EINVAL;
	if (response->direction != MessageDirection_last_response &&
				!(params->multi && response->direction ==
				MessageDirection_response))
		return -EINVAL;
	return 0;
}

int protocol_init(unsigned table_size)
{
	int i;
	
	pthread_mutex_init(&pending.lock, NULL);
	pending.hash=hash_alloc(table_size, mux_hash);
	if (pending.hash == NULL)
		return -ENOMEM;
	return 0;
}
