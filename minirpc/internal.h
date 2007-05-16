#ifndef MINIRPC_INTERNAL
#error This header is for internal use by the miniRPC protocol library
#endif

#ifndef MINIRPC_INTERNAL_H
#define MINIRPC_INTERNAL_H
#define MINIRPC_PROTOCOL

#include "minirpc.h"
#include "minirpc_protocol.h"
#include "minirpc_xdr.h"
#include "list.h"
#include "hash.h"

struct mrpc_conn_set {
	const struct mrpc_protocol *protocol;
	unsigned maxbuf;
	unsigned expected_fds;
	unsigned msg_buckets;
	
	struct htable *conns;
	pthread_mutex_t conns_lock;
	
	struct list_head events;
	int events_notify_pipe[2];
	pthread_mutex_t events_lock;
	
	int epoll_fd;
	int shutdown_pipe[2];
	pthread_t thread;
	unsigned events_threads;		/* protected by events_lock */
	pthread_cond_t events_threads_cond;
};

enum conn_state {
	STATE_HEADER,
	STATE_DATA
};

struct mrpc_message {
	struct mrpc_connection *conn;
	struct list_head lh_msgs;
	struct mrpc_header hdr;
	char *data;
	
	/* For async callbacks */
	reply_callback_fn *callback;
	void *private;
};

struct mrpc_connection {
	struct list_head lh_conns;
	struct mrpc_conn_set *set;
	int fd;
	void *private;
	
	void *operations;
	pthread_rwlock_t operations_lock;
	
	struct list_head send_msgs;
	pthread_mutex_t send_msgs_lock;
	struct mrpc_message *send_msg;
	enum conn_state send_state;
	char send_hdr_buf[MINIRPC_HEADER_LEN];
	unsigned send_offset;
	
	unsigned recv_offset;
	unsigned recv_length;
	char recv_hdr_buf[MINIRPC_HEADER_LEN];
	enum conn_state recv_state;
	struct mrpc_message *recv_msg;
	
	struct htable *pending_replies;
	pthread_mutex_t pending_replies_lock;
	pthread_mutex_t sync_wakeup_lock;
	
	int next_sequence;
	pthread_mutex_t next_sequence_lock;
};

/* connection.c */
mrpc_status_t send_message(struct mrpc_message *msg);

/* message.c */
unsigned request_hash(struct list_head *head, unsigned buckets);
void process_incoming_message(struct mrpc_connection *conn);

/* serialize.c */
struct mrpc_message *mrpc_alloc_message(struct mrpc_connection *conn);
void mrpc_free_message(struct mrpc_message *msg);
void cond_free(void *ptr);
mrpc_status_t serialize_len(xdrproc_t xdr_proc, void *in, char *out,
			unsigned out_len);
mrpc_status_t unserialize(xdrproc_t xdr_proc, char *in, unsigned in_len,
			void *out, unsigned out_len);
mrpc_status_t format_request(struct mrpc_connection *conn, unsigned cmd,
			void *data, struct mrpc_message **result);
mrpc_status_t format_reply(struct mrpc_message *request, void *data,
			struct mrpc_message **result);
mrpc_status_t format_reply_error(struct mrpc_message *request,
			mrpc_status_t status, struct mrpc_message **result);
mrpc_status_t unformat_request(struct mrpc_message *msg, void **result);
mrpc_status_t unformat_reply(struct mrpc_message *msg, void **result);

/* xdr_len.c */
void xdrlen_create(XDR *xdrs);

#endif
