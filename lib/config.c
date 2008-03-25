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

#include <errno.h>
#include <pthread.h>
#define MINIRPC_INTERNAL
#include "internal.h"

#define set_config(set, var, val) do {				\
		pthread_mutex_lock(&(set)->config_lock);	\
		(set)->config.var=(val);			\
		pthread_mutex_unlock(&(set)->config_lock);	\
	} while (0)

exported int mrpc_set_accept_func(struct mrpc_conn_set *set,
			mrpc_accept_fn *func)
{
	if (set == NULL || !set->protocol->is_server || func == NULL)
		return EINVAL;
	set_config(set, accept, func);
	return 0;
}

exported int mrpc_set_disconnect_func(struct mrpc_conn_set *set,
			mrpc_disconnect_fn *func)
{
	if (set == NULL)
		return EINVAL;
	set_config(set, disconnect, func);
	return 0;
}

exported int mrpc_set_ioerr_func(struct mrpc_conn_set *set,
			mrpc_ioerr_fn *func)
{
	if (set == NULL)
		return EINVAL;
	set_config(set, ioerr, func);
	return 0;
}

exported int mrpc_set_max_buf_len(struct mrpc_conn_set *set, unsigned len)
{
	if (set == NULL || len == 0)
		return EINVAL;
	set_config(set, msg_max_buf_len, len);
	return 0;
}

exported int mrpc_set_listen_backlog(struct mrpc_conn_set *set,
			unsigned backlog)
{
	if (set == NULL || backlog == 0)
		return EINVAL;
	set_config(set, listen_backlog, backlog);
	return 0;
}

exported int mrpc_set_accept_backoff(struct mrpc_conn_set *set, unsigned ms)
{
	if (set == NULL || ms == 0)
		return EINVAL;
	set_config(set, accept_backoff, ms);
	return 0;
}
