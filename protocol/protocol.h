#ifndef PROTOCOL_H
#define PROTOCOL_H

#include "ISRMessage.h"

struct isr_conn_set;
struct isr_connection;

typedef void (request_fn)(struct isr_connection *conn, void *conn_data,
			struct ISRMessage *msg);

typedef void (reply_callback_fn)(struct isr_connection *conn, void *conn_data,
			struct ISRMessage *request, struct ISRMessage *reply,
			void *msg_data);

int isr_conn_set_alloc(struct isr_conn_set **new_set, int is_server,
			request_fn *func, int expected_fds,
			unsigned conn_buckets, unsigned msg_buckets,
			unsigned msg_buf_len);
void isr_conn_set_free(struct isr_conn_set *set);

int isr_conn_add(struct isr_connection **new_conn, struct isr_conn_set *set,
			int fd, void *data);
void isr_conn_remove(struct isr_connection *conn);

int isr_send_request(struct isr_connection *conn, struct ISRMessage *request,
			struct ISRMessage **reply);
int isr_send_request_async(struct isr_connection *conn,
			struct ISRMessage *request,
			reply_callback_fn *callback, void *data);
int isr_send_reply(struct isr_connection *conn,
			struct ISRMessage *request, struct ISRMessage *reply);
int isr_send_partial_reply(struct isr_connection *conn,
			struct ISRMessage *request, struct ISRMessage *reply);

struct ISRMessage *isr_alloc_message(void);
void isr_free_message(struct ISRMessage *msg);

#endif
