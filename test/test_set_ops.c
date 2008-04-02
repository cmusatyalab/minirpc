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

#define ITERS 25

#include "common.h"

const struct proto_server_operations ops_ok;
const struct proto_server_operations ops_fail;

mrpc_status_t do_ping_ok(void *conn_data, struct mrpc_message *msg)
{
	expect(proto_server_set_operations(conn_data, &ops_fail), 0);
	return MINIRPC_OK;
}

mrpc_status_t do_ping_fail(void *conn_data, struct mrpc_message *msg)
{
	expect(proto_server_set_operations(conn_data, &ops_ok), 0);
	return 1;
}

const struct proto_server_operations ops_ok = {
	.ping = do_ping_ok
};

const struct proto_server_operations ops_fail = {
	.ping = do_ping_fail
};

void *do_accept(void *set_data, struct mrpc_connection *conn,
			struct sockaddr *from, socklen_t from_len)
{
	if (proto_server_set_operations(conn, &ops_ok))
		die("Error setting operations struct");
	return conn;
}

int main(int argc, char **argv)
{
	struct mrpc_conn_set *sset;
	struct mrpc_conn_set *cset;
	struct mrpc_connection *conn;
	unsigned port;
	int ret;
	int i;

	sset=spawn_server(&port, proto_server, do_accept, NULL, 1);
	mrpc_set_disconnect_func(sset, disconnect_normal);

	if (mrpc_conn_set_create(&cset, proto_client, NULL))
		die("Couldn't allocate conn set");
	mrpc_set_disconnect_func(cset, disconnect_user);
	mrpc_start_dispatch_thread(cset);

	ret=mrpc_conn_create(&conn, cset, NULL);
	if (ret)
		die("%s", strerror(ret));
	ret=mrpc_connect(conn, "localhost", port);
	if (ret)
		die("%s", strerror(ret));

	for (i=0; i<ITERS; i++) {
		expect(proto_ping(conn), 0);
		expect(proto_ping(conn), 1);
	}

	mrpc_conn_close(conn);
	mrpc_conn_set_unref(cset);
	mrpc_listen_close(sset);
	mrpc_conn_set_unref(sset);
	expect_disconnects(1, 1, 0);
	return 0;
}
