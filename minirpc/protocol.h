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

#ifndef MINIRPC_PROTOCOL
#error This header is for use by miniRPC stub code only
#endif

#ifndef MINIRPC_PROTOCOL_H
#define MINIRPC_PROTOCOL_H

#include <rpc/xdr.h>
#include <minirpc/minirpc.h>

#define SET_PTR_IF_NOT_NULL(ptr, val) do { \
		if ((ptr) != NULL) \
			*(ptr)=(val); \
	} while (0)

struct mrpc_protocol {
	int is_server;
	mrpc_status_t (*request)(const void *ops, void *conn_data,
				struct mrpc_message *msg, int cmd, void *in,
				void *out);
	mrpc_status_t (*sender_request_info)(unsigned cmd, xdrproc_t *type,
				unsigned *size);
	mrpc_status_t (*sender_reply_info)(unsigned cmd, xdrproc_t *type,
				unsigned *size);
	mrpc_status_t (*receiver_request_info)(unsigned cmd, xdrproc_t *type,
				unsigned *size);
	mrpc_status_t (*receiver_reply_info)(unsigned cmd, xdrproc_t *type,
				unsigned *size);
};

typedef void reply_callback_fn;
typedef void (long_reply_callback_fn)(void *conn_private, void *msg_private,
			struct mrpc_message *reply, mrpc_status_t status,
			void *data);
typedef void (short_reply_callback_fn)(void *conn_private, void *msg_private,
			struct mrpc_message *reply, mrpc_status_t status);

/* connection.c */
int mrpc_conn_set_operations(struct mrpc_connection *conn,
			struct mrpc_protocol *protocol, const void *ops);

/* message.c */
mrpc_status_t mrpc_send_request(const struct mrpc_protocol *protocol,
			struct mrpc_connection *conn, int cmd, void *in,
			void **out);
mrpc_status_t mrpc_send_request_async(const struct mrpc_protocol *protocol,
			struct mrpc_connection *conn, int cmd,
			reply_callback_fn *callback, void *private, void *in);
mrpc_status_t mrpc_send_request_noreply(const struct mrpc_protocol *protocol,
			struct mrpc_connection *conn, int cmd, void *in);
mrpc_status_t mrpc_send_reply(const struct mrpc_protocol *protocol,
			struct mrpc_message *request, void *data);
mrpc_status_t mrpc_send_reply_error(const struct mrpc_protocol *protocol,
			struct mrpc_message *request, mrpc_status_t status);

#endif
