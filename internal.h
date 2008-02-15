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
#include <apr_ring.h>
#include <apr_hash.h>
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

APR_RING_HEAD(conn_ring, mrpc_connection);
APR_RING_HEAD(event_ring, mrpc_event);

struct mrpc_conn_set {
	struct mrpc_config config;
	const struct mrpc_set_operations *ops;
	void *private;

	apr_pool_t *pool;

	struct conn_ring event_conns;
	apr_file_t *events_notify_pipe_read;
	apr_file_t *events_notify_pipe_write;
	pthread_mutex_t events_lock;

	apr_pollset_t *pollset;
	apr_file_t *shutdown_pipe_read;
	apr_file_t *shutdown_pipe_write;
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
	APR_RING_ENTRY(mrpc_event) lh_events;
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

	apr_hash_t *pending_replies;
	pthread_mutex_t pending_replies_lock;
	pthread_mutex_t sync_wakeup_lock;

	APR_RING_ENTRY(mrpc_connection) lh_event_conns;
	struct event_ring events;	/* protected by set->events_lock */
	struct mrpc_event *plugged_event;
	unsigned plugged_user;

	apr_uint32_t next_sequence;  /* atomic operations only */
};

#define APR_RING_REMOVE_INIT(ep, link) \
	do { \
		APR_RING_UNSPLICE((ep), (ep), link); \
		APR_RING_ELEM_INIT((ep), link); \
	} while (0)
/* We don't want people accidentally using the unclean version */
#undef APR_RING_REMOVE

#define APR_RING_ELEM_EMPTY(ep, link) \
	(APR_RING_NEXT((ep), link) == (ep))

/* init.c */
apr_status_t mrpc_init(void);

/* connection.c */
mrpc_status_t send_message(struct mrpc_message *msg);
void mrpc_conn_free(struct mrpc_connection *conn);

/* message.c */
unsigned numeric_hash_fn(const char *key, apr_ssize_t *klen);
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
