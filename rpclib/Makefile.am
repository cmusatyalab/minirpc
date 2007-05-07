EXTRA_PROGRAMS = client
GEN = openisr_xdr.c openisr.h minirpc_xdr.c minirpc.h
client_SOURCES = $(GEN)
MAINTAINERCLEANFILES = $(GEN)

%_xdr.c: %.x
	rm -f $@
	rpcgen -M -N -DRPCGEN -c -o $@ $^

%.h: %.x
	rm -f $@
	rpcgen -M -N -DRPCGEN -h -o $@ $^
