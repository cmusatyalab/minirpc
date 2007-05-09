enum minirpc_status {
	MINIRPC_OK			=  0,
	MINIRPC_REQUEST			= -1,
	MINIRPC_ENCODING_ERR		= -2,
	MINIRPC_NOMEM			= -3,
	MINIRPC_PROCEDURE_UNAVAIL	= -4,
	MINIRPC_PROTOCOL_MISMATCH	= -5,
	MINIRPC_DEFER			= -6
};

struct minirpc_header {
	unsigned sequence;
	int status;
	unsigned cmd;
	unsigned datalen;
};

#ifdef RPC_HDR
%#define MINIRPC_HEADER_LEN	16
#endif
