struct mrpc_header {
	unsigned sequence;
	int status;
	int cmd;
	unsigned datalen;
};

#ifdef RPC_HDR
%#define MINIRPC_HEADER_LEN	16
#endif
