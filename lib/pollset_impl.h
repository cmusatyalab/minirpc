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

#ifndef MINIRPC_POLLSET
#error This header is for internal use by the miniRPC pollset implementation
#endif

#ifndef MINIRPC_POLLSET_IMPL_H
#define MINIRPC_POLLSET_IMPL_H

struct pollset {
	pthread_mutex_t lock;
	GHashTable *members;
	GQueue *dead;
	GTree *timers;
	GQueue *expired;
	struct reftrack *ref;

	const struct pollset_ops *ops;
	struct impl_data *impl;
	struct selfpipe *wakeup;

	pthread_mutex_t poll_lock;
};

struct poll_fd {
	int fd;
	poll_flags_t flags;
	struct timeval expires;  /* wall-clock time to fire timeout_fn */
	void *private;
	int dead;
	poll_callback_fn *readable_fn;
	poll_callback_fn *writable_fn;
	poll_callback_fn *hangup_fn;
	poll_callback_fn *error_fn;
	poll_callback_fn *timeout_fn;
};

struct pollset_ops {
	int (*create)(struct pollset *pset);
	void (*destroy)(struct pollset *pset);
	int (*add)(struct pollset *pset, struct poll_fd *pfd);
	int (*modify)(struct pollset *pset, struct poll_fd *pfd);
	void (*remove)(struct pollset *pset, struct poll_fd *pfd);
	int (*poll)(struct pollset *pset, int timeout_ms);
};

#endif
