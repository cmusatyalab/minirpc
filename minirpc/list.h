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

#ifndef LIST_H
#define LIST_H

#include <stddef.h>

struct list_head {
	struct list_head *prev;
	struct list_head *next;
};

static inline void INIT_LIST_HEAD(struct list_head *head)
{
	head->prev=head;
	head->next=head;
}

static inline int list_is_empty(struct list_head *head)
{
	return (head == head->prev && head == head->next);
}

static inline void list_add(struct list_head *new, struct list_head *list)
{
	new->next=list->next;
	new->prev=list;
	list->next->prev=new;
	list->next=new;
}

static inline void list_add_tail(struct list_head *new, struct list_head *list)
{
	new->prev=list->prev;
	new->next=list;
	list->prev->next=new;
	list->prev=new;
}

static inline void list_del_init(struct list_head *head)
{
	head->prev->next=head->next;
	head->next->prev=head->prev;
	head->prev=head;
	head->next=head;
}

#define list_entry(head, type, field) (((void*)head) - offsetof(type, field))
#define list_first_entry(head, type, field) \
	list_entry((head)->next, type, field)
#define list_for_each(cur, head) \
	for (cur=(head)->next; cur != (head); cur=cur->next)
#define list_for_each_safe(cur, next, head) \
	for (cur=(head)->next, next=cur->next; cur != (head); \
			cur=next, next=cur->next)
#define list_for_each_entry(cur, head, field) \
	for (cur=list_entry((head)->next, typeof(cur), field); \
			&cur->field != (head); \
			cur=list_entry(cur->field.next, typeof(cur), field))
#define list_for_each_entry_safe(cur, next, head, field) \
	for (cur=list_entry((head)->next, typeof(cur), field), \
			next=list_entry(cur->field.next, typeof(cur), field); \
			&cur->field != (head); cur=next, \
			next=list_entry(cur->field.next, typeof(cur), field))

#endif
