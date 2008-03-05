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

#include <unistd.h>
#include "common.h"

static const struct mrpc_config client_config = {
	.protocol = &proto_client,
	.disconnect = disconnect_user
};

static const struct mrpc_config server_config = {
	.protocol = &proto_server,
	.accept = async_server_accept,
	.disconnect = disconnect_normal
};

int main(int argc, char **argv)
{
	struct mrpc_conn_set *sset;
	struct mrpc_conn_set *cset;
	struct mrpc_connection *conn;
	int port;
	int ret;

	if (mrpc_init())
		die("Couldn't initialize minirpc");
	async_server_init();
	async_client_init();
	sset=spawn_server(&port, &server_config, NULL, 1);

	if (mrpc_conn_set_create(&cset, &client_config, NULL))
		die("Couldn't allocate conn set");

	ret=mrpc_connect(&conn, cset, "localhost", port, NULL);
	if (ret)
		die("%s", strerror(ret));

	mrpc_start_dispatch_thread(cset);
	async_client_set_ops(conn);
	async_client_run(conn);
	trigger_callback_sync(conn);
	/* Give the async client some additional time to notice if it receives
	   more callbacks than it should */
	sleep(1);
	mrpc_conn_close(conn);
	mrpc_conn_set_destroy(cset);
	mrpc_conn_set_destroy(sset);
	expect_disconnects(1, 1, 0);
	return 0;
}
