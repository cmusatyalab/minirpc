AM_CPPFLAGS = -I..
AM_CFLAGS = -Wall -Wstrict-prototypes
AM_LDFLAGS = -L.. -lminirpc -lpthread

GEN_C = proto_minirpc.c proto_xdr.c
GEN_H = proto_minirpc.h proto_client.h proto_server.h proto_xdr.h
GEN = $(GEN_C) $(GEN_H)
MAINTAINERCLEANFILES = $(GEN)
BUILT_SOURCES = $(GEN_H)

check_PROGRAMS = test_basic test_async_server test_async_client
TESTS = $(check_PROGRAMS)

COMMON  = common.c common.h client_sync.c client_async.c
COMMON += server_sync.c server_async.c $(GEN)

test_basic_SOURCES = test_basic.c $(COMMON)
test_async_server_SOURCES = test_async_server.c $(COMMON)
test_async_client_SOURCES = test_async_client.c $(COMMON)

%_minirpc.c %_minirpc.h %_client.h %_server.h %_xdr.c %_xdr.h: %.mx ../minirpcgen.in
	../minirpcgen -o $(<:%.mx=%) $(filter-out %.in,$^)
