/*
 * miniRPC - TCP RPC library with asynchronous operations
 *
 * Copyright (C) 2007-2008 Carnegie Mellon University
 *
 * This code is distributed "AS IS" without warranty of any kind under the
 * terms of the GNU Lesser General Public License version 2.1, as shown in
 * the file COPYING.
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
	int port;
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

void client(int files)
{
	struct mrpc_conn_set *cset;
	struct mrpc_connection *conn;
	GAsyncQueue *queue;
	struct open_conn *oconn;
	pthread_t thr;
	int i;
	int ret;
	char *port;

	if (mrpc_conn_set_create(&cset, proto_client, NULL))
		die("Couldn't allocate conn set");
	mrpc_set_disconnect_func(cset, disconnect_user);
	start_monitored_dispatcher(cset);
	queue=g_async_queue_new();
	pthread_create(&thr, NULL, closer, queue);

	sem_post(&shared->ready);
	sem_wait(&shared->start);
	port=g_strdup_printf("%u", g_atomic_int_get(&shared->port));
	for (i=0; i < files - BUFFER; i++) {
		ret=mrpc_conn_create(&conn, cset, NULL);
		if (ret)
			die("%s", strerror(ret));
		ret=mrpc_connect(conn, AF_UNSPEC, NULL, port);
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
	g_free(port);
	exit(0);
}

int main(int argc, char **argv)
{
	struct mrpc_conn_set *sset;
	char *port;
	int stat;
	int ret=0;
	int files;
	int i;

	/* Valgrind keeps a reserved FD range at the upper end of the FD
	   space, but doesn't use all of the FDs in it.  If accept() returns
	   an fd inside this space, Valgrind converts the return value into
	   EMFILE and closes the fd (!!!).  This causes the client to receive
	   unexpected connection closures and makes the test fail.  So we
	   don't run this test under Valgrind.  (Also, some versions of
	   Valgrind don't support process-shared semaphores.) */
	exclude_valgrind();

	files=get_max_files();
	shared=mmap(NULL, sizeof(*shared), PROT_READ|PROT_WRITE,
				MAP_SHARED|MAP_ANONYMOUS, 0, 0);
	if (shared == MAP_FAILED)
		die("Couldn't map shared segment: %s", strerror(errno));
	if (sem_init(&shared->ready, 1, 0))
		die("Couldn't initialize semaphore: %s", strerror(errno));
	if (sem_init(&shared->start, 1, 0))
		die("Couldn't initialize semaphore: %s", strerror(errno));

	for (i=0; i<MULTIPLE; i++)
		if (!fork())
			client(files);
	for (i=0; i<MULTIPLE; i++)
		sem_wait(&shared->ready);
	sset=spawn_server(&port, proto_server, sync_server_accept, NULL, 1);
	mrpc_set_disconnect_func(sset, disconnect_normal);
	g_atomic_int_set(&shared->port, atoi(port));
	for (i=0; i<MULTIPLE; i++)
		sem_post(&shared->start);
	while (1) {
		if (wait(&stat) == -1) {
			if (errno == ECHILD)
				break;
			else
				continue;
		}
		if (WIFSIGNALED(stat)) {
			message("Client died on signal %d", WTERMSIG(stat));
			ret=1;
		}
		if (WIFEXITED(stat) && WEXITSTATUS(stat)) {
			message("Client returned %d", WEXITSTATUS(stat));
			ret=1;
		}
	}
	mrpc_listen_close(sset);
	mrpc_conn_set_unref(sset);
	expect_disconnects(0, MULTIPLE * (files - BUFFER), 0);
	sem_destroy(&shared->ready);
	sem_destroy(&shared->start);
	free(port);
	return ret;
}
