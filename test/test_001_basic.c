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

static void disconnect(void *conn_data, enum mrpc_disc_reason reason)
{
	message("Disconnect: %d", reason);
}

static const struct mrpc_config client_config = {
	.protocol = &proto_client
};

static const struct mrpc_set_operations client_ops = {
	.disconnect = disconnect
};

static const struct mrpc_config server_config = {
	.protocol = &proto_server
};

static const struct mrpc_set_operations server_ops = {
	.accept = sync_server_accept,
	.disconnect = disconnect
};

int main(int argc, char **argv)
{
	struct mrpc_conn_set *sset;
	struct mrpc_conn_set *cset;
	struct mrpc_connection *conn;
	int port;
	int ret;

	sset=spawn_server(&port, &server_config, &server_ops, NULL, 1);

	if (mrpc_conn_set_alloc(&cset, &client_config, &client_ops, NULL))
		die("Couldn't allocate conn set");

	ret=mrpc_connect(&conn, cset, "localhost", port, NULL);
	if (ret)
		die("%s", strerror(-ret));

	loop_int_sync(conn);
	error_sync(conn);
	check_int_sync(conn);
	notify_sync(conn);
	invalidate(conn);
	return 0;
}
