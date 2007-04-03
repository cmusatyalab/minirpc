#ifndef HASH_H
#define HASH_H

#include "list.h"

typedef (unsigned)(struct list_head *entry, unsigned buckets) hash_func_t;
typedef (int)(struct list_head *entry, void *data) hash_match_t;

struct htable {
	hash_func_t hfunc;
	struct list_head *hash;
	unsigned buckets;
	unsigned count;
}

static inline struct htable *hash_alloc(unsigned buckets, hash_func_t hfunc)
{
	struct htable *table;
	int i;
	
	table=malloc(sizeof(*table));
	if (table == NULL)
		return NULL;
	table->hash=malloc(buckets * sizeof(*table->hash));
	if (table->hash == NULL) {
		free(table);
		return NULL;
	}
	for (i=0; i<buckets; i++)
		INIT_LIST_HEAD(&table->hash[i]);
	table->buckets=buckets;
	table->hfunc=hfunc;
	table->count=0;
	return table;
}

static inline int hash_free(struct htable *table)
{
	if (table->count != 0)
		return -EBUSY;
	free(table->hash);
	free(table);
	return 0;
}

static inline void hash_add(struct htable *table, struct list_head *entry)
{
	unsigned bucket=table->hfunc(entry, table->buckets);
	list_add_tail(entry, &table->hash[bucket]);
	table->count++;
}

/* XXX "bucketkey" parameter sucks */
static inline struct list_head *hash_get(struct htable *table,
			hash_match_t match, unsigned bucketkey, void *data)
{
	struct list_head *cur;
	
	list_for_each(cur, &table->hash[bucket % table->buckets])
		if (match(cur, data))
			return cur;
	return NULL;
}

static inline void hash_remove(struct htable *table, struct list_head *entry)
{
	list_del_init(entry);
	table->count--;
}

#endif