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

static const struct mrpc_config client_config = {
	.protocol = &proto_client,
	.disconnect = disconnect_fatal
};

static const struct mrpc_config server_config = {
	.protocol = &proto_server,
	.accept = sync_server_accept,
	.disconnect = disconnect_fatal
};

int main(int argc, char **argv)
{
	struct mrpc_conn_set *sset;
	struct mrpc_conn_set *cset;
	struct mrpc_connection *conn;
	int port;
	int ret;

	sset=spawn_server(&port, &server_config, NULL, 1);

	if (mrpc_conn_set_alloc(&cset, &client_config, NULL))
		die("Couldn't allocate conn set");

	ret=mrpc_connect(&conn, cset, "localhost", port, NULL);
	if (ret)
		die("%s", strerror(-ret));

	launch_dispatch_thread(cset);
	sync_client_set_ops(conn);
	sync_client_run(conn);
	trigger_callback_sync(conn);
	invalidate_sync(conn);
	return 0;
}
