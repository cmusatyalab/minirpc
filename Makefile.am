AM_CPPFLAGS = -D_GNU_SOURCE
AM_CFLAGS = -Wall -Wstrict-prototypes -Wmissing-prototypes
AM_LDFLAGS = -lpthread

if HAVE_VISIBILITY
AM_CFLAGS += -fvisibility=hidden
endif

GEN_C = minirpc_xdr.c
GEN_H = minirpc_xdr.h
GEN = $(GEN_C) $(GEN_H)
MAINTAINERCLEANFILES = $(GEN)
BUILT_SOURCES = $(GEN_H)
EXTRA_DIST = LICENSE CHANGES

%_xdr.c: %.x
	rm -f $@
	rpcgen -c $^ | \
		sed -e 's/"$(^:%.x=%\.h)"/"$(^:%.x=%_xdr.h)"/g' > $@ || \
		(rm $@ && false)

%_xdr.h: %.x
	rm -f $@
	rpcgen -h -o $@ $^

SUBDIRS = test

lib_LTLIBRARIES = libminirpc.la
libminirpc_la_SOURCES = connection.c message.c event.c serialize.c xdr_len.c
libminirpc_la_SOURCES += internal.h $(GEN)
EXTHEADERS = minirpc.h protocol.h list.h hash.h
nobase_include_HEADERS = $(foreach hf,$(EXTHEADERS),minirpc/$(hf))
