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

#endif
