/*
 * miniRPC - TCP RPC library with asynchronous operations and TLS support
 *
 * Copyright (C) 2007 Carnegie Mellon University
 *
 * This software is distributed under the terms of the Eclipse Public License,
 * Version 1.0 which can be found in the file named LICENSE.  ANY USE,
 * REPRODUCTION OR DISTRIBUTION OF THIS SOFTWARE CONSTITUTES RECIPIENT'S
 * ACCEPTANCE OF THIS AGREEMENT
 */

#include <pthread.h>
#include <apr_pools.h>
#include <apr_atomic.h>
#define MINIRPC_INTERNAL
#include "internal.h"

apr_pool_t *mrpc_pool;

/* We can't use an APR atomic variable, because we haven't yet initialized
   the atomic module */
static unsigned init_count;
static pthread_mutex_t init_lock=PTHREAD_MUTEX_INITIALIZER;

apr_status_t mrpc_get(void)
{
	apr_status_t stat=APR_SUCCESS;

	pthread_mutex_lock(&init_lock);
	if (init_count++)
		goto done;
	stat=apr_initialize();
	if (stat)
		goto bad_init;
	stat=apr_pool_create(&mrpc_pool, NULL);
	if (stat)
		goto bad_pool;
	stat=apr_atomic_init(mrpc_pool);
	if (stat)
		goto bad_atomic;
done:
	pthread_mutex_unlock(&init_lock);
	return stat;

bad_atomic:
	apr_pool_destroy(mrpc_pool);
bad_pool:
	apr_terminate();
bad_init:
	init_count--;
	goto done;
}

void mrpc_put(void)
{
	pthread_mutex_lock(&init_lock);
	if (!--init_count) {
		apr_pool_destroy(mrpc_pool);
		apr_terminate();
	}
	pthread_mutex_unlock(&init_lock);
}
