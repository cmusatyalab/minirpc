#ifndef LIBPROTOCOL
#error This header is for internal use by the ISR protocol library
#endif

#ifndef INTERNAL_H
#define INTERNAL_H

#include "protocol.h"
#include "minirpc_xdr.h"
#include "list.h"
#include "hash.h"

#define SET_PTR_IF_NOT_NULL(ptr, val) do { \
		if ((ptr) != NULL) \
			*(ptr)=(val); \
	} while (0)

struct minirpc_message {
	struct minirpc_connection *conn;
	struct list_head lh_msgs;
	struct minirpc_header hdr;
	char *data;
	
	/* For async callbacks */
	reply_callback_fn *callback;
	void *private;
};

struct minirpc_protocol {
	int (*request)(struct minirpc_connection *conn, int cmd, void *in,
			void *out);
	int (*request_info)(unsigned cmd, xdrproc_t *type, unsigned *size);
	int (*reply_info)(unsigned cmd, xdrproc_t *type, unsigned *size);
};

struct minirpc_conn_set {
	const struct minirpc_protocol *protocol;
	unsigned maxbuf;
	unsigned expected_fds;
	unsigned msg_buckets;
	
	struct htable *conns;
	pthread_mutex_t conns_lock;
	
	struct list_head callback_queue;
	pthread_mutex_t callback_queue_lock;
	pthread_cond_t callback_queue_cond;
	
	int epoll_fd;
	int signal_pipe[2];
	pthread_t thread;
};

enum conn_state {
	STATE_HEADER,
	STATE_DATA
}

struct minirpc_connection {
	struct list_head lh_conns;
	struct isr_conn_set *set;
	int fd;
	void *private;
	
	void *operations;
	pthread_rwlock_t operations_lock;
	
	struct list_head send_msgs;
	pthread_mutex_t send_msgs_lock;
	struct queued_message *send_msg;
	enum conn_state send_state;
	char send_hdr_buf[MINIRPC_HEADER_LEN];
	unsigned send_offset;
	
	unsigned recv_offset;
	unsigned recv_length;
	char recv_hdr_buf[MINIRPC_HEADER_LEN];
	enum conn_state recv_state;
	struct queued_message *recv_msg;
	
	struct htable *pending_replies;
	pthread_mutex_t pending_replies_lock;
	pthread_mutex_t sync_wakeup_lock;
	
	int next_sequence;
	pthread_mutex_t next_sequence_lock;
};

typedef void (reply_callback_fn)(void *conn_private, void *msg_private,
			int status, void *data);

/* connection.c */
int send_message(struct isr_connection *conn, struct ISRMessage *msg);

/* message.c */
struct minirpc_message *minirpc_alloc_message(void);
void minirpc_free_message(struct minirpc_message *msg);
unsigned request_hash(struct list_head *head, unsigned buckets);
void process_incoming_message(struct isr_connection *conn);
int isr_send_request(struct isr_connection *conn, struct ISRMessage *request,
			struct ISRMessage **reply);
int isr_send_request_async(struct isr_connection *conn,
			struct ISRMessage *request,
			reply_callback_fn *callback, void *data);
int isr_send_reply(struct isr_connection *conn,
			struct ISRMessage *request, struct ISRMessage *reply);

/* xdr_len.c */
void xdrlen_create(XDR *xdrs);

#endif
