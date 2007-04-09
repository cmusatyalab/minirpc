#ifndef LIBPROTOCOL
#error This header is for internal use by the ISR protocol library
#endif

#ifndef INTERNAL_H
#define INTERNAL_H

#include "protocol.h"
#include "list.h"
#include "hash.h"

struct isr_conn_set {
	pthread_mutex_t lock;
	struct htable *table;
	unsigned buflen;
	int epoll_fd;
	int server;
	new_request_fn *request_fn;
	unsigned msg_buckets;
};

struct isr_connection {
	struct list_head lh_hash;
	struct isr_conn_set *set;
	int fd;
	char *send_buf;
	unsigned send_offset;
	unsigned send_length;
	pthread_mutex_t send_msgs_lock;
	struct list_head send_msgs;
	char *recv_buf;
	unsigned recv_offset;
	struct ISRMessage *recv_msg;
	struct htable *pending_replies;
	pthread_mutex_t pending_replies_lock;
	void *data;
};

#endif
