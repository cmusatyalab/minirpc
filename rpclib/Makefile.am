EXTRA_PROGRAMS = client
client_SOURCES = openisr_clnt.c openisr_xdr.c openisr.h

%.h %_clnt.c %_svc.c %_xdr.c: %.x
	rpcgen -5 -M $^
