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

/* XXX do we want to fail if there are no possible replies?  the callback
   can never be called */
int send_request_async(struct ISRMessage *msg, reply_callback_t callback,
			void *data)
{
	_send_request_async(msg, callback, data);
	pthread_mutex_unlock(&pending.lock);
	return 0;
}

/* XXX if validate struct says there are no possible replies, return
   immediately */
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
			int async, int *willReply)
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
	if (willReply != NULL)
		*willReply = params->nr_response_types ? 1 : 0;
	return 0;
}

/* XXX this isn't safe if genflow processed multiple choice types, since
   the enum definitions may overlap */
static int validate_response(struct ISRMessage *request,
			struct ISRMessage *response)
{
	const struct flow_params *params;
	int i;
	
	params=ISRMessageBody_get_flow(request->body.present);
	if (params == NULL)
		return -EINVAL;
	for (i=0; i<params->nr_response_types; i++)
		if (response->body.present == params->response_types[i])
			break;
	if (i == params->nr_response_types)
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
