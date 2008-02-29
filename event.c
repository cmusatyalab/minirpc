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

#include <sys/poll.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#define MINIRPC_INTERNAL
#include "internal.h"

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

	assert(msg->event == NULL);
	event=mrpc_alloc_event(msg->conn, type);
	event->msg=msg;
	msg->event=event;
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
static void try_queue_conn(struct mrpc_connection *conn)
{
	struct mrpc_conn_set *set=conn->set;

	if (conn_is_plugged(conn) || g_queue_is_empty(conn->events) ||
				conn->lh_event_conns != NULL)
		return;
	if (g_queue_is_empty(set->event_conns))
		selfpipe_set(set->events_notify_pipe);
	conn->lh_event_conns=g_list_append(NULL, conn);
	g_queue_push_tail_link(set->event_conns, conn->lh_event_conns);
}

/* set->events_lock must be held */
static void try_unqueue_conn(struct mrpc_connection *conn)
{
	if (conn->lh_event_conns == NULL)
		return;
	g_queue_delete_link(conn->set->event_conns, conn->lh_event_conns);
	conn->lh_event_conns=NULL;
	if (g_queue_is_empty(conn->set->event_conns))
		selfpipe_clear(conn->set->events_notify_pipe);
}

void queue_event(struct mrpc_event *event)
{
	struct mrpc_connection *conn=event->conn;

	pthread_mutex_lock(&conn->set->events_lock);
	g_queue_push_tail(conn->events, event);
	try_queue_conn(conn);
	pthread_mutex_unlock(&conn->set->events_lock);
}

static struct mrpc_event *unqueue_event(struct mrpc_conn_set *set)
{
	struct mrpc_connection *conn;
	struct mrpc_event *event=NULL;

	pthread_mutex_lock(&set->events_lock);
	conn=g_queue_pop_head(set->event_conns);
	if (conn == NULL) {
		selfpipe_clear(set->events_notify_pipe);
	} else {
		conn->lh_event_conns=NULL;
		if (g_queue_is_empty(set->event_conns))
			selfpipe_clear(set->events_notify_pipe);
		event=g_queue_pop_head(conn->events);
		assert(event != NULL);
		conn->plugged_event=event;
	}
	pthread_mutex_unlock(&set->events_lock);
	return event;
}

static mrpc_status_t _mrpc_unplug_event(struct mrpc_connection *conn,
			struct mrpc_event *event)
{
	pthread_mutex_lock(&conn->set->events_lock);
	if (conn->plugged_event == NULL || conn->plugged_event != event) {
		pthread_mutex_unlock(&conn->set->events_lock);
		return MINIRPC_INVALID_ARGUMENT;
	}
	assert(conn->lh_event_conns == NULL);
	conn->plugged_event=NULL;
	try_queue_conn(conn);
	pthread_mutex_unlock(&conn->set->events_lock);
	return MINIRPC_OK;
}

static mrpc_status_t mrpc_unplug_event(struct mrpc_event *event)
{
	return _mrpc_unplug_event(event->conn, event);
}

exported mrpc_status_t mrpc_unplug_message(struct mrpc_message *msg)
{
	return _mrpc_unplug_event(msg->conn, msg->event);
}

/* Will not affect events already in processing */
exported mrpc_status_t mrpc_plug_conn(struct mrpc_connection *conn)
{
	pthread_mutex_lock(&conn->set->events_lock);
	conn->plugged_user++;
	try_unqueue_conn(conn);
	pthread_mutex_unlock(&conn->set->events_lock);
	return MINIRPC_OK;
}

exported mrpc_status_t mrpc_unplug_conn(struct mrpc_connection *conn)
{
	mrpc_status_t ret=MINIRPC_OK;

	pthread_mutex_lock(&conn->set->events_lock);
	if (conn->plugged_user) {
		conn->plugged_user--;
		try_queue_conn(conn);
	} else {
		ret=MINIRPC_INVALID_ARGUMENT;
	}
	pthread_mutex_unlock(&conn->set->events_lock);
	return ret;
}

/* Produce a copy of the notify fd.  This ensures that the application can
   never inadvertently cause SIGPIPE by closing the read end of the notify
   pipe, and also ensures that we don't close the fd out from under the
   application when the connection set is destroyed.  The application must
   close the fd when done with it.  The application must not read or write
   the fd, only poll on it.  When an event is ready to be processed, the
   fd will be readable.  Returns -errno on error. */
exported int mrpc_get_event_fd(struct mrpc_conn_set *set)
{
	int fd;

	fd=dup(selfpipe_fd(set->events_notify_pipe));
	if (fd == -1)
		return -errno;
	return fd;
}

static void fail_request(struct mrpc_message *request, mrpc_status_t err)
{
	mrpc_unplug_message(request);
	if (request->hdr.cmd >= 0) {
		if (mrpc_send_reply_error(request->conn->set->conf.protocol,
					request, err))
			mrpc_free_message(request);
	} else {
		mrpc_free_message(request);
	}
}

/* XXX notifications to application */
static void dispatch_request(struct mrpc_event *event)
{
	struct mrpc_connection *conn=event->conn;
	struct mrpc_message *request=event->msg;
	void *request_data;
	void *reply_data=NULL;
	mrpc_status_t ret;
	mrpc_status_t result=MINIRPC_PROCEDURE_UNAVAIL;
	xdrproc_t request_type;
	xdrproc_t reply_type;
	unsigned reply_size;
	int doreply;

	assert(request->hdr.status == MINIRPC_PENDING);

	if (conn->set->conf.protocol->receiver_request_info(request->hdr.cmd,
				&request_type, NULL)) {
		/* Unknown opcode */
		fail_request(request, MINIRPC_PROCEDURE_UNAVAIL);
		return;
	}

	doreply=(request->hdr.cmd >= 0);
	if (doreply) {
		if (conn->set->conf.protocol->
					receiver_reply_info(request->hdr.cmd,
					&reply_type, &reply_size)) {
			/* Can't happen if the info tables are well-formed */
			fail_request(request, MINIRPC_ENCODING_ERR);
			return;
		}
		reply_data=mrpc_alloc_argument(reply_size);
	}
	ret=unformat_request(request, &request_data);
	if (ret) {
		/* Invalid datalen, etc. */
		fail_request(request, ret);
		mrpc_free_argument(NULL, reply_data);
		return;
	}
	/* We don't need the serialized request data anymore.  The request
	   struct may stay around for a while, so free up some memory. */
	mrpc_free_message_data(request);

	pthread_mutex_lock(&conn->operations_lock);
	if (conn->set->conf.protocol->request != NULL)
		result=conn->set->conf.protocol->request(conn->operations,
					conn->private, request,
					request->hdr.cmd, request_data,
					reply_data);
	/* Note: if the application returned MINIRPC_PENDING and then
	   immediately sent its reply from another thread, the request has
	   already been freed.  So, if result == MINIRPC_PENDING, we can't
	   access @request anymore. */
	pthread_mutex_unlock(&conn->operations_lock);
	mrpc_unplug_event(event);
	mrpc_free_argument(request_type, request_data);

	if (doreply) {
		if (result == MINIRPC_PENDING) {
			mrpc_free_argument(reply_type, reply_data);
			return;
		}
		if (result)
			ret=mrpc_send_reply_error(conn->set->conf.protocol,
						request, result);
		else
			ret=mrpc_send_reply(conn->set->conf.protocol, request,
						reply_data);
		mrpc_free_argument(reply_type, reply_data);
		if (ret) {
			/* XXX reply failed! */
			mrpc_free_message(request);
		}
	} else {
		mrpc_free_message(request);
	}
}

static void run_reply_callback(struct mrpc_event *event)
{
	struct mrpc_message *reply=event->msg;
	long_reply_callback_fn *longfn=event->callback;
	short_reply_callback_fn *shortfn=event->callback;
	void *out=NULL;
	unsigned size;
	mrpc_status_t ret;

	ret=reply->conn->set->conf.protocol->sender_reply_info(reply->hdr.cmd,
				NULL, &size);
	if (ret) {
		/* XXX */
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
		longfn(reply->conn->private, event->private, reply, ret, out);
	else
		shortfn(reply->conn->private, event->private, reply, ret);
	mrpc_free_message(reply);
}

static void dispatch_event(struct mrpc_event *event)
{
	struct mrpc_connection *conn=event->conn;
	struct mrpc_config *conf=&conn->set->conf;
	struct mrpc_event *nevent;
	int squash;
	enum mrpc_disc_reason reason;

	pthread_mutex_lock(&conn->shutdown_lock);
	squash=conn->shutdown_flags & SHUT_SQUASH_EVENTS;
	reason=conn->disc_reason;
	pthread_mutex_unlock(&conn->shutdown_lock);

	if (squash) {
		switch (event->type) {
		case EVENT_REQUEST:
		case EVENT_IOERR:
			destroy_event(event);
			return;
		case EVENT_REPLY:
			event->msg->hdr.status=MINIRPC_NETWORK_FAILURE;
			break;
		default:
			break;
		}
	}

	switch (event->type) {
	case EVENT_ACCEPT:
		assert(conf->accept != NULL);
		conn->private=conf->accept(conn->set->private, conn,
					event->addr, event->addrlen);
		g_free(event->addr);
		break;
	case EVENT_REQUEST:
		dispatch_request(event);
		break;
	case EVENT_REPLY:
		run_reply_callback(event);
		break;
	case EVENT_DISCONNECT:
		if (conf->disconnect)
			conf->disconnect(conn->private, reason);
		mrpc_conn_free(conn);
		break;
	case EVENT_IOERR:
		if (conf->ioerr)
			conf->ioerr(conn->private, event->errstring);
		free(event->errstring);
		break;
	case EVENT_Q_SHUTDOWN:
		/* The send queue is empty and the event queue has been
		   drained.  Queue events to wake all pending waiters, then
		   a DISCONNECT event to close out the conn. */
		pending_kill(conn);
		nevent=mrpc_alloc_event(conn, EVENT_DISCONNECT);
		queue_event(nevent);
		break;
	default:
		assert(0);
	}
	if (event->type != EVENT_DISCONNECT)
		mrpc_unplug_event(event);
	g_slice_free(struct mrpc_event, event);
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

exported int mrpc_dispatch_one(struct mrpc_conn_set *set)
{
	struct mrpc_event *event;

	event=unqueue_event(set);
	if (event != NULL)
		dispatch_event(event);
	return (event != NULL);
}

exported int mrpc_dispatch_all(struct mrpc_conn_set *set)
{
	int i;

	for (i=0; mrpc_dispatch_one(set); i++);
	return i;
}

exported int mrpc_dispatch_loop(struct mrpc_conn_set *set)
{
	struct pollset *pset;
	struct mrpc_event *event;
	int ret;

	pthread_mutex_lock(&set->events_lock);
	set->events_threads++;
	pthread_cond_broadcast(&set->events_threads_cond);
	pthread_mutex_unlock(&set->events_lock);

	ret=pollset_alloc(&pset);
	if (ret)
		goto out;
	ret=pollset_add(pset, selfpipe_fd(set->events_notify_pipe),
				POLLSET_READABLE, NULL, NULL, NULL, NULL,
				NULL);
	if (ret)
		goto out;
	ret=pollset_add(pset, selfpipe_fd(set->shutdown_pipe),
				POLLSET_READABLE, NULL, NULL, NULL, NULL,
				NULL);
	if (ret)
		goto out;

	while (!selfpipe_is_set(set->shutdown_pipe)) {
		event=unqueue_event(set);
		if (event == NULL) {
			ret=pollset_poll(pset);
			if (ret)
				goto out;
		} else {
			dispatch_event(event);
		}
	}
	ret=0;

out:
	pollset_free(pset);
	pthread_mutex_lock(&set->events_lock);
	set->events_threads--;
	pthread_cond_broadcast(&set->events_threads_cond);
	pthread_mutex_unlock(&set->events_lock);
	return ret;
}
