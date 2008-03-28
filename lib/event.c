/*
 * miniRPC - TCP RPC library with asynchronous operations
 *
 * Copyright (C) 2007-2008 Carnegie Mellon University
 *
 * This software is distributed under the terms of the Eclipse Public License,
 * Version 1.0 which can be found in the file named LICENSE.  ANY USE,
 * REPRODUCTION OR DISTRIBUTION OF THIS SOFTWARE CONSTITUTES RECIPIENT'S
 * ACCEPTANCE OF THIS AGREEMENT
 */

#include <sys/poll.h>
#include <stdarg.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#define MINIRPC_INTERNAL
#include "internal.h"

static __thread struct mrpc_event *active_event;

struct dispatch_thread_data {
	struct mrpc_conn_set *set;
	pthread_mutex_t lock;
	pthread_cond_t cond;
	int started;
};

struct mrpc_event *mrpc_alloc_event(struct mrpc_connection *conn,
			enum event_type type)
{
	struct mrpc_event *event;

	event=g_slice_new0(struct mrpc_event);
	event->type=type;
	event->conn=conn;
	return event;
}

struct mrpc_event *mrpc_alloc_message_event(struct mrpc_message *msg,
			enum event_type type)
{
	struct mrpc_event *event;

	event=mrpc_alloc_event(msg->conn, type);
	event->msg=msg;
	return event;
}

static void destroy_event(struct mrpc_event *event)
{
	if (event->addr)
		g_free(event->addr);
	if (event->msg)
		mrpc_free_message(event->msg);
	if (event->errstring)
		free(event->errstring);
	g_slice_free(struct mrpc_event, event);
}

static int conn_is_plugged(struct mrpc_connection *conn)
{
	return (conn->plugged_event != NULL || conn->plugged_user != 0);
}

/* set->events_lock must be held */
static void update_notify_pipe(struct mrpc_conn_set *set)
{
	if (!g_queue_is_empty(set->event_conns) ||
				selfpipe_is_set(set->shutdown_pipe))
		selfpipe_set(set->events_notify_pipe);
	else
		selfpipe_clear(set->events_notify_pipe);
}

/* set->events_lock must be held */
static void try_queue_conn(struct mrpc_connection *conn)
{
	struct mrpc_conn_set *set=conn->set;

	if (conn_is_plugged(conn) || g_queue_is_empty(conn->events) ||
				conn->lh_event_conns != NULL)
		return;
	conn->lh_event_conns=g_list_append(NULL, conn);
	g_queue_push_tail_link(set->event_conns, conn->lh_event_conns);
	update_notify_pipe(set);
}

/* set->events_lock must be held */
static void try_unqueue_conn(struct mrpc_connection *conn)
{
	if (conn->lh_event_conns == NULL)
		return;
	g_queue_delete_link(conn->set->event_conns, conn->lh_event_conns);
	conn->lh_event_conns=NULL;
	update_notify_pipe(conn->set);
}

void queue_event(struct mrpc_event *event)
{
	struct mrpc_connection *conn=event->conn;

	pthread_mutex_lock(&conn->set->events_lock);
	g_queue_push_tail(conn->events, event);
	conn->events_pending++;
	try_queue_conn(conn);
	pthread_mutex_unlock(&conn->set->events_lock);
}

void queue_ioerr_event(struct mrpc_connection *conn, char *fmt, ...)
{
	struct mrpc_event *event;
	va_list ap;

	event=mrpc_alloc_event(conn, EVENT_IOERR);
	va_start(ap, fmt);
	if (vasprintf(&event->errstring, fmt, ap) == -1)
		event->errstring=NULL;
	va_end(ap);
	queue_event(event);
}

/* events_lock must be held */
static struct mrpc_event *unqueue_event(struct mrpc_conn_set *set)
{
	struct mrpc_connection *conn;
	struct mrpc_event *event=NULL;

	conn=g_queue_pop_head(set->event_conns);
	if (conn != NULL) {
		conn->lh_event_conns=NULL;
		event=g_queue_pop_head(conn->events);
		assert(event != NULL);
		conn->plugged_event=event;
	}
	update_notify_pipe(set);
	return event;
}

void kick_event_shutdown_sequence(struct mrpc_connection *conn)
{
	struct mrpc_event *event;

	pthread_mutex_lock(&conn->sequence_lock);
	pthread_mutex_lock(&conn->set->events_lock);
	if (conn->events_pending == 0 &&
				(conn->sequence_flags & SEQ_FD_CLOSED) &&
				!(conn->sequence_flags & SEQ_PENDING_INIT)) {
		conn->sequence_flags |= SEQ_PENDING_INIT;
		pthread_mutex_unlock(&conn->set->events_lock);
		pthread_mutex_unlock(&conn->sequence_lock);
		pending_kill(conn);
		pthread_mutex_lock(&conn->sequence_lock);
		pthread_mutex_lock(&conn->set->events_lock);
		conn->sequence_flags |= SEQ_PENDING_DONE;
	}
	if (conn->events_pending == 0 &&
				(conn->sequence_flags & SEQ_PENDING_DONE) &&
				!(conn->sequence_flags & SEQ_DISC_FIRED)) {
		conn->sequence_flags |= SEQ_DISC_FIRED;
		pthread_mutex_unlock(&conn->set->events_lock);
		pthread_mutex_unlock(&conn->sequence_lock);
		event=mrpc_alloc_event(conn, EVENT_DISCONNECT);
		queue_event(event);
	} else {
		pthread_mutex_unlock(&conn->set->events_lock);
		pthread_mutex_unlock(&conn->sequence_lock);
	}
}

static void finish_event(struct mrpc_connection *conn, refserial_t serial)
{
	int count;

	ref_put(conn->running_event_ref, serial);
	pthread_mutex_lock(&conn->set->events_lock);
	assert(conn->events_pending > 0);
	count=--conn->events_pending;
	pthread_mutex_unlock(&conn->set->events_lock);
	if (!count)
		kick_event_shutdown_sequence(conn);
}

static int _mrpc_release_event(struct mrpc_event *event)
{
	struct mrpc_connection *conn=event->conn;

	pthread_mutex_lock(&conn->set->events_lock);
	if (conn->plugged_event == NULL || conn->plugged_event != event) {
		pthread_mutex_unlock(&conn->set->events_lock);
		return ENOENT;
	}
	assert(conn->lh_event_conns == NULL);
	conn->plugged_event=NULL;
	try_queue_conn(conn);
	pthread_mutex_unlock(&conn->set->events_lock);
	return 0;
}

/* Hidden parameter: "active_event", a thread-local variable set by
   dispatch_event().  For external use only! */
exported int mrpc_release_event(void)
{
	if (active_event == NULL)
		return EPERM;
	return _mrpc_release_event(active_event);
}

exported int mrpc_stop_events(struct mrpc_connection *conn)
{
	int was_plugged;
	refserial_t serial;

	if (conn == NULL)
		return EINVAL;
	pthread_mutex_lock(&conn->set->events_lock);
	was_plugged=conn->plugged_user++;
	try_unqueue_conn(conn);
	if (!was_plugged)
		serial=ref_update(conn->running_event_ref);
	pthread_mutex_unlock(&conn->set->events_lock);
	if (was_plugged)
		return EALREADY;
	ref_wait(conn->running_event_ref, serial);
	return 0;
}

exported int mrpc_start_events(struct mrpc_connection *conn)
{
	int ret=0;

	if (conn == NULL)
		return EINVAL;
	pthread_mutex_lock(&conn->set->events_lock);
	if (conn->plugged_user) {
		conn->plugged_user--;
		try_queue_conn(conn);
	} else {
		ret=EINVAL;
	}
	pthread_mutex_unlock(&conn->set->events_lock);
	return ret;
}

exported int mrpc_get_event_fd(struct mrpc_conn_set *set)
{
	return selfpipe_fd(set->events_notify_pipe);
}

static void fail_request(struct mrpc_event *event, mrpc_status_t err)
{
	struct mrpc_message *request=event->msg;

	_mrpc_release_event(event);
	if (request->hdr.cmd >= 0) {
		if (mrpc_send_reply_error(request->conn->set->protocol,
					request->hdr.cmd, request, err))
			mrpc_free_message(request);
	} else {
		mrpc_free_message(request);
	}
}

static void dispatch_request(struct mrpc_event *event)
{
	struct mrpc_connection *conn=event->conn;
	struct mrpc_message *request=event->msg;
	const void *ops;
	refserial_t serial;
	void *request_data;
	void *reply_data=NULL;
	mrpc_status_t ret;
	mrpc_status_t result;
	xdrproc_t request_type;
	xdrproc_t reply_type;
	unsigned reply_size;
	int doreply;

	assert(request->hdr.status == MINIRPC_PENDING);

	if (conn->set->protocol->receiver_request_info(request->hdr.cmd,
				&request_type, NULL)) {
		/* Unknown opcode */
		fail_request(event, MINIRPC_PROCEDURE_UNAVAIL);
		return;
	}

	doreply=(request->hdr.cmd >= 0);
	if (doreply) {
		if (conn->set->protocol->receiver_reply_info(request->hdr.cmd,
					&reply_type, &reply_size)) {
			/* Can't happen if the info tables are well-formed */
			fail_request(event, MINIRPC_ENCODING_ERR);
			return;
		}
		reply_data=mrpc_alloc_argument(reply_size);
	}
	ret=unformat_request(request, &request_data);
	if (ret) {
		/* Invalid datalen, etc. */
		fail_request(event, ret);
		mrpc_free_argument(NULL, reply_data);
		return;
	}
	/* We don't need the serialized request data anymore.  The request
	   struct may stay around for a while, so free up some memory. */
	mrpc_free_message_data(request);

	assert(conn->set->protocol->request != NULL);
	serial=ref_get(conn->operations_ref);
	ops=g_atomic_pointer_get(&conn->operations);
	result=conn->set->protocol->request(ops, conn->private, request,
				request->hdr.cmd, request_data, reply_data);
	/* Note: if the application returned MINIRPC_PENDING and then
	   immediately sent its reply from another thread, the request has
	   already been freed.  So, if result == MINIRPC_PENDING, we can't
	   access @request anymore. */
	ref_put(conn->operations_ref, serial);
	_mrpc_release_event(event);
	mrpc_free_argument(request_type, request_data);

	if (doreply) {
		if (result == MINIRPC_PENDING) {
			mrpc_free_argument(reply_type, reply_data);
			return;
		}
		if (result)
			ret=mrpc_send_reply_error(conn->set->protocol,
						request->hdr.cmd, request,
						result);
		else
			ret=mrpc_send_reply(conn->set->protocol,
						request->hdr.cmd, request,
						reply_data);
		mrpc_free_argument(reply_type, reply_data);
		if (ret) {
			queue_ioerr_event(conn, "Synchronous reply failed, "
						"seq %u cmd %d status %d "
						"err %d",
						request->hdr.sequence,
						request->hdr.cmd, result, ret);
			mrpc_free_message(request);
		}
	} else {
		mrpc_free_message(request);
	}
}

static void run_reply_callback(struct mrpc_event *event)
{
	struct mrpc_message *reply=event->msg;
	long_reply_callback_fn *longfn =
				(long_reply_callback_fn *) event->callback;
	short_reply_callback_fn *shortfn =
				(short_reply_callback_fn *) event->callback;
	void *out=NULL;
	xdrproc_t type;
	unsigned size;
	mrpc_status_t ret;

	if (reply->conn->set->protocol->sender_reply_info(reply->hdr.cmd,
				&type, &size)) {
		/* Can't happen if the info tables are well-formed */
		queue_ioerr_event(reply->conn, "Internal error running reply "
					"callback, seq %u cmd %d status %d",
					reply->hdr.sequence, reply->hdr.cmd,
					reply->hdr.status);
		mrpc_free_message(reply);
		return;
	}
	ret=unformat_reply(reply, &out);
	/* On x86, we could unconditionally call the four-argument form, even
	   if the function we're calling only expects three arguments, since
	   the extra argument would merely languish on the stack.  But I don't
	   want to make assumptions about the calling convention of the machine
	   architecture.  This should ensure that a function expecting three
	   arguments gets three, and a function expecting four arguments gets
	   four. */
	if (size)
		longfn(reply->conn->private, event->private, ret, out);
	else
		shortfn(reply->conn->private, event->private, ret);
	mrpc_free_argument(type, out);
	mrpc_free_message(reply);
}

static void dispatch_event(struct mrpc_event *event, refserial_t serial)
{
	struct mrpc_connection *conn=event->conn;
	mrpc_accept_fn *accept;
	mrpc_disconnect_fn *disconnect;
	mrpc_ioerr_fn *ioerr;
	int squash;
	int fire_disconnect;
	enum mrpc_disc_reason reason;

	assert(active_event == NULL);
	active_event=event;
	conn_get(conn);
	pthread_mutex_lock(&conn->sequence_lock);
	squash=conn->sequence_flags & SEQ_SQUASH_EVENTS;
	fire_disconnect=conn->sequence_flags & SEQ_HAVE_FD;
	reason=conn->disc_reason;
	pthread_mutex_unlock(&conn->sequence_lock);

	if (squash) {
		switch (event->type) {
		case EVENT_REQUEST:
		case EVENT_IOERR:
			_mrpc_release_event(event);
			destroy_event(event);
			goto out;
		case EVENT_REPLY:
			event->msg->hdr.status=MINIRPC_NETWORK_FAILURE;
			break;
		default:
			break;
		}
	}

	switch (event->type) {
	case EVENT_ACCEPT:
		accept=get_config(conn->set, accept);
		assert(accept != NULL);
		conn->private=accept(conn->set->private, conn, event->addr,
					event->addrlen);
		g_free(event->addr);
		break;
	case EVENT_REQUEST:
		dispatch_request(event);
		break;
	case EVENT_REPLY:
		run_reply_callback(event);
		break;
	case EVENT_DISCONNECT:
		disconnect=get_config(conn->set, disconnect);
		if (fire_disconnect && disconnect)
			disconnect(conn->private, reason);
		conn_put(conn);
		break;
	case EVENT_IOERR:
		ioerr=get_config(conn->set, ioerr);
		if (ioerr)
			ioerr(conn->private, event->errstring);
		free(event->errstring);
		break;
	default:
		assert(0);
	}
	_mrpc_release_event(event);
	g_slice_free(struct mrpc_event, event);
out:
	finish_event(conn, serial);
	conn_put(conn);
	assert(active_event == event);
	active_event=NULL;
}

void destroy_events(struct mrpc_connection *conn)
{
	struct mrpc_event *event;

	pthread_mutex_lock(&conn->set->events_lock);
	try_unqueue_conn(conn);
	while ((event=g_queue_pop_head(conn->events)) != NULL)
		destroy_event(event);
	pthread_mutex_unlock(&conn->set->events_lock);
}

exported void mrpc_dispatcher_add(struct mrpc_conn_set *set)
{
	if (set == NULL)
		return;
	pthread_mutex_lock(&set->events_lock);
	set->events_threads++;
	pthread_mutex_unlock(&set->events_lock);
}

exported void mrpc_dispatcher_remove(struct mrpc_conn_set *set)
{
	if (set == NULL)
		return;
	pthread_mutex_lock(&set->events_lock);
	set->events_threads--;
	pthread_cond_broadcast(&set->events_threads_cond);
	pthread_mutex_unlock(&set->events_lock);
}

static int mrpc_dispatch_one(struct mrpc_conn_set *set)
{
	struct mrpc_event *event;
	refserial_t serial;

	if (set == NULL)
		return EINVAL;
	pthread_mutex_lock(&set->events_lock);
	event=unqueue_event(set);
	if (event != NULL) {
		serial=ref_get(event->conn->running_event_ref);
		pthread_mutex_unlock(&set->events_lock);
		dispatch_event(event, serial);
	} else {
		pthread_mutex_unlock(&set->events_lock);
	}
	if (selfpipe_is_set(set->shutdown_pipe))
		return ENXIO;
	else if (event)
		return 0;
	else
		return EAGAIN;
}

exported int mrpc_dispatch(struct mrpc_conn_set *set, int max)
{
	int i;
	int ret;

	for (i=0; i < max || max == 0; i++) {
		ret=mrpc_dispatch_one(set);
		if (ret)
			return ret;
	}
	return 0;
}

exported int mrpc_dispatch_loop(struct mrpc_conn_set *set)
{
	int ret;

	while (1) {
		ret=mrpc_dispatch_one(set);
		if (ret == EAGAIN)
			selfpipe_wait(set->events_notify_pipe);
		else if (ret)
			break;
	}
	return ret;
}

static void *dispatch_thread(void *arg)
{
	struct dispatch_thread_data *data=arg;
	struct mrpc_conn_set *set=data->set;

	block_signals();
	mrpc_dispatcher_add(set);
	pthread_mutex_lock(&data->lock);
	data->started=1;
	pthread_cond_broadcast(&data->cond);
	pthread_mutex_unlock(&data->lock);
	mrpc_dispatch_loop(set);
	mrpc_dispatcher_remove(set);
	return NULL;
}

exported int mrpc_start_dispatch_thread(struct mrpc_conn_set *set)
{
	struct dispatch_thread_data data = {0};
	pthread_t thr;
	pthread_attr_t attr;
	int ret;

	if (set == NULL)
		return EINVAL;
	data.set=set;
	pthread_mutex_init(&data.lock, NULL);
	pthread_cond_init(&data.cond, NULL);
	ret=pthread_attr_init(&attr);
	if (ret)
		return ret;
	ret=pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	if (ret)
		goto out;
	ret=pthread_create(&thr, &attr, dispatch_thread, &data);
	pthread_mutex_lock(&data.lock);
	while (!data.started)
		pthread_cond_wait(&data.cond, &data.lock);
	pthread_mutex_unlock(&data.lock);
out:
	pthread_attr_destroy(&attr);
	return ret;
}
