#define MINIRPC_INTERNAL
#include "internal.h"
#include "sample-client.h"

void free_ListParcels(ListParcels *in, int container)
{
	xdr_free(xdr_ListParcels, in);
	if (container)
		free(in);
}

int list_parcels(struct mrpc_connection *conn, ListParcels *in,
			ListParcelsReply **out)
{
	return mrpc_send_request(conn, nr_list_parcels, in, out);
}

typedef void (list_parcels_callback_fn)(void *conn_private, void *msg_private,
			int status, ListParcelsReply *reply);

int list_parcels_async(struct mrpc_connection *conn, ListParcels *in,
			list_parcels_callback_fn *callback, void *private)
{
	return mrpc_send_request_async(conn, nr_list_parcels, in, callback,
				private);
}

int send_list_parcels_async_reply(struct mrpc_message *request, int status,
			ListParcelsReply *out)
{
	return mrpc_send_reply(request, status, out);
}

int list_parcels_oneway(struct mrpc_connection *conn, ListParcels *in)
{
	return mrpc_send_request_noreply(conn, nr_list_parcels, in);
}

static int sample_client_request_info(unsigned cmd, xdrproc_t *type,
			unsigned *size)
{
	switch (cmd) {
	case nr_list_parcels:
		SET_PTR_IF_NOT_NULL(type, (xdrproc_t)xdr_Func1Data);
		SET_PTR_IF_NOT_NULL(size, sizeof(Func1Data));
		return MINIRPC_OK;
	default:
		return MINIRPC_PROCEDURE_UNAVAIL;
	}
	
}

static int sample_client_reply_info(unsigned cmd, xdrproc_t *type,
			unsigned *size)
{
	switch (cmd) {
	case nr_list_parcels:
		SET_PTR_IF_NOT_NULL(type, (xdrproc_t)xdr_Func1Return);
		SET_PTR_IF_NOT_NULL(size, sizeof(Func1Return));
		return MINIRPC_OK;
	default:
		return MINIRPC_PROCEDURE_UNAVAIL;
	}
	
}

static int sample_client_request(struct mrpc_connection *conn, int cmd,
			void *in, void *out)
{
	struct sample_client_operations *ops=conn->operations;
	
	switch (cmd) {
	case nr_func1:
		if (conn->operations->func1 == NULL)
			return MINIRPC_PROCEDURE_UNAVAIL;
		else
			return ops->func1(conn->data, in, out);
	default:
		return MINIRPC_PROCEDURE_UNAVAIL;
	}
}

struct mrpc_protocol sample_client = {
	.request = sample_client_request;
	.request_info = sample_client_request_info;
	.reply_info = sample_client_reply_info;
};

int sample_client_set_operations(struct mrpc_connection *conn,
			struct sample_client_operations *ops)
{
	return mrpc_conn_set_operations(conn, protocol, ops);
}

/* XXX restrict app from returning REQUEST status? */
