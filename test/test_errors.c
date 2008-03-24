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

mrpc_status_t do_ping(void *conn_data, struct mrpc_message *msg)
{
	expect(proto_client_check_int_send_async_reply(msg),
				MINIRPC_INVALID_PROTOCOL);
	expect(proto_client_check_int_send_async_reply_error(msg, 1),
				MINIRPC_INVALID_PROTOCOL);
	expect(proto_ping_send_async_reply_error(msg, 0),
				MINIRPC_INVALID_ARGUMENT);
	expect(proto_check_int_send_async_reply(msg),
				MINIRPC_INVALID_ARGUMENT);
	expect(proto_check_int_send_async_reply_error(msg, 1),
				MINIRPC_INVALID_ARGUMENT);
	expect(proto_ping_send_async_reply(msg), 0);
	return MINIRPC_PENDING;
}

const struct proto_server_operations probe_ops = {
	.ping = do_ping
};

void *probe_server_accept(void *set_data, struct mrpc_connection *conn,
			struct sockaddr *from, socklen_t from_len)
{
	if (proto_server_set_operations(conn, &probe_ops))
		die("Error setting operations struct");
	return conn;
}

static const struct mrpc_config client_config = {
	.protocol = &proto_client,
	.disconnect = disconnect_user
};

static const struct mrpc_config server_config = {
	.protocol = &proto_server,
	.accept = sync_server_accept,
	.disconnect = disconnect_normal
};

static const struct mrpc_config probe_server_config = {
	.protocol = &proto_server,
	.accept = probe_server_accept,
	.disconnect = disconnect_normal
};

int main(int argc, char **argv)
{
	struct mrpc_conn_set *sset;
	struct mrpc_conn_set *cset;
	struct mrpc_connection *conn;
	struct sockaddr_in addr;
	unsigned port;
	int fd;

	if (mrpc_init())
		die("Couldn't initialize minirpc");

	expect(mrpc_conn_set_create(NULL, &server_config, NULL), EINVAL);
	expect(mrpc_conn_set_create(&sset, NULL, NULL), EINVAL);
	mrpc_conn_set_destroy(NULL);
	expect(mrpc_start_dispatch_thread(NULL), EINVAL);
	mrpc_dispatcher_add(NULL);
	mrpc_dispatcher_remove(NULL);
	expect(mrpc_dispatch_loop(NULL), EINVAL);
	expect(mrpc_dispatch(NULL, 0), EINVAL);
	expect(mrpc_plug_conn(NULL), EINVAL);
	expect(mrpc_unplug_conn(NULL), EINVAL);
	expect(mrpc_unplug_message(NULL), EINVAL);

	if (mrpc_conn_set_create(&sset, &server_config, NULL))
		die("Couldn't allocate conn set");
	if (mrpc_conn_set_create(&cset, &client_config, NULL))
		die("Couldn't allocate conn set");
	if (mrpc_start_dispatch_thread(sset))
		die("Couldn't start server dispatcher");
	if (mrpc_start_dispatch_thread(cset))
		die("Couldn't start client dispatcher");

	port=0;
	expect(mrpc_listen(NULL, "localhost", &port), EINVAL);
	expect(mrpc_listen(sset, "localhost", NULL), EINVAL);
	port=0;
	expect(mrpc_listen(sset, NULL, &port), 0);
	expect(mrpc_conn_create(&conn, cset, NULL), 0);
	expect(mrpc_connect(conn, NULL, port), 0);
	mrpc_listen_close(NULL);
	mrpc_listen_close(sset);
	expect(mrpc_conn_close(conn), 0);
	expect(mrpc_conn_close(NULL), EINVAL);
	expect(mrpc_conn_create(&conn, cset, NULL), 0);
	expect(mrpc_connect(conn, NULL, port), ECONNREFUSED);
	expect(mrpc_conn_close(conn), 0);

	port=0;
	expect(mrpc_listen(sset, "localhost", &port), 0);
	expect(mrpc_conn_create(NULL, cset, NULL), EINVAL);
	expect(mrpc_conn_create(&conn, NULL, NULL), EINVAL);
	expect(mrpc_conn_create(&conn, cset, NULL), 0);
	expect(mrpc_connect(NULL, "localhost", port), EINVAL);
	expect(mrpc_connect(conn, "localhost", 0), ECONNREFUSED);

	fd=socket(PF_INET, SOCK_STREAM, 0);
	if (fd == -1)
		die("Couldn't create socket");
	addr.sin_family=AF_INET;
	addr.sin_port=htons(port);
	addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
	expect(connect(fd, (struct sockaddr *)&addr, sizeof(addr)), 0);
	expect(mrpc_bind_fd(NULL, fd), EINVAL);
	expect(mrpc_bind_fd(conn, fd), 0);
	expect(mrpc_connect(conn, NULL, port), EINVAL);
	expect(mrpc_bind_fd(conn, fd), EINVAL);
	expect(mrpc_conn_close(conn), 0);
	expect(mrpc_conn_create(&conn, cset, NULL), 0);
	fd=socket(PF_INET, SOCK_STREAM, 0);
	if (fd == -1)
		die("Couldn't create socket");
	expect(listen(fd, 16), 0);
	expect(mrpc_bind_fd(conn, fd), EINVAL);
	close(fd);
	expect(mrpc_bind_fd(conn, 0), ENOTSOCK);

	expect(mrpc_connect(conn, NULL, port), 0);
	expect(proto_client_set_operations(NULL, NULL), EINVAL);
	expect(proto_ping(NULL), MINIRPC_INVALID_ARGUMENT);
	expect(proto_ping_async(conn, NULL, NULL), MINIRPC_INVALID_ARGUMENT);
	expect(proto_notify(NULL, NULL), MINIRPC_INVALID_ARGUMENT);
	expect(proto_ping_send_async_reply(NULL), MINIRPC_INVALID_ARGUMENT);
	expect(proto_ping_send_async_reply_error(NULL, 0),
				MINIRPC_INVALID_ARGUMENT);
	expect(proto_client_check_int(conn, NULL), MINIRPC_INVALID_PROTOCOL);
	expect(proto_client_check_int_async(conn,
				(proto_client_check_int_callback_fn*)1,
				NULL, NULL), MINIRPC_INVALID_PROTOCOL);
	expect(mrpc_conn_close(conn), 0);

	expect(mrpc_conn_create(&conn, cset, NULL), 0);
	expect(proto_ping(conn), MINIRPC_INVALID_ARGUMENT);
	mrpc_conn_set_destroy(cset);
	mrpc_conn_set_destroy(sset);

	sset=spawn_server(&port, &probe_server_config, NULL, 1);
	if (mrpc_conn_set_create(&cset, &client_config, NULL))
		die("Couldn't allocate conn set");
	if (mrpc_start_dispatch_thread(cset))
		die("Couldn't start client dispatcher");
	expect(mrpc_conn_create(&conn, cset, NULL), 0);
	expect(mrpc_connect(conn, NULL, port), 0);
	expect(proto_ping(conn), 0);
	mrpc_conn_set_destroy(cset);
	mrpc_conn_set_destroy(sset);

	return 0;
}
