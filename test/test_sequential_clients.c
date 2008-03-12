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

	if (mrpc_conn_set_create(&cset, &client_config, NULL))
		die("Couldn't create conn set");
	mrpc_start_dispatch_thread(cset);

	/* Try repeated connections from the same conn set */
	for (i=0; i<500; i++) {
		ret=mrpc_conn_create(&conn, cset, NULL);
		if (ret)
			die("%s in mrpc_conn_create() on iteration %d",
						strerror(ret), i);
		ret=mrpc_connect(conn, "localhost", port);
		if (ret)
			die("%s in mrpc_connect() on iteration %d",
						strerror(ret), i);
		sync_client_set_ops(conn);
		sync_client_run(conn);
		mrpc_conn_close(conn);
	}
	mrpc_conn_set_destroy(cset);
	expect_disconnects(500, -1, 0);

	/* Try repeated connections from different conn sets */
	for (i=0; i<500; i++) {
		if (mrpc_conn_set_create(&cset, &client_config, NULL))
			die("Couldn't create conn set");
		mrpc_start_dispatch_thread(cset);

		ret=mrpc_conn_create(&conn, cset, NULL);
		if (ret)
			die("%s in mrpc_conn_create() on iteration %d",
						strerror(ret), i);
		ret=mrpc_connect(conn, "localhost", port);
		if (ret)
			die("%s in mrpc_connect() on iteration %d",
						strerror(ret), i);
		sync_client_set_ops(conn);
		sync_client_run(conn);
		mrpc_conn_set_destroy(cset);
	}

	mrpc_conn_set_destroy(sset);
	expect_disconnects(1000, 1000, 0);
	return 0;
}
