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
	return MINIRPC_OK;
}

static mrpc_status_t do_loop_int(void *conn_data, struct mrpc_message *msg,
			IntParam *in, IntParam *out)
{
	out->val=in->val;
	return MINIRPC_OK;
}

static mrpc_status_t do_check_int(void *conn_data, struct mrpc_message *msg,
			IntParam *req)
{
	if (req->val == INT_VALUE)
		return MINIRPC_OK;
	else
		return 1;
}

static mrpc_status_t do_error(void *conn_data, struct mrpc_message *msg,
			IntParam *out)
{
	return 1;
}

static mrpc_status_t do_invalidate_ops(void *conn_data,
			struct mrpc_message *msg)
{
	if (proto_server_set_operations(conn_data, NULL))
		die("Couldn't set operations");
	return MINIRPC_OK;
}

static void do_notify(void *conn_data, struct mrpc_message *msg,
			CondVarPtr *req)
{
	pthread_cond_t *cond = (void*)(unsigned long)req->cond;
	pthread_mutex_t *lock = (void*)(unsigned long)req->mutex;

	pthread_mutex_lock(lock);
	pthread_cond_broadcast(cond);
	pthread_mutex_unlock(lock);
}

static const struct proto_server_operations ops = {
	.ping = do_ping,
	.loop_int = do_loop_int,
	.check_int = do_check_int,
	.error = do_error,
	.invalidate_ops = do_invalidate_ops,
	.notify = do_notify,
};

void *sync_server_accept(void *set_data, struct mrpc_connection *conn,
			struct sockaddr *from, socklen_t from_len)
{
	message("New connection");
	if (proto_server_set_operations(conn, &ops))
		message("Error setting operations struct");
	return conn;
}
