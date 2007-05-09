#define LIBPROTOCOL
#include "internal.h"
#include "sample-client.h"

void free_ListParcels(ListParcels *in, int container)
{
	xdr_free(xdr_ListParcels, in);
	if (container)
		free(in);
}

typedef void (list_parcels_callback_fn)(void *conn_private, void *msg_private,
			int status, ListParcelsReply *reply);

int list_parcels(struct minirpc_connection *conn, ListParcels *in,
			ListParcelsReply **out)
{
	struct minirpc_message *msg;
	int ret;
	
	ret=format_request(conn, 17, in, &msg);
	if (ret)
		return ret;
	ret=minirpc_send_request(conn, &msg);
	if (ret)
		return ret;
	ret=msg->hdr.status;
	if (!ret)
		ret=unformat_request(conn, 17, msg, out);
	minirpc_free_message(msg);
	return ret;
}

int list_parcels_async(struct minirpc_connection *conn, ListParcels *in,
			list_parcels_callback_fn *callback, void *private)
{
	struct minirpc_message *msg;
	int ret;
	
	ret=format_request(conn, 17, in, &msg);
	if (ret)
		return ret;
	return minirpc_send_request_async(conn, msg, callback, private);
}

int send_list_parcels_async_reply(struct minirpc_message *request,
			ListParcelsReply *out)
{
	stuff;
	minirpc_free_message(request);
}

int list_parcels_oneway(struct minirpc_connection *conn, ListParcels *in)
{
	struct minirpc_message *msg;
	int ret;
	
	ret=format_request(conn, 17, in, &msg);
	if (ret)
		return ret;
	return send_message(conn, msg);
}

int sample_client_request_info(unsigned cmd, xdrproc_t *type, unsigned *size)
{
	switch (cmd) {
	case nr_func1:
		SET_PTR_IF_NOT_NULL(type, (xdrproc_t)xdr_Func1Data);
		SET_PTR_IF_NOT_NULL(size, sizeof(Func1Data));
		return MINIRPC_OK;
	default:
		return MINIRPC_PROCEDURE_UNAVAIL;
	}
	
}

int sample_client_reply_info(unsigned cmd, xdrproc_t *type, unsigned *size)
{
	switch (cmd) {
	case nr_func1:
		SET_PTR_IF_NOT_NULL(type, (xdrproc_t)xdr_Func1Return);
		SET_PTR_IF_NOT_NULL(size, sizeof(Func1Return));
		return MINIRPC_OK;
	default:
		return MINIRPC_PROCEDURE_UNAVAIL;
	}
	
}

int sample_client_request(struct minirpc_connection *conn, int cmd, void *in,
			void *out)
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

int sample_client_set_operations(struct minirpc_connection *conn,
			struct sample_client_operations *ops)
{
	if (conn->set->protocol != protocol)
		return MINIRPC_PROTOCOL_MISMATCH;
	pthread_rwlock_wrlock(&conn->operations_lock);
	conn->operations=ops;
	pthread_rwlock_unlock(&conn->operations_lock);
	return MINIRPC_OK;
}

/* XXX restrict app from returning REQUEST status? */
