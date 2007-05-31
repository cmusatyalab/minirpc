#ifndef MINIRPC_INTERNAL
#error This header is for internal use by the miniRPC protocol library
#endif

#ifndef MINIRPC_INTERNAL_H
#define MINIRPC_INTERNAL_H
#define MINIRPC_PROTOCOL

#include <sys/types.h>
#include <sys/socket.h>
#include <minirpc/minirpc.h>
#include <minirpc/protocol.h>
#include <minirpc/list.h>
#include <minirpc/hash.h>
#include "minirpc_xdr.h"

#include "config.h"
#ifdef HAVE_VISIBILITY
#define exported __attribute__ ((visibility ("default")))
#else
#define exported
#endif

struct mrpc_conn_set {
	struct mrpc_config config;
	const struct mrpc_set_operations *ops;
	void *private;

	struct htable *conns;
	pthread_mutex_t conns_lock;

	struct list_head event_conns;
	int events_notify_pipe[2];
	pthread_mutex_t events_lock;

	int epoll_fd;
	int shutdown_pipe[2];
	pthread_t thread;
	unsigned events_threads;		/* protected by events_lock */
	pthread_cond_t events_threads_cond;
};

enum event_type {
	EVENT_ACCEPT,
	EVENT_REQUEST,
	EVENT_REPLY,
	EVENT_DISCONNECT,
	EVENT_IOERR
};

struct mrpc_event {
	struct list_head lh_events;
	enum event_type type;
	struct mrpc_connection *conn;

	/* accept */
	struct sockaddr *addr;
	socklen_t addrlen;

	/* request/reply */
	struct mrpc_message *msg;

	/* reply */
	reply_callback_fn *callback;
	void *private;

	/* disconnect */
	enum mrpc_disc_reason disc_reason;

	/* message errors */
	char *errstring;
};

struct mrpc_message {
	struct mrpc_connection *conn;
	struct mrpc_event *event;
	struct list_head lh_msgs;
	struct mrpc_header hdr;
	char *data;
};

enum conn_state {
	STATE_IDLE,
	STATE_HEADER,
	STATE_DATA
};

struct mrpc_connection {
	struct list_head lh_conns;
	struct mrpc_conn_set *set;
	int fd;
	void *private;

	const void *operations;
	pthread_mutex_t operations_lock;

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

	struct list_head lh_event_conns;
	struct list_head events;	/* protected by set->events_lock */
	struct mrpc_event *plugged_event;
	unsigned plugged_user;

	int next_sequence;
	pthread_mutex_t next_sequence_lock;
};

/* connection.c */
mrpc_status_t send_message(struct mrpc_message *msg);

/* message.c */
unsigned request_hash(struct list_head *head, unsigned buckets);
void process_incoming_message(struct mrpc_message *msg);

/* event.c */
struct mrpc_event *mrpc_alloc_event(struct mrpc_connection *conn,
			enum event_type type);
struct mrpc_event *mrpc_alloc_message_event(struct mrpc_message *msg,
			enum event_type type);
void queue_event(struct mrpc_event *event);

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
