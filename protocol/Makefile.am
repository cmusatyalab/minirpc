AM_CFLAGS = -Wall -Wstrict-prototypes -Wmissing-prototypes

GEN_C = openisr_xdr.c openisr_minirpc.c
GEN_H = openisr_xdr.h openisr_minirpc.h openisr_client.h openisr_server.h
GEN = $(GEN_C) $(GEN_H)
MAINTAINERCLEANFILES = $(GEN)
BUILT_SOURCES = $(GEN_H)

%_minirpc.c %_minirpc.h %_client.h %_server.h %_xdr.c %_xdr.h: %.mx ../minirpc/minirpcgen.pl
	../minirpc/minirpcgen.pl -o $(<:%.mx=%) $(filter-out %.pl,$^)

noinst_LTLIBRARIES = libisrproto.la
libisrproto_la_SOURCES = $(GEN)
libisrproto_la_LDFLAGS = -static
