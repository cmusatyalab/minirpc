/*
 * miniRPC - TCP RPC library with asynchronous operations
 *
 * Copyright (C) 2007-2008 Carnegie Mellon University
 *
 * This software is distributed under the terms of the Eclipse Public License,
 * Version 1.0 which can be found in the file named LICENSE.  ANY USE,
 * REPRODUCTION OR DISTRIBUTION OF THIS SOFTWARE CONSTITUTES RECIPIENT'S
 * ACCEPTANCE OF THIS AGREEMENT
 */

#include "common.h"

int main(int argc, char **argv)
{
	struct mrpc_conn_set *sset;
	struct mrpc_conn_set *cset;
	struct mrpc_connection *conn;
	unsigned port;
	int ret;
	int i;

	sset=spawn_server(&port, proto_server, sync_server_accept, NULL, 1);
	mrpc_set_disconnect_func(sset, disconnect_normal);

	if (mrpc_conn_set_create(&cset, proto_client, NULL))
		die("Couldn't create conn set");
	mrpc_set_disconnect_func(cset, disconnect_user);
	start_monitored_dispatcher(cset);

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
		mrpc_conn_unref(conn);
	}
	mrpc_conn_set_unref(cset);

	/* Try repeated connections from different conn sets */
	for (i=0; i<100; i++) {
		if (mrpc_conn_set_create(&cset, proto_client, NULL))
			die("Couldn't create conn set");
		mrpc_set_disconnect_func(cset, disconnect_user);
		start_monitored_dispatcher(cset);

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
		mrpc_conn_unref(conn);
		mrpc_conn_set_unref(cset);
	}

	mrpc_listen_close(sset);
	mrpc_conn_set_unref(sset);
	expect_disconnects(600, 600, 0);
	return 0;
}
