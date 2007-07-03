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

static apr_pool_t *init_pool;
/* We can't use an APR atomic variable, because we haven't yet initialized
   the atomic module */
static int init_done;
static pthread_mutex_t init_lock=PTHREAD_MUTEX_INITIALIZER;

/* It's not safe to destroy init_pool, because the application may still want
   to use APR atomic variables even after it's done using miniRPC.  In fact
   it's not really safe to call apr_atomic_init() twice from the same process
   context. */
apr_status_t mrpc_init(void)
{
	apr_status_t stat=APR_SUCCESS;

	pthread_mutex_lock(&init_lock);
	if (init_done)
		goto done;
	stat=apr_initialize();
	if (stat)
		goto done;
	stat=apr_pool_create(&init_pool, NULL);
	if (stat)
		goto bad_pool;
	stat=apr_atomic_init(init_pool);
	if (stat)
		goto bad_atomic;
	init_done=1;
done:
	pthread_mutex_unlock(&init_lock);
	return stat;

bad_atomic:
	apr_pool_destroy(init_pool);
bad_pool:
	apr_terminate();
	goto done;
}
