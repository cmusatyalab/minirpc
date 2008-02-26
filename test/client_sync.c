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

#include <time.h>
#include "common.h"

void loop_int_sync(struct mrpc_connection *conn)
{
	struct IntParam request;
	struct IntParam *reply;
	mrpc_status_t ret;

	request.val=INT_VALUE;
	ret=proto_loop_int(conn, &request, &reply);
	if (ret)
		die("Loop returned %d", ret);
	if (reply->val != INT_VALUE)
		die("Reply body contained %d", reply->val);
	free_IntParam(&request, 0);
	free_IntParam(reply, 1);
}

void check_int_sync(struct mrpc_connection *conn)
{
	struct IntParam request;
	mrpc_status_t ret;

	request.val=INT_VALUE;
	ret=proto_check_int(conn, &request);
	if (ret)
		die("Check returned %d", ret);
	free_IntParam(&request, 0);

	request.val=12;
	ret=proto_check_int(conn, &request);
	if (ret != 1)
		die("Failed check returned %d", ret);
	free_IntParam(&request, 0);
}

void error_sync(struct mrpc_connection *conn)
{
	struct IntParam *reply;
	mrpc_status_t ret;

	ret=proto_error(conn, &reply);
	if (ret != 1)
		die("Error returned %d", ret);
}

void notify_sync(struct mrpc_connection *conn)
{
	struct CondVarPtr notify;
	struct timespec ts = {0};
	pthread_mutex_t lock;
	pthread_cond_t cond;
	mrpc_status_t ret;
	int rval;

	pthread_mutex_init(&lock, NULL);
	pthread_cond_init(&cond, NULL);
	notify.mutex=(unsigned long)&lock;
	notify.cond=(unsigned long)&cond;
	pthread_mutex_lock(&lock);
	ret=proto_notify(conn, &notify);
	if (ret)
		die("Notify returned %d", ret);
	free_CondVarPtr(&notify, 0);
	ts.tv_sec=time(NULL) + 5;
	rval=pthread_cond_timedwait(&cond, &lock, &ts);
	if (rval == ETIMEDOUT)
		die("Timed out waiting for notify completion");
	else if (rval)
		die("Condition variable wait failed");
	pthread_mutex_unlock(&lock);
}

void invalidate(struct mrpc_connection *conn)
{
	int ret;

	ret=proto_ping(conn);
	if (ret)
		die("Ping returned %d", ret);
	ret=proto_invalidate_ops(conn);
	if (ret)
		die("Invalidate returned %d", ret);
	ret=proto_ping(conn);
	if (ret != MINIRPC_PROCEDURE_UNAVAIL)
		die("Ping returned %d", ret);
}
