#ifndef MINIRPC_PROTOCOL_H
#define MINIRPC_PROTOCOL_H

struct mrpc_protocol {
	int is_server;
	int (*request)(void *ops, void *conn_data, struct mrpc_message *msg,
				int cmd, void *in, void *out)
	int (*request_info)(unsigned cmd, xdrproc_t *type, unsigned *size);
	int (*reply_info)(unsigned cmd, xdrproc_t *type, unsigned *size);
};

typedef void (reply_callback_fn)(void *conn_private, void *msg_private,
			int status, void *data);

#endif
