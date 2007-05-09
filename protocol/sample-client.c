#define LIBPROTOCOL
#include "internal.h"
#include "sample-client.h"

void free_ListParcels(ListParcels *in, int container)
{
	xdr_free(xdr_ListParcels, in);
	if (container)
		free(in);
}

int list_parcels(struct minirpc_connection *conn, ListParcels *in,
			ListParcelsReply **out)
{
	return call_sync(conn, 17, in, out);
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

void dispatch_request(struct minirpc_conn_set *conn,
			struct minirpc_message *request)
{
	struct minirpc_message *reply;
	void *request_data;
	void *reply_data;
	int ret;
	xdrproc_t request_type;
	xdrproc_t reply_type;
	unsigned request_size;
	unsigned reply_size;
	int noreply;
	
	BUG_ON(request->hdr.status != MINIRPC_REQUEST);
	
	conn->set->protocol->info(request->hdr.cmd, &noreply, &request_type,
				&reply_type, &request_size, &reply_size);
	if (!noreply) {
		reply=minirpc_alloc_message();
		if (reply == NULL) {
			/* XXX we can't return an error because we're out of
			   memory */
		}
		reply->hdr.sequence=request->hdr.sequence;
		reply->hdr.cmd=request->hdr.cmd;
	}
	request_data=malloc(request_size);
	if (request_data == NULL) {
		/* XXX */
	}
	if (!noreply) {
		reply_data=malloc(reply_size);
		if (reply_data == NULL) {
			free(request_data);
			/* XXX */
		}
	}
	ret=unserialize(request_type, request->data, request->hdr.datalen,
				request_data, request_size);
	
	pthread_rwlock_rdlock(&conn->operations_lock);
	ret=conn->set->protocol->request(conn, request->hdr.cmd, request_data,
				reply_data);
	pthread_rwlock_unlock(&conn->operations_lock);
	
	minirpc_free_message(request);
	free(request_data);
	if (!noreply) {
		reply->hdr.status=ret;
		if (ret == MINIRPC_OK) {
			ret=serialize(reply_type, reply_data, &reply->data,
						&reply->hdr.datalen);
			if (ret)
				reply->hdr.status=ret;
		}
		free(reply_data);
		send_message(conn, reply);
	}
}

void dispatch_reply(struct minirpc_connection *conn,
			struct minirpc_message *msg, void *data)
{
	
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
