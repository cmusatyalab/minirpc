EXTRA_PROGRAMS = client
GEN_C = openisr_xdr.c minirpc_xdr.c
GEN_H = openisr_xdr.h minirpc_xdr.h
GEN = $(GEN_C) $(GEN_H)
client_SOURCES = $(GEN)
MAINTAINERCLEANFILES = $(GEN)
BUILT_SOURCES = $(GEN_H)

%_xdr.c: %.x
	rm -f $@
	rpcgen -M -N -DRPCGEN -c $^ | \
		sed -e 's/"$(^:%.x=%\.h)"/"$(^:%.x=%_xdr.h)"/g' > $@ || \
		(rm $@ && false)

%_xdr.h: %.x
	rm -f $@
	rpcgen -M -N -DRPCGEN -h -o $@ $^
