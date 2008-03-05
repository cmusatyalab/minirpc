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

#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#define MINIRPC_INTERNAL
#include "internal.h"

exported int mrpc_init(void)
{
	if (!g_thread_supported())
		g_thread_init(NULL);
	return 0;
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
