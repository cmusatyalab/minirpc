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

#define MULTIPLE 5
#define BUFFER 25
#define DELAY 2

#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <time.h>
#include <semaphore.h>
#include <unistd.h>
#include <glib.h>
#include <pthread.h>
#include "common.h"

static struct {
	sem_t ready;
	sem_t start;
} *shared;

struct open_conn {
	struct mrpc_connection *conn;
	struct timeval expire;
} sentinel;

int get_max_files(void)
{
	struct rlimit rlim;

	if (getrlimit(RLIMIT_NOFILE, &rlim))
		die("Couldn't get system fd limit");
	return rlim.rlim_cur;
}

void *closer(void *arg)
{
	GAsyncQueue *queue=arg;
	struct open_conn *oconn;
	struct timeval curtime;
	struct timeval wait;
	struct timespec wait_ts;

	g_async_queue_ref(queue);
	while ((oconn=g_async_queue_pop(queue)) != &sentinel) {
		gettimeofday(&curtime, NULL);
		if (timercmp(&curtime, &oconn->expire, <)) {
			timersub(&oconn->expire, &curtime, &wait);
			TIMEVAL_TO_TIMESPEC(&wait, &wait_ts);
			while (nanosleep(&wait_ts, &wait_ts) &&
						errno == EINTR);
		}
		mrpc_conn_close(oconn->conn);
		mrpc_conn_unref(oconn->conn);
		g_slice_free(struct open_conn, oconn);
	}
	g_async_queue_unref(queue);
	return NULL;
}

void client(int files, unsigned port)
{
	struct mrpc_conn_set *cset;
	struct mrpc_connection *conn;
	GAsyncQueue *queue;
	struct open_conn *oconn;
	pthread_t thr;
	int i;
	int ret;

	if (mrpc_conn_set_create(&cset, proto_client, NULL))
		die("Couldn't allocate conn set");
	mrpc_set_disconnect_func(cset, disconnect_user);
	start_monitored_dispatcher(cset);
	queue=g_async_queue_new();
	pthread_create(&thr, NULL, closer, queue);

	sem_post(&shared->ready);
	sem_wait(&shared->start);
	for (i=0; i < files - BUFFER; i++) {
		ret=mrpc_conn_create(&conn, cset, NULL);
		if (ret)
			die("%s", strerror(ret));
		ret=mrpc_connect(conn, NULL, port);
		if (ret)
			die("%s", strerror(ret));
		oconn=g_slice_new0(struct open_conn);
		oconn->conn=conn;
		gettimeofday(&oconn->expire, NULL);
		oconn->expire.tv_sec += DELAY;
		g_async_queue_push(queue, oconn);
	}
	g_async_queue_push(queue, &sentinel);
	pthread_join(thr, NULL);
	g_async_queue_unref(queue);
	mrpc_conn_set_unref(cset);
	expect_disconnects(files - BUFFER, 0, 0);
	exit(0);
}

int main(int argc, char **argv)
{
	struct mrpc_conn_set *sset;
	unsigned port;
	int stat;
	int ret=0;
	int files;
	int i;

	files=get_max_files();
	shared=mmap(NULL, sizeof(*shared), PROT_READ|PROT_WRITE,
				MAP_SHARED|MAP_ANONYMOUS, 0, 0);
	if (shared == MAP_FAILED)
		die("Couldn't map shared segment: %s", strerror(errno));
	if (sem_init(&shared->ready, 1, 0)) {
		/* Valgrind apparently still uses LinuxThreads. */
		if (errno == ENOSYS)
			return 77;
		die("Couldn't initialize semaphore: %s", strerror(errno));
	}
	if (sem_init(&shared->start, 1, 0))
		die("Couldn't initialize semaphore: %s", strerror(errno));

	sset=spawn_server(&port, proto_server, sync_server_accept, NULL, 1);
	mrpc_set_disconnect_func(sset, disconnect_normal);

	for (i=0; i<MULTIPLE; i++)
		if (!fork())
			client(files, port);
	for (i=0; i<MULTIPLE; i++)
		sem_wait(&shared->ready);
	for (i=0; i<MULTIPLE; i++)
		sem_post(&shared->start);
	while (1) {
		if (wait(&stat) == -1) {
			if (errno == ECHILD)
				break;
			else
				continue;
		}
		if (!WIFEXITED(stat))
			die("Unexpected return from wait()");
		if (WEXITSTATUS(stat)) {
			message("Client returned %d", WEXITSTATUS(stat));
			ret=1;
		}
	}
	mrpc_listen_close(sset);
	mrpc_conn_set_unref(sset);
	expect_disconnects(0, MULTIPLE * (files - BUFFER), 0);
	sem_destroy(&shared->ready);
	sem_destroy(&shared->start);
	return ret;
}
