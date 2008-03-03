/*
 * miniRPC - TCP RPC library with asynchronous operations and TLS support
 *
 * Copyright (C) 2007-2008 Carnegie Mellon University
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
	struct mrpc_config conf;
	void *private;

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
	EVENT_IOERR,
	EVENT_Q_SHUTDOWN
};

struct mrpc_event {
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

enum shutdown_flags {
	SHUT_STARTED		= 0x0001,
	SHUT_SQUASH_EVENTS	= 0x0002,
	SHUT_FD_CLOSED		= 0x0004,
};

struct mrpc_connection {
	struct mrpc_conn_set *set;
	int fd;
	void *private;

	pthread_mutex_t shutdown_lock;
	unsigned shutdown_flags;
	enum mrpc_disc_reason disc_reason;

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

	gint next_sequence;  /* atomic operations only */
};

/* connection.c */
mrpc_status_t send_message(struct mrpc_message *msg);
void mrpc_conn_free(struct mrpc_connection *conn);

/* message.c */
struct pending_reply;
struct mrpc_message *mrpc_alloc_message(struct mrpc_connection *conn);
void mrpc_free_message(struct mrpc_message *msg);
void mrpc_alloc_message_data(struct mrpc_message *msg, unsigned len);
void mrpc_free_message_data(struct mrpc_message *msg);
void process_incoming_message(struct mrpc_message *msg);
void pending_kill(struct mrpc_connection *conn);
void pending_free(struct pending_reply *pending);

/* event.c */
struct mrpc_event *mrpc_alloc_event(struct mrpc_connection *conn,
			enum event_type type);
struct mrpc_event *mrpc_alloc_message_event(struct mrpc_message *msg,
			enum event_type type);
void queue_event(struct mrpc_event *event);
void destroy_events(struct mrpc_connection *conn);
int thread_on_conn(struct mrpc_connection *conn);

/* pollset.c */
typedef unsigned poll_flags_t;
#define POLLSET_READABLE	((poll_flags_t) 0x1)
#define POLLSET_WRITABLE	((poll_flags_t) 0x2)
typedef void (*poll_callback_fn)(void *private, int fd);
int pollset_alloc(struct pollset **new);
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
int selfpipe_create(struct selfpipe **new);
void selfpipe_destroy(struct selfpipe *sp);
void selfpipe_set(struct selfpipe *sp);
void selfpipe_clear(struct selfpipe *sp);
int selfpipe_is_set(struct selfpipe *sp);
int selfpipe_fd(struct selfpipe *sp);

/* serialize.c */
void *mrpc_alloc_argument(unsigned len);
void mrpc_free_argument(xdrproc_t xdr_proc, void *buf);
mrpc_status_t serialize(xdrproc_t xdr_proc, void *in, char *out,
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

/* util.c */
int set_nonblock(int fd);

/* xdr_len.c */
void xdrlen_create(XDR *xdrs);

#endif
