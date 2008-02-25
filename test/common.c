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

#include <stdio.h>
#include <stdarg.h>
#include <pthread.h>
#include "common.h"

#define LISTEN_PORT 58000

static const char *running_test;

void test_start(const char *desc)
{
	running_test=desc;
}

void test_pass(void)
{
}

void _message(const char *file, int line, const char *func, const char *fmt,
			...)
{
	va_list ap;

	va_start(ap, fmt);
	fprintf(stderr, "%s line %d: %s(): ", file, line, func);
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
}

static void *run_dispatch_loop(void *arg)
{
	mrpc_dispatch_loop(arg);
	return NULL;
}

struct mrpc_conn_set *spawn_server(int *listen_port,
			const struct mrpc_config *config,
			const struct mrpc_set_operations *ops,
			void *set_data, int threads)
{
	struct mrpc_conn_set *set;
	pthread_t thr;
	int ret;
	int i;
	int bound;

	if (mrpc_conn_set_alloc(&set, config, ops, set_data))
		die("Couldn't allocate connection set");
	ret=mrpc_listen(set, "localhost", LISTEN_PORT, &bound);
	if (ret)
		die("%s", strerror(-ret));
	message("Bound %d listeners", bound);
	for (i=0; i<threads; i++) {
		if (pthread_create(&thr, NULL, run_dispatch_loop, set))
			die("Couldn't spawn server thread");
	}
	if (listen_port)
		*listen_port=LISTEN_PORT;
	return set;
}
