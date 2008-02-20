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
#include <sys/epoll.h>
#include <errno.h>
#define MINIRPC_POLLSET
#define MINIRPC_INTERNAL
#include "internal.h"
#include "pollset_impl.h"

struct impl_data {
	int fd;
};

static int epoll_create_set(struct pollset *pset)
{
	int err;

	pset->impl=g_slice_new0(struct impl_data);
	pset->impl->fd=epoll_create(8);
	if (pset->impl->fd == -1) {
		err=-errno;
		g_slice_free(struct impl_data, pset->impl);
		return err;
	}
	return 0;
}

static void epoll_destroy_set(struct pollset *pset)
{
	close(pset->impl->fd);
	g_slice_free(struct impl_data, pset->impl);
}

static void populate_event(struct poll_fd *pfd, struct epoll_event *ev)
{
	memset(ev, 0, sizeof(*ev));
	ev->data.ptr=pfd;
	if (pfd->flags & POLLSET_READABLE)
		ev->events |= EPOLLIN;
	if (pfd->flags & POLLSET_WRITABLE)
		ev->events |= EPOLLOUT;
	/* EPOLLHUP and EPOLLERR are implicitly monitored */
}

static int epoll_add(struct pollset *pset, struct poll_fd *pfd)
{
	struct epoll_event ev;

	populate_event(pfd, &ev);
	return epoll_ctl(pset->impl->fd, EPOLL_CTL_ADD, pfd->fd, &ev);
}

static int epoll_modify(struct pollset *pset, struct poll_fd *pfd)
{
	struct epoll_event ev;

	populate_event(pfd, &ev);
	return epoll_ctl(pset->impl->fd, EPOLL_CTL_MOD, pfd->fd, &ev);
}

static void epoll_remove(struct pollset *pset, struct poll_fd *pfd)
{
	/* The last argument is ignored and should be NULL, but kernels
	   kernels < 2.6.9 return an error if it is */
	epoll_ctl(pset->impl->fd, EPOLL_CTL_DEL, pfd->fd,
				(struct epoll_event *)1);
}

static int epoll_poll(struct pollset *pset)
{
	struct epoll_event ev[64]; /* XXX */
	struct poll_fd *pfd;
	int i;
	int count;

	count=epoll_wait(pset->impl->fd, ev, 16, -1);
	if (count == -1)
		return -errno;

	for (i=0; i<count; i++) {
		pfd=ev[i].data.ptr;
		if ((ev[i].events & EPOLLOUT) && pfd->writable_fn &&
					(pfd->flags & POLLSET_WRITABLE) &&
					!pfd->dead)
			pfd->writable_fn(pfd->private, pfd->fd);
		if ((ev[i].events & EPOLLIN) && pfd->readable_fn &&
					(pfd->flags & POLLSET_READABLE) &&
					!pfd->dead)
			pfd->readable_fn(pfd->private, pfd->fd);
		if ((ev[i].events & (EPOLLERR|EPOLLHUP)) && !pfd->dead) {
			if ((ev[i].events & EPOLLHUP) && pfd->hangup_fn)
				pfd->hangup_fn(pfd->private, pfd->fd);
			else if (pfd->error_fn)
				pfd->error_fn(pfd->private, pfd->fd);
			pollset_del(pset, pfd->fd);
		}
	}
	return 0;
}

static const struct pollset_ops ops = {
	.create = epoll_create_set,
	.destroy = epoll_destroy_set,
	.add = epoll_add,
	.modify = epoll_modify,
	.remove = epoll_remove,
	.poll = epoll_poll
};
const struct pollset_ops *ops_epoll = &ops;
