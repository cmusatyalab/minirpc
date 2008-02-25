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

static mrpc_status_t do_ping(void *conn_data, struct mrpc_message *msg)
{
	message("Received ping");
	return MINIRPC_OK;
}

static mrpc_status_t do_query_int(void *conn_data, struct mrpc_message *msg,
			IntParam *in, IntParam *out)
{
	message("Query, value %d", in->val);
	out->val=in->val;
	return MINIRPC_OK;
}

static mrpc_status_t do_call_int(void *conn_data, struct mrpc_message *msg,
			IntParam *req)
{
	message("Received call(): %d", req->val);
	return MINIRPC_OK;
}

static mrpc_status_t do_error(void *conn_data, struct mrpc_message *msg,
			IntParam *out)
{
	message("Received error call");
	return 1;
}

static mrpc_status_t do_invalidate_ops(void *conn_data,
			struct mrpc_message *msg)
{
	if (proto_server_set_operations(conn_data, NULL))
		message("Couldn't set operations");
	return MINIRPC_OK;
}

static void do_notify(void *conn_data, struct mrpc_message *msg, IntParam *req)
{
	message("Received notify(): %d", req->val);
}

static const struct proto_server_operations ops = {
	.ping = do_ping,
	.query_int = do_query_int,
	.call_int = do_call_int,
	.error = do_error,
	.invalidate_ops = do_invalidate_ops,
	.notify_int = do_notify,
};

void *sync_server_accept(void *set_data, struct mrpc_connection *conn,
			struct sockaddr *from, socklen_t from_len)
{
	message("New connection");
	if (proto_server_set_operations(conn, &ops))
		message("Error setting operations struct");
	return conn;
}
