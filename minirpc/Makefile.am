AM_CFLAGS = -Wall -Wstrict-prototypes -Wmissing-prototypes

GEN_C = minirpc_xdr.c
GEN_H = minirpc_xdr.h
GEN = $(GEN_C) $(GEN_H)
MAINTAINERCLEANFILES = $(GEN)
BUILT_SOURCES = $(GEN_H)

%_xdr.c: %.x
	rm -f $@
	rpcgen -c $^ | \
		sed -e 's/"$(^:%.x=%\.h)"/"$(^:%.x=%_xdr.h)"/g' > $@ || \
		(rm $@ && false)

%_xdr.h: %.x
	rm -f $@
	rpcgen -h -o $@ $^

lib_LTLIBRARIES = libminirpc.la
libminirpc_la_SOURCES = connection.c message.c serialize.c xdr_len.c
libminirpc_la_SOURCES += hash.h list.h minirpc.h minirpc_protocol.h internal.h
libminirpc_la_SOURCES += $(GEN)
