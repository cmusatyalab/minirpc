/*
 * miniRPC - TCP RPC library with asynchronous operations and TLS support
 *
 * Copyright (C) 2007 Carnegie Mellon University
 *
 * This software is distributed under the terms of the Eclipse Public License,
 * Version 1.0 which can be found in the file named LICENSE.  ANY USE,
 * REPRODUCTION OR DISTRIBUTION OF THIS SOFTWARE CONSTITUTES RECIPIENT'S
 * ACCEPTANCE OF THIS AGREEMENT
 */

#ifndef MINIRPC_INTERNAL
#error This header is for internal use by the miniRPC protocol library
#endif

#ifndef MINIRPC_INTERNAL_H
#define MINIRPC_INTERNAL_H
#define MINIRPC_PROTOCOL

#include <sys/types.h>
#include <sys/socket.h>
#include <glib.h>
#include <apr_pools.h>
#include <apr_poll.h>
#include <minirpc/minirpc.h>
#include <minirpc/protocol.h>
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

	apr_pool_t *pool;

	GQueue *event_conns;
	struct selfpipe *events_notify_pipe;
	pthread_mutex_t events_lock;

	struct pollset *pollset;
	struct selfpipe *shutdown_pipe;
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
	enum event_type type;
	struct mrpc_connection *conn;

	/* accept */
	apr_sockaddr_t *addr;

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
	struct mrpc_header hdr;
	char *data;
};

enum conn_state {
	STATE_IDLE,
	STATE_HEADER,
	STATE_DATA
};

struct mrpc_connection {
	struct mrpc_conn_set *set;
	apr_socket_t *sock;
	void *private;
	apr_pool_t *pool;

	const void *operations;
	pthread_mutex_t operations_lock;

	GQueue *send_msgs;
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

	GHashTable *pending_replies;
	pthread_mutex_t pending_replies_lock;
	pthread_mutex_t sync_wakeup_lock;

	GList *lh_event_conns;
	GQueue *events;	/* protected by set->events_lock */
	struct mrpc_event *plugged_event;
	unsigned plugged_user;

	apr_uint32_t next_sequence;  /* atomic operations only */
};

/* init.c */
apr_status_t mrpc_init(void);

/* connection.c */
mrpc_status_t send_message(struct mrpc_message *msg);
void mrpc_conn_free(struct mrpc_connection *conn);

/* message.c */
void process_incoming_message(struct mrpc_message *msg);

/* event.c */
struct mrpc_event *mrpc_alloc_event(struct mrpc_connection *conn,
			enum event_type type);
struct mrpc_event *mrpc_alloc_message_event(struct mrpc_message *msg,
			enum event_type type);
void queue_event(struct mrpc_event *event);

/* pollset.c */
typedef unsigned poll_flags_t;
#define POLLSET_READABLE	((poll_flags_t) 0x1)
#define POLLSET_WRITABLE	((poll_flags_t) 0x2)
typedef void (*poll_callback_fn)(void *private, int fd);
struct pollset *pollset_alloc(void);
void pollset_free(struct pollset *pset);
int pollset_add(struct pollset *pset, int fd, poll_flags_t flags,
			void *private, poll_callback_fn readable,
			poll_callback_fn writable, poll_callback_fn hangup,
			poll_callback_fn error);
int pollset_modify(struct pollset *pset, int fd, poll_flags_t flags);
void pollset_del(struct pollset *pset, int fd);
int pollset_poll(struct pollset *pset);
void pollset_wake(struct pollset *pset);

/* selfpipe.c */
struct selfpipe *selfpipe_create(void);
void selfpipe_destroy(struct selfpipe *sp);
void selfpipe_set(struct selfpipe *sp);
void selfpipe_clear(struct selfpipe *sp);
int selfpipe_is_set(struct selfpipe *sp);
int selfpipe_fd(struct selfpipe *sp);

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
