#ifndef MINIRPC_PROTOCOL
#error This header is for use by miniRPC stub code only
#endif

#ifndef MINIRPC_PROTOCOL_H
#define MINIRPC_PROTOCOL_H

#include <rpc/xdr.h>
/* XXX for status enum; should move to minirpc.h */
#include "minirpc_xdr.h"

#define SET_PTR_IF_NOT_NULL(ptr, val) do { \
		if ((ptr) != NULL) \
			*(ptr)=(val); \
	} while (0)

struct mrpc_message;

struct mrpc_protocol {
	int is_server;
	int (*request)(void *ops, void *conn_data, struct mrpc_message *msg,
				int cmd, void *in, void *out);
	int (*sender_request_info)(unsigned cmd, xdrproc_t *type,
				unsigned *size);
	int (*sender_reply_info)(unsigned cmd, xdrproc_t *type,
				unsigned *size);
	int (*receiver_request_info)(unsigned cmd, xdrproc_t *type,
				unsigned *size);
	int (*receiver_reply_info)(unsigned cmd, xdrproc_t *type,
				unsigned *size);
};

typedef void (reply_callback_fn)(void *conn_private, void *msg_private,
			int status, void *data);

/* connection.c */
int mrpc_conn_set_operations(struct mrpc_connection *conn,
			struct mrpc_protocol *protocol, void *ops);

/* message.c */
int mrpc_send_request(const struct mrpc_protocol *protocol,
			struct mrpc_connection *conn, unsigned cmd, void *in,
			void **out);
int mrpc_send_request_async(const struct mrpc_protocol *protocol,
			struct mrpc_connection *conn, unsigned cmd,
			reply_callback_fn *callback, void *private, void *in);
int mrpc_send_request_noreply(const struct mrpc_protocol *protocol,
			struct mrpc_connection *conn, unsigned cmd, void *in);
int mrpc_send_reply(const struct mrpc_protocol *protocol,
			struct mrpc_message *request, int status, void *data);

#endif