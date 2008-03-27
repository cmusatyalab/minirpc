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

#include <pthread.h>
#include <assert.h>
#define MINIRPC_INTERNAL
#include "internal.h"

struct reftrack_version {
	refserial_t serial;
	unsigned count;
};

struct reftrack {
	pthread_mutex_t lock;
	pthread_cond_t cond;
	struct reftrack_version *cur;
	GSList *prev;
};

struct thread_version_count_data {
	const struct reftrack_version *ver;
	unsigned count;
};

static __thread GSList *thread_versions;

static gint ref_compare_ver_equal(const void *item, const void *serialptr)
{
	const struct reftrack_version *ver=item;
	const refserial_t *serial=serialptr;

	assert(ver->count > 0);
	if (ver->serial == *serial)
		return 0;
	return 1;
}

static void count_thread_version_refs(void *data, void *user_data)
{
	struct thread_version_count_data *thdata = user_data;

	if (data == thdata->ver)
		thdata->count++;
}

static gint ref_compare_ver_wait(const void *item, const void *serialptr)
{
	const struct reftrack_version *ver=item;
	const refserial_t *serial=serialptr;
	struct thread_version_count_data thdata;

	assert(ver->count > 0);
	if (ver->serial > *serial)
		return 1;
	thdata.ver=ver;
	thdata.count=0;
	g_slist_foreach(thread_versions, count_thread_version_refs, &thdata);
	assert(ver->count >= thdata.count);
	if (ver->count == thdata.count)
		return 1;
	return 0;
}

/* ref lock must be held */
static struct reftrack_version *ref_findver(struct reftrack *ref,
			refserial_t serial)
{
	GSList *lh;

	if (ref->cur->serial == serial)
		return ref->cur;
	lh=g_slist_find_custom(ref->prev, &serial, ref_compare_ver_equal);
	if (lh == NULL)
		return NULL;
	return lh->data;
}

struct reftrack *ref_alloc(void)
{
	struct reftrack *ref;

	ref=g_slice_new0(struct reftrack);
	pthread_mutex_init(&ref->lock, NULL);
	pthread_cond_init(&ref->cond, NULL);
	ref->cur=g_slice_new0(struct reftrack_version);
	return ref;
}

void ref_free(struct reftrack *ref)
{
	pthread_mutex_lock(&ref->lock);
	assert(ref->cur->count == 0);
	assert(ref->prev == NULL);
	pthread_mutex_unlock(&ref->lock);
	g_slice_free(struct reftrack_version, ref->cur);
	g_slice_free(struct reftrack, ref);
}

/* Get a reference to the current version of the reftrack.  Return the serial
   number of that version.  The reference is only valid in the thread that
   obtained it. */
refserial_t ref_get(struct reftrack *ref)
{
	struct reftrack_version *ver;

	pthread_mutex_lock(&ref->lock);
	ver=ref->cur;
	ver->count++;
	pthread_mutex_unlock(&ref->lock);
	thread_versions=g_slist_prepend(thread_versions, ver);
	return ver->serial;
}

/* Release a reference to the reftrack.  Caller must specify the serial number
   originally returned by ref_get().  The reference must be released by the
   same thread that obtained it. */
void ref_put(struct reftrack *ref, refserial_t serial)
{
	struct reftrack_version *ver;

	pthread_mutex_lock(&ref->lock);
	ver=ref_findver(ref, serial);
	assert(ver != NULL);
	if (!--ver->count && ver != ref->cur) {
		ref->prev=g_slist_remove(ref->prev, ver);
		g_slice_free(struct reftrack_version, ver);
		pthread_cond_broadcast(&ref->cond);
	}
	pthread_mutex_unlock(&ref->lock);
	thread_versions=g_slist_remove(thread_versions, ver);
}

/* Update the reftrack to a new version.  Return the serial number of the
   version we displaced. */
refserial_t ref_update(struct reftrack *ref)
{
	refserial_t oldserial;

	pthread_mutex_lock(&ref->lock);
	oldserial=ref->cur->serial;
	if (ref->cur->count) {
		ref->prev=g_slist_prepend(ref->prev, ref->cur);
		ref->cur=g_slice_new0(struct reftrack_version);
	}
	ref->cur->serial = oldserial + 1;
	pthread_mutex_unlock(&ref->lock);
	return oldserial;
}

/* Block until no threads (other than, possibly, the caller) hold reftrack
   references to versions less than or equal the specified version. */
void ref_wait(struct reftrack *ref, refserial_t serial)
{
	pthread_mutex_lock(&ref->lock);
	assert(ref->cur->serial > serial);
	while (g_slist_find_custom(ref->prev, &serial,
				ref_compare_ver_wait) != NULL)
		pthread_cond_wait(&ref->cond, &ref->lock);
	pthread_mutex_unlock(&ref->lock);
}

/* Returns true if someone holds a reference on the reftrack.  This is racy,
   and is only intended for assert()s and such. */
int ref_is_held(struct reftrack *ref)
{
	int ret=0;

	pthread_mutex_lock(&ref->lock);
	if (ref->cur->count || ref->prev != NULL)
		ret=1;
	pthread_mutex_unlock(&ref->lock);
	return ret;
}
