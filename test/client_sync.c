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

#include "common.h"

void query_int_sync(struct mrpc_connection *conn)
{
	struct IntParam request;
	struct IntParam *reply;
	mrpc_status_t ret;

	request.val=12;
	ret=proto_query_int(conn, &request, &reply);
	if (ret)
		die("query returned %d", ret);
	if (reply->val != 12)
		die("reply body contained %d", reply->val);
	free_IntParam(&request, 0);
	free_IntParam(reply, 1);
}

void call_int_sync(struct mrpc_connection *conn)
{
	struct IntParam request;
	mrpc_status_t ret;

	request.val=12;
	ret=proto_call_int(conn, &request);
	if (ret)
		die("query returned %d", ret);
	free_IntParam(&request, 0);
}

void error_sync(struct mrpc_connection *conn)
{
	struct IntParam *reply;
	mrpc_status_t ret;

	ret=proto_error(conn, &reply);
	if (ret != 1)
		die("query returned %d", ret);
}

void notify_int_sync(struct mrpc_connection *conn)
{
	struct IntParam notify;
	mrpc_status_t ret;

	notify.val=12;
	ret=proto_notify_int(conn, &notify);
	if (ret)
		die("notify returned %d", ret);
	free_IntParam(&notify, 0);
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
