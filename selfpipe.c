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

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#define MINIRPC_INTERNAL
#include "internal.h"

struct selfpipe {
	int pipe[2];
	int set;
	pthread_mutex_t lock;
};

/* Also used by connection.c; there's no good place to put this */
int set_nonblock(int fd)
{
	int flags;

	flags=fcntl(fd, F_GETFL);
	if (flags == -1)
		return -errno;
	if (fcntl(fd, F_SETFL, flags | O_NONBLOCK))
		return -errno;
	return 0;
}

int selfpipe_create(struct selfpipe **new)
{
	struct selfpipe *sp;
	int i;
	int ret;

	*new=NULL;
	sp=g_slice_new0(struct selfpipe);
	pthread_mutex_init(&sp->lock, NULL);
	if (pipe(sp->pipe)) {
		ret=-errno;
		goto bad_free;
	}
	for (i=0; i < 2; i++) {
		ret=set_nonblock(sp->pipe[i]);
		if (ret)
			goto bad_close;
	}
	*new=sp;
	return 0;

bad_close:
	close(sp->pipe[1]);
	close(sp->pipe[0]);
bad_free:
	g_slice_free(struct selfpipe, sp);
	return ret;
}

void selfpipe_destroy(struct selfpipe *sp)
{
	close(sp->pipe[1]);
	close(sp->pipe[0]);
	g_slice_free(struct selfpipe, sp);
}

void selfpipe_set(struct selfpipe *sp)
{
	pthread_mutex_lock(&sp->lock);
	/* In order to be resilient to someone else (improperly) reading
	   from the read end of the pipe, we always write even if the pipe
	   is already readable. */
	write(sp->pipe[1], "a", 1);
	sp->set=1;
	pthread_mutex_unlock(&sp->lock);
}

void selfpipe_clear(struct selfpipe *sp)
{
	char buf[32];

	pthread_mutex_lock(&sp->lock);
	while (read(sp->pipe[0], buf, sizeof(buf)) == sizeof(buf));
	sp->set=0;
	pthread_mutex_unlock(&sp->lock);
}

int selfpipe_is_set(struct selfpipe *sp)
{
	int ret;

	pthread_mutex_lock(&sp->lock);
	ret=sp->set;
	pthread_mutex_unlock(&sp->lock);
	return ret;
}

int selfpipe_fd(struct selfpipe *sp)
{
	return sp->pipe[0];
}