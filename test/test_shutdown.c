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

#include <semaphore.h>
#include <pthread.h>
#include <glib.h>
#include <assert.h>
#include "common.h"

sem_t ready;
sem_t complete;
sem_t can_free;
struct mrpc_message *last_request;

mrpc_status_t do_ping(void *conn_data, struct mrpc_message *msg)
{
	g_atomic_pointer_set(&last_request, msg);
	return MINIRPC_PENDING;
}

const struct proto_server_operations ops = {
	.ping = do_ping
};

void *do_accept(void *set_data, struct mrpc_connection *conn,
			struct sockaddr *from, socklen_t from_len)
{
	if (proto_server_set_operations(conn, &ops))
		die("Error setting operations struct");
	return conn;
}

void ping_cb(void *conn_private, void *msg_private, mrpc_status_t status)
{
	if (status != MINIRPC_NETWORK_FAILURE)
		die("Async ping received %d", status);
	sem_post(&complete);
}

void *do_sync_ping(void *connp)
{
	struct mrpc_connection *conn=connp;
	mrpc_status_t ret;

	sem_post(&ready);
	ret=proto_ping(conn);
	if (ret != MINIRPC_NETWORK_FAILURE)
		die("Sync ping received %d", ret);
	sem_post(&complete);
	return NULL;
}

void server_disconnect(void *conn_data, enum mrpc_disc_reason reason)
{
	struct mrpc_message *msg;

	msg=g_atomic_pointer_get(&last_request);
	assert(msg != NULL);
	expect(proto_ping_send_async_reply(msg), MINIRPC_NETWORK_FAILURE);
	disconnect_normal(conn_data, reason);
}

void client_disconnect(void *conn_data, enum mrpc_disc_reason reason)
{
	sem_wait(&can_free);
	disconnect_user(conn_data, reason);
}

int main(int argc, char **argv)
{
	struct mrpc_conn_set *sset;
	struct mrpc_conn_set *cset;
	struct mrpc_connection *conn;
	unsigned port;
	int ret;
	int i;
	pthread_t thr;

	expect(sem_init(&ready, 0, 0), 0);
	expect(sem_init(&complete, 0, 0), 0);
	expect(sem_init(&can_free, 0, 0), 0);
	sset=spawn_server(&port, proto_server, do_accept, NULL, 1);
	mrpc_set_disconnect_func(sset, server_disconnect);

	if (mrpc_conn_set_create(&cset, proto_client, NULL))
		die("Couldn't allocate conn set");
	mrpc_set_disconnect_func(cset, client_disconnect);
	mrpc_start_dispatch_thread(cset);

	ret=mrpc_conn_create(&conn, cset, NULL);
	if (ret)
		die("%s", strerror(ret));
	ret=mrpc_connect(conn, "localhost", port);
	if (ret)
		die("%s", strerror(ret));

	expect(proto_ping_async(conn, ping_cb, NULL), MINIRPC_OK);
	pthread_create(&thr, NULL, do_sync_ping, conn);
	expect(pthread_detach(thr), 0);
	sem_wait(&ready);
	expect(mrpc_conn_close(conn), 0);
	expect(proto_ping(conn), MINIRPC_NETWORK_FAILURE);
	expect(mrpc_conn_close(conn), EALREADY);
	for (i=0; i<2; i++)
		sem_wait(&complete);
	sem_post(&can_free);
	mrpc_conn_set_unref(cset);
	mrpc_listen_close(sset);
	mrpc_conn_set_unref(sset);
	expect_disconnects(1, 1, 0);
	return 0;
}
