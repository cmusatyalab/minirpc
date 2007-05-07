AM_CFLAGS = -Wall -Wstrict-prototypes -Wmissing-prototypes

GEN_C = openisr_xdr.c minirpc_xdr.c
GEN_H = openisr_xdr.h minirpc_xdr.h
GEN = $(GEN_C) $(GEN_H)
MAINTAINERCLEANFILES = $(GEN)
BUILT_SOURCES = $(GEN_H)

%_xdr.c: %.x
	rm -f $@
	rpcgen -M -N -c $^ | \
		sed -e 's/"$(^:%.x=%\.h)"/"$(^:%.x=%_xdr.h)"/g' > $@ || \
		(rm $@ && false)

%_xdr.h: %.x
	rm -f $@
	rpcgen -M -N -h -o $@ $^

%.x: %.mx stubgen.pl
	./stubgen.pl -o $(@:%.x=%) $(filter-out %.pl,$^)

SUBDIRS = test

noinst_LTLIBRARIES = libisrproto.la
libisrproto_la_SOURCES = connection.c message.c
libisrproto_la_SOURCES += hash.h list.h protocol.h internal.h
libisrproto_la_SOURCES += $(GEN)
libisrproto_la_LDFLAGS = -static
