#include "protocol.h"
#include "hash.h"

typedef (void)(struct ISRMessage *request, struct ISRMessage *reply,
			void *data) reply_callback_t;

struct pending_entry {
	struct list_head lh_hash;
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


static unsigned mux_hash(struct list_head *head, unsigned buckets)
{
	struct pending_entry *entry=list_entry(head, struct pending_entry,
				lh_hash);
	return something % buckets;
}

static int mux_match(struct list_head *head, void *data)
{
	XXX;
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
static int _send_message_async(struct ISRMessage *msg,
			reply_callback_t callback, void *data)
{
	struct pending_entry *entry=malloc(sizeof(*entry));
	
	if (entry == NULL)
		return -ENOMEM;
	INIT_LIST_HEAD(&entry->lh_hash);
	entry->request=msg;
	entry->callback=callback;
	entry->data=data;
	pthread_mutex_lock(&pending.lock);
	list_add_tail(&entry->lh_hash, pending.hash[hash(something)]);
	return 0;
}

int send_message_async(struct ISRMessage *msg, reply_callback_t callback,
			void *data)
{
	_send_message_async(msg, callback, data);
	pthread_mutex_unlock(&pending.lock);
	return 0;
}

int send_message(struct ISRMessage *request, struct ISRMessage **reply)
{
	pthread_cond_t cond=PTHREAD_COND_INITIALIZER;
	struct sync_data sdata;
	int ret;
	
	sdata.cond=&cond;
	sdata.reply=reply;
	/* Locks pending.lock */
	ret=_send_message_async(request, sync_callback, &sdata);
	if (ret)
		return ret;
	pthread_cond_wait(&cond, &pending.lock);
	pthread_mutex_unlock(&pending.lock);
	return 0;
}

int send_message_dropreply(struct ISRMessage *request)
{
	return send_message_async(request, noreply_callback, NULL);
}

void process_incoming_message(struct ISRMessage *msg)
{
	
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
