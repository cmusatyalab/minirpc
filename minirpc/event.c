#include <sys/poll.h>
#include <unistd.h>
#include <assert.h>
#include <pthread.h>
#define MINIRPC_INTERNAL
#include "internal.h"

struct mrpc_event *mrpc_alloc_event(struct mrpc_connection *conn,
			enum event_type type)
{
	struct mrpc_event *event;
	
	event=malloc(sizeof(*event));
	if (event == NULL)
		return NULL;
	memset(event, 0, sizeof(*event));
	INIT_LIST_HEAD(&event->lh_events);
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
	if (event == NULL)
		return NULL;
	event->msg=msg;
	msg->event=event;
	return event;
}

static int empty_pipe(int fd)
{
	char buf[8];
	int count;
	ssize_t ret;
	
	for (count=0; (ret=read(fd, buf, sizeof(buf))) >= 0; count += ret);
	return count;
}

static int conn_is_plugged(struct mrpc_connection *conn)
{
	return (conn->plugged_event != NULL || conn->plugged_user != 0);
}

/* set->events_lock must be held */
static void try_queue_conn(struct mrpc_connection *conn)
{
	struct mrpc_conn_set *set=conn->set;
	
	if (conn_is_plugged(conn) || list_is_empty(&conn->events) ||
				!list_is_empty(&conn->lh_event_conns))
		return;
	if (list_is_empty(&set->event_conns))
		write(set->events_notify_pipe[1], "a", 1);
	list_add_tail(&conn->lh_event_conns, &set->event_conns);
}

void queue_event(struct mrpc_event *event)
{
	struct mrpc_connection *conn=event->conn;
	
	assert(list_is_empty(&event->lh_events));
	pthread_mutex_lock(&conn->set->events_lock);
	list_add_tail(&event->lh_events, &conn->events);
	try_queue_conn(conn);
	pthread_mutex_unlock(&conn->set->events_lock);
}

static struct mrpc_event *unqueue_event(struct mrpc_conn_set *set)
{
	struct mrpc_connection *conn;
	struct mrpc_event *event=NULL;
	
	pthread_mutex_lock(&set->events_lock);
	if (list_is_empty(&set->event_conns)) {
		if (empty_pipe(set->events_notify_pipe[0]))
			/* XXX */;
	} else {
		conn=list_first_entry(&set->event_conns, struct mrpc_connection,
					lh_event_conns);
		list_del_init(&conn->lh_event_conns);
		if (list_is_empty(&set->event_conns))
			if (empty_pipe(set->events_notify_pipe[0]) != 1)
				/* XXX */;
		assert(!list_is_empty(&conn->events));
		event=list_first_entry(&conn->events, struct mrpc_event,
					lh_events);
		list_del_init(&event->lh_events);
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
	assert(list_is_empty(&conn->lh_event_conns));
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
	if (!list_is_empty(&conn->lh_event_conns))
		list_del_init(&conn->lh_event_conns);
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

/* Return a copy of the notify fd.  This ensures that the application can never
   inadvertently cause SIGPIPE by closing the read end of the notify pipe, and
   also ensures that we don't close the fd out from under the application when
   the connection set is destroyed.  The application must close the fd when
   done with it.  The application must not read or write the fd, only select()
   on it.  When an event is ready to be processed, the fd will be readable. */
exported int mrpc_get_event_fd(struct mrpc_conn_set *set)
{
	return dup(set->events_notify_pipe[0]);
}

static void fail_request(struct mrpc_message *request, mrpc_status_t err)
{
	mrpc_unplug_message(request);
	if (request->hdr.cmd >= 0) {
		if (mrpc_send_reply_error(request->conn->set->config.protocol,
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
	
	if (conn->set->config.protocol->receiver_request_info(request->hdr.cmd,
				&request_type, NULL)) {
		/* Unknown opcode */
		fail_request(request, MINIRPC_PROCEDURE_UNAVAIL);
		return;
	}
	
	doreply=(request->hdr.cmd >= 0);
	if (doreply) {
		if (conn->set->config.protocol->
					receiver_reply_info(request->hdr.cmd,
					&reply_type, &reply_size)) {
			/* Can't happen if the info tables are well-formed */
			fail_request(request, MINIRPC_ENCODING_ERR);
			return;
		}
		
		if (reply_size) {
			reply_data=malloc(reply_size);
			if (reply_data == NULL) {
				fail_request(request, MINIRPC_NOMEM);
				return;
			}
			memset(reply_data, 0, reply_size);
		}
	}
	ret=unformat_request(request, &request_data);
	if (ret) {
		/* Invalid datalen, etc. */
		fail_request(request, ret);
		cond_free(reply_data);
		return;
	}
	/* We don't need the serialized request data anymore.  The request
	   struct may stay around for a while, so free up some memory. */
	cond_free(request->data);
	request->data=NULL;
	
	pthread_mutex_lock(&conn->operations_lock);
	if (conn->set->config.protocol->request != NULL)
		result=conn->set->config.protocol->request(conn->operations,
					conn->private, request,
					request->hdr.cmd, request_data,
					reply_data);
	/* Note: if the application returned MINIRPC_PENDING and then
	   immediately sent its reply from another thread, the request has
	   already been freed.  So, if result == MINIRPC_PENDING, we can't
	   access @request anymore. */
	pthread_mutex_unlock(&conn->operations_lock);
	mrpc_unplug_event(event);
	xdr_free(request_type, request_data);
	cond_free(request_data);
	
	if (doreply) {
		if (result == MINIRPC_PENDING) {
			xdr_free(reply_type, reply_data);
			cond_free(reply_data);
			return;
		}
		if (result)
			ret=mrpc_send_reply_error(conn->set->config.protocol,
						request, result);
		else
			ret=mrpc_send_reply(conn->set->config.protocol, request,
						reply_data);
		xdr_free(reply_type, reply_data);
		cond_free(reply_data);
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
	
	ret=reply->conn->set->config.protocol->sender_reply_info(reply->hdr.cmd,
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

static void run_disconnect_method(struct mrpc_event *event)
{
	if (event->conn->set->ops->disconnect)
		event->conn->set->ops->disconnect(event->conn->private,
					event->disc_reason);
	/* XXX free conn? */
}

static void dispatch_event(struct mrpc_event *event)
{
	switch (event->type) {
	case EVENT_REQUEST:
		dispatch_request(event);
		break;
	case EVENT_REPLY:
		run_reply_callback(event);
		break;
	case EVENT_DISCONNECT:
		run_disconnect_method(event);
		break;
	case EVENT_IOERR:
		if (event->conn->set->ops->ioerr)
			event->conn->set->ops->ioerr(event->conn->private,
						event->errstring);
		break;
	default:
		assert(0);
	}
	mrpc_unplug_event(event);
	free(event);
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
	struct mrpc_event *event;
	struct pollfd poll_s[2] = {{0}};
	int ret=0;
	
	pthread_mutex_lock(&set->events_lock);
	set->events_threads++;
	pthread_cond_broadcast(&set->events_threads_cond);
	pthread_mutex_unlock(&set->events_lock);
	
	poll_s[0].fd=set->events_notify_pipe[0];
	poll_s[0].events=POLLIN;
	poll_s[1].fd=set->shutdown_pipe[0];
	poll_s[1].events=POLLIN;
	
	while (poll_s[1].revents == 0) {
		event=unqueue_event(set);
		if (event == NULL) {
			if (poll(poll_s, 2, -1) == -1 && errno != EINTR) {
				ret=-errno;
				break;
			}
		} else {
			dispatch_event(event);
		}
	}
	
	pthread_mutex_lock(&set->events_lock);
	set->events_threads--;
	pthread_cond_broadcast(&set->events_threads_cond);
	pthread_mutex_unlock(&set->events_lock);
	return ret;
}
