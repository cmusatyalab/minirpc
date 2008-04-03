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

#define ITERS 1000
#define THREADS 5

#include <sched.h>
#include <semaphore.h>
#include <unistd.h>
#include <pthread.h>
#include "common.h"

struct data {
	pthread_mutex_t lock;
	struct mrpc_connection *conn;
	int current;
	int highwater;
	int parallel;
	int blocked;
};

struct data server;
struct data client;

sem_t complete;

void handler_wrapper(struct data *data)
{
	struct mrpc_connection *conn;
	int parallel;

	pthread_mutex_lock(&data->lock);
	conn=data->conn;
	if (data->blocked) {
		if (data == &server)
			die("Request function run when events blocked");
		else
			die("Callback function run when events blocked");
	}
	expect(mrpc_start_events(conn), EINVAL);
	data->current++;
	if (data->highwater < data->current)
		data->highwater=data->current;
	parallel=data->blocked=data->parallel;
	pthread_mutex_unlock(&data->lock);

	if (parallel) {
		expect(mrpc_stop_events(conn), 0);
		expect(mrpc_release_event(), 0);
		expect(mrpc_release_event(), ENOENT);
	}
	sched_yield();
	if (parallel) {
		pthread_mutex_lock(&data->lock);
		data->blocked=0;
		pthread_mutex_unlock(&data->lock);
		expect(mrpc_start_events(conn), 0);
		sched_yield();
	}

	pthread_mutex_lock(&data->lock);
	data->current--;
	pthread_mutex_unlock(&data->lock);
}

mrpc_status_t do_ping(void *conn_data, struct mrpc_message *msg)
{
	handler_wrapper(&server);
	return MINIRPC_OK;
}

const struct proto_server_operations ops = {
	.ping = do_ping
};

void *do_accept(void *set_data, struct mrpc_connection *conn,
			struct sockaddr *from, socklen_t from_len)
{
	if (proto_server_set_operations(conn, &ops))
		die("Error setting operations struct");
	pthread_mutex_lock(&server.lock);
	server.conn=conn;
	pthread_mutex_unlock(&server.lock);
	return conn;
}

void ping_cb(void *conn_private, void *msg_private, mrpc_status_t status)
{
	expect(status, MINIRPC_OK);
	handler_wrapper(&client);
	sem_post(&complete);
}

void do_round(int cli_pl, int srv_pl)
{
	struct mrpc_connection *sconn;
	struct mrpc_connection *cconn;
	int i;

	pthread_mutex_lock(&client.lock);
	client.current=0;
	client.highwater=0;
	client.parallel=cli_pl;
	cconn=client.conn;
	pthread_mutex_unlock(&client.lock);
	pthread_mutex_lock(&server.lock);
	server.current=0;
	server.highwater=0;
	server.parallel=srv_pl;
	sconn=server.conn;
	pthread_mutex_unlock(&server.lock);

	for (i=0; i<ITERS; i++)
		expect(proto_ping_async(cconn, ping_cb, NULL), MINIRPC_OK);
	for (i=0; i<ITERS; i++)
		sem_wait(&complete);

	pthread_mutex_lock(&client.lock);
	if (cli_pl && client.highwater == 1)
		die("Expected client parallelism but saw none");
	if (!cli_pl && client.highwater > 1)
		die("Expected no client parallelism but got high water of %d",
					client.highwater);
	pthread_mutex_unlock(&client.lock);
	pthread_mutex_lock(&server.lock);
	if (srv_pl && server.highwater == 1)
		die("Expected server parallelism but saw none");
	if (!srv_pl && server.highwater > 1)
		die("Expected no server parallelism but got high water of %d",
					server.highwater);
	pthread_mutex_unlock(&server.lock);
}

int main(int argc, char **argv)
{
	struct mrpc_conn_set *sset;
	struct mrpc_conn_set *cset;
	struct mrpc_connection *conn;
	unsigned port;
	int ret;
	int i;
	int j;

	pthread_mutex_init(&server.lock, NULL);
	pthread_mutex_init(&client.lock, NULL);
	expect(sem_init(&complete, 0, 0), 0);
	sset=spawn_server(&port, proto_server, do_accept, NULL, THREADS);
	mrpc_set_disconnect_func(sset, disconnect_normal);

	if (mrpc_conn_set_create(&cset, proto_client, NULL))
		die("Couldn't allocate conn set");
	mrpc_set_disconnect_func(cset, disconnect_user);
	for (i=0; i<THREADS; i++)
		start_monitored_dispatcher(cset);

	ret=mrpc_conn_create(&conn, cset, NULL);
	if (ret)
		die("%s", strerror(ret));
	ret=mrpc_connect(conn, "localhost", port);
	if (ret)
		die("%s", strerror(ret));
	pthread_mutex_lock(&client.lock);
	client.conn=conn;
	pthread_mutex_unlock(&client.lock);

	for (i=0; i<2; i++)
		for (j=0; j<2; j++)
			do_round(i, j);

	mrpc_conn_close(conn);
	mrpc_conn_unref(conn);
	mrpc_conn_set_unref(cset);
	mrpc_listen_close(sset);
	mrpc_conn_set_unref(sset);
	sem_destroy(&complete);
	expect_disconnects(1, 1, 0);
	return 0;
}
