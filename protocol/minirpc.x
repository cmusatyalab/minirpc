enum minirpc_status {
	MINIRPC_REPLY			=  0,
	MINIRPC_REQUEST			= -1,
	MINIRPC_NO_HANDLER		= -2,
	MINIRPC_PROCEDURE_UNKNOWN	= -3,
	MINIRPC_MESSAGE_TOO_LARGE	= -4
};

struct minirpc_header {
	unsigned sequence;
	int status;
	unsigned cmd;
	unsigned arglen;
};
