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
	.disconnect = disconnect_user
};

static const struct mrpc_config server_config = {
	.protocol = &proto_server,
	.accept = sync_server_accept,
	.disconnect = disconnect_normal
};

int main(int argc, char **argv)
{
	struct mrpc_conn_set *sset;
	struct mrpc_conn_set *cset;
	struct mrpc_connection *conn;
	int port;
	int ret;
	int i;

	if (mrpc_init())
		die("Couldn't initialize minirpc");
	sset=spawn_server(&port, &server_config, NULL, 1);

	if (mrpc_conn_set_alloc(&cset, &client_config, NULL))
		die("Couldn't allocate conn set");
	launch_dispatch_thread(cset);

	for (i=0; i<1000; i++) {
		ret=mrpc_connect(&conn, cset, "localhost", port, NULL);
		if (ret)
			die("%s", strerror(-ret));
		sync_client_set_ops(conn);
		sync_client_run(conn);
		mrpc_conn_close(conn);
	}
	return 0;
}
