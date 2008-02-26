AM_CPPFLAGS = -I..
AM_CFLAGS = -Wall -Wstrict-prototypes
AM_LDFLAGS = -L.. -lminirpc -lpthread

GEN_C = proto_minirpc.c proto_xdr.c
GEN_H = proto_minirpc.h proto_client.h proto_server.h proto_xdr.h
GEN = $(GEN_C) $(GEN_H)
MAINTAINERCLEANFILES = $(GEN)
BUILT_SOURCES = $(GEN_H)

check_LIBRARIES = libcommon.a
libcommon_a_SOURCES  = common.c common.h client_sync.c client_async.c
libcommon_a_SOURCES += server_sync.c server_async.c $(GEN)
LDADD = libcommon.a

check_PROGRAMS  = test_basic
check_PROGRAMS += test_async_server
check_PROGRAMS += test_async_client

TESTS = $(check_PROGRAMS)

%_minirpc.c %_minirpc.h %_client.h %_server.h %_xdr.c %_xdr.h: %.mx ../minirpcgen.in
	../minirpcgen -o $(<:%.mx=%) $(filter-out %.in,$^)
