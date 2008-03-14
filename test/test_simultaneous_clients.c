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

#define DISPATCHERS 5
#define THREADS 25
#define ITERS 50

static const struct mrpc_config client_config = {
	.protocol = &proto_client,
	.disconnect = disconnect_user
};

static const struct mrpc_config server_config = {
	.protocol = &proto_server,
	.accept = sync_server_accept,
	.disconnect = disconnect_normal
};

static pthread_mutex_t lock;
static pthread_cond_t cond;
static int running;
static int go;
static unsigned port;

void *worker(void *arg)
{
	struct mrpc_conn_set *cset=arg;
	struct mrpc_connection *conn;
	int i;
	int ret;

	pthread_mutex_lock(&lock);
	running++;
	pthread_cond_broadcast(&cond);
	while (!go)
		pthread_cond_wait(&cond, &lock);
	pthread_mutex_unlock(&lock);
	for (i=0; i<ITERS; i++) {
		ret=mrpc_conn_create(&conn, cset, NULL);
		if (ret)
			die("%s in mrpc_conn_create() on iteration %d",
						strerror(ret), i);
		ret=mrpc_connect(conn, NULL, port);
		if (ret)
			die("%s in mrpc_connect() on iteration %d",
						strerror(ret), i);
		sync_client_set_ops(conn);
		sync_client_run(conn);
		trigger_callback_sync(conn);
		invalidate_sync(conn);
		mrpc_conn_close(conn);
	}
	pthread_mutex_lock(&lock);
	running--;
	pthread_mutex_unlock(&lock);
	pthread_cond_broadcast(&cond);
	return NULL;
}

int main(int argc, char **argv)
{
	struct mrpc_conn_set *sset;
	struct mrpc_conn_set *cset;
	pthread_t thr;
	int i;

	if (mrpc_init())
		die("Couldn't initialize minirpc");
	sset=spawn_server(&port, &server_config, NULL, DISPATCHERS);
	if (mrpc_conn_set_create(&cset, &client_config, NULL))
		die("Couldn't create conn set");
	for (i=0; i<DISPATCHERS; i++)
		mrpc_start_dispatch_thread(cset);
	pthread_mutex_init(&lock, NULL);
	pthread_cond_init(&cond, NULL);

	for (i=0; i<THREADS; i++) {
		if (pthread_create(&thr, NULL, worker, cset))
			die("Couldn't create thread");
		if (pthread_detach(thr))
			die("Couldn't detach thread");
	}
	pthread_mutex_lock(&lock);
	while (running < THREADS)
		pthread_cond_wait(&cond, &lock);
	go=1;
	pthread_cond_broadcast(&cond);
	while (running)
		pthread_cond_wait(&cond, &lock);
	pthread_mutex_unlock(&lock);

	mrpc_conn_set_destroy(cset);
	mrpc_conn_set_destroy(sset);
	expect_disconnects(THREADS * ITERS, THREADS * ITERS, 0);
	return 0;
}
