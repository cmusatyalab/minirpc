AM_CFLAGS = -Wall -Wstrict-prototypes -Wmissing-prototypes

GEN_C = minirpc_xdr.c openisr_xdr.c openisr_client.c openisr_server.c
GEN_H = minirpc_xdr.h openisr_xdr.h openisr_client.h openisr_server.h
GEN = $(GEN_C) $(GEN_H)
MAINTAINERCLEANFILES = $(GEN)
BUILT_SOURCES = $(GEN_H)

%_client.c %_client.h %_server.c %_server.h %_xdr.c %_xdr.h: %.mx minirpcgen.pl
	./minirpcgen.pl -o $(<:%.mx=%) $(filter-out %.pl,$^)

SUBDIRS = test

noinst_LTLIBRARIES = libisrproto.la
libisrproto_la_SOURCES = connection.c message.c serialize.c xdr_len.c
libisrproto_la_SOURCES += hash.h list.h minirpc.h minirpc_protocol.h internal.h
libisrproto_la_SOURCES += $(GEN)
libisrproto_la_LDFLAGS = -static
