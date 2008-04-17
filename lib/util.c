/*
 * miniRPC - TCP RPC library with asynchronous operations
 *
 * Copyright (C) 2007-2008 Carnegie Mellon University
 *
 * This code is distributed "AS IS" without warranty of any kind under the
 * terms of the GNU Lesser General Public License version 2.1, as shown in
 * the file COPYING.
 */

#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <assert.h>
#include <pthread.h>
#define MINIRPC_INTERNAL
#include "internal.h"
#include "version.h"

static void _mrpc_init(void)
{
	if (!g_thread_supported())
		g_thread_init(NULL);
}

void mrpc_init(void)
{
	static pthread_once_t started = PTHREAD_ONCE_INIT;

	pthread_once(&started, _mrpc_init);
}

int set_nonblock(int fd)
{
	int flags;

	flags=fcntl(fd, F_GETFL);
	if (flags == -1)
		return errno;
	if (fcntl(fd, F_SETFL, flags | O_NONBLOCK))
		return errno;
	return 0;
}

int block_signals(void)
{
	sigset_t sigs;

	if (sigfillset(&sigs))
		return errno;
	return pthread_sigmask(SIG_SETMASK, &sigs, NULL);
}

void assert_callback_func(void *ignored)
{
	assert(0);
}

exported int mrpc_version_code(void)
{
	return VERSION_CODE;
}
