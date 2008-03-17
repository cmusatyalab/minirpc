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
	.disconnect = disconnect_user,
	.msg_max_buf_len = 128
};

static const struct mrpc_config server_config = {
	.protocol = &proto_server,
	.accept = sync_server_accept,
	.disconnect = disconnect_normal,
	.msg_max_buf_len = 128
};

int main(int argc, char **argv)
{
	struct mrpc_conn_set *sset;
	struct mrpc_conn_set *cset;
	struct mrpc_connection *conn;
	unsigned port;
	int ret;

	if (mrpc_init())
		die("Couldn't initialize minirpc");
	sset=spawn_server(&port, &server_config, NULL, 1);
	if (mrpc_conn_set_create(&cset, &client_config, NULL))
		die("Couldn't allocate conn set");

	ret=mrpc_conn_create(&conn, cset, NULL);
	if (ret)
		die("%s", strerror(ret));
	ret=mrpc_connect(conn, "localhost", port);
	if (ret)
		die("%s", strerror(ret));

	mrpc_start_dispatch_thread(cset);
	expect(send_buffer_sync(conn), MINIRPC_ENCODING_ERR);
	expect(proto_ping(conn), 0);
	expect(recv_buffer_sync(conn), MINIRPC_ENCODING_ERR);
	expect(proto_ping(conn), 0);
	msg_buffer_sync(conn);
	expect(proto_ping(conn), 0);
	mrpc_conn_set_destroy(cset);
	mrpc_conn_set_destroy(sset);
	expect_disconnects(1, 1, 0);
	return 0;
}
