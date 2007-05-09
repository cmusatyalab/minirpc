#define LIBPROTOCOL
#include "internal.h"

struct minirpc_message *minirpc_alloc_message(void)
{
	struct minirpc_message *msg;
	
	msg=malloc(sizeof(*msg));
	if (msg == NULL)
		return NULL;
	memset(msg, 0, sizeof(*msg));
	INIT_LIST_HEAD(&msg->lh_msgs);
	return msg;
}

void minirpc_free_message(struct minirpc_message *msg)
{
	/* XXX make sure list is empty */
	free(msg->data);
	free(msg);
}

static int serialize_common(enum xdr_op direction, xdrproc_t xdr_proc,
			void *data, char *buf, unsigned buflen)
{
	XDR xdrs;
	int ret=MINIRPC_OK;
	
	xdrmem_create(&xdrs, buf, buflen, direction);
	if (!xdr_proc(&xdrs, data) || xdr_getpos(&xdrs) != buflen)
		ret=MINIRPC_ENCODING_ERR;
	xdr_destroy(&xdrs);
	return MINIRPC_OK;
}

int serialize_len(xdrproc_t xdr_proc, void *in, char *out, unsigned out_len)
{
	return serialize_common(XDR_ENCODE, xdr_proc, in, out, out_len);
}

int unserialize(xdrproc_t xdr_proc, char *in, unsigned in_len, void *out,
			unsigned out_len)
{
	int ret;
	
	memset(out, 0, out_len);
	ret=serialize_common(XDR_DECODE, xdr_proc, out, in, in_len);
	if (ret) {
		/* Free partially-allocated structure */
		xdr_free(xdr_proc, out);
	}
	return ret;
}

int serialize(xdrproc_t xdr_proc, void *in, char **out, unsigned *out_len)
{
	XDR xdrs;
	char *buf;
	unsigned len;
	int ret;
	
	xdrlen_create(&xdrs);
	if (!xdr_proc(&xdrs, in)) {
		xdr_destroy(&xdrs);
		return MINIRPC_ENCODING_ERR;
	}
	len=xdr_getpos(&xdrs);
	xdr_destroy(&xdrs);
	
	buf=malloc(len);
	if (buf == NULL)
		return MINIRPC_NOMEM;
	ret=serialize_len(xdr_proc, in, buf, len);
	if (ret) {
		free(buf);
		return ret;
	}
	*out=buf;
	*out_len=len;
	return MINIRPC_OK;
}

static int format_message(xdrproc_t type, void *data,
			struct minirpc_message **result)
{
	struct minirpc_message *msg;
	int ret;
	
	msg=minirpc_alloc_message();
	if (msg == NULL)
		return MINIRPC_NOMEM;
	ret=serialize(type, in, &msg->data, &msg->hdr.datalen);
	if (ret) {
		minirpc_free_message(msg);
		return ret;
	}
	*result=msg;
	return MINIRPC_OK;
}

static int unformat_message(xdrproc_t type, unsigned size,
			struct minirpc_message *msg, void **result)
{
	void *buf;
	
	buf=malloc(size);
	if (buf == NULL)
		return MINIRPC_NOMEM;
	ret=unserialize(type, msg->data, msg->hdr.datalen, buf, size);
	if (ret) {
		free(buf);
		return ret;
	}
	*result=buf;
	return MINIRPC_OK;
}

int format_request(struct minirpc_connection *conn, unsigned cmd, void *data,
			struct minirpc_message **result)
{
	struct minirpc_message *msg;
	xdrproc_t type;
	int ret;
	
	conn->set->protocol->request_info(cmd, &type, NULL);
	ret=format_message(type, data, &msg);
	if (ret)
		return ret;
	pthread_mutex_lock(&conn->next_sequence_lock);
	msg->hdr.sequence=conn->next_sequence++;
	pthread_mutex_unlock(&conn->next_sequence_lock);
	msg->hdr.status=MINIRPC_REQUEST;
	msg->hdr.cmd=cmd;
	*result=msg;
	return MINIRPC_OK;
}

int format_reply(struct minirpc_connection *conn, unsigned sequence,
			unsigned cmd, void *data,
			struct minirpc_message **result)
{
	struct minirpc_message *msg;
	xdrproc_t type;
	int ret;
	
	conn->set->protocol->reply_info(cmd, &type, NULL);
	ret=format_message(type, data, &msg);
	if (ret)
		return ret;
	msg->hdr.sequence=sequence;
	msg->hdr.status=MINIRPC_OK;
	msg->hdr.cmd=cmd;
	*result=msg;
	return MINIRPC_OK;
}

int format_error_reply(struct minirpc_connection *conn, unsigned sequence,
			unsigned cmd, int err, struct minirpc_message **result)
{
	struct minirpc_message *msg;
	int ret;
	
	ret=format_message(xdr_void, data, &msg);
	if (ret)
		return ret;
	msg->hdr.sequence=sequence;
	msg->hdr.status=err;
	msg->hdr.cmd=cmd;
	*result=msg;
	return MINIRPC_OK;
}

int unformat_request(struct minirpc_connection *conn, unsigned cmd,
			struct minirpc_message *msg, void **result)
{
	xdrproc_t type;
	unsigned size;
	
	conn->set->protocol->request_info(cmd, &type, &size);
	return unformat_message(type, size, msg, result);
}

int unformat_reply(struct minirpc_connection *conn, unsigned cmd,
			struct minirpc_message *msg, void **result)
{
	xdrproc_t type;
	unsigned size;
	
	if (msg->hdr.status) {
		type=xdr_void;
		size=0;
	} else {
		conn->set->protocol->reply_info(cmd, &type, &size);
	}
	return unformat_message(type, size, msg, result);
}
