enum mrpc_status {
	MINIRPC_OK			=  0,
	MINIRPC_REQUEST			= -1,
	MINIRPC_ENCODING_ERR		= -2,
	MINIRPC_NOMEM			= -3,
	MINIRPC_PROCEDURE_UNAVAIL	= -4,
	MINIRPC_INVALID_ARGUMENT	= -5,
	MINIRPC_DEFER			= -6
};

struct mrpc_header {
	unsigned sequence;
	int status;
	int cmd;
	unsigned datalen;
};

#ifdef RPC_HDR
%#define MINIRPC_HEADER_LEN	16
#endif
