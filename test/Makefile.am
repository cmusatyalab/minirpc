AM_CPPFLAGS = -I..
AM_CFLAGS = -Wall -Wstrict-prototypes
AM_LDFLAGS = -L.. -lminirpc -lpthread

GEN_C = proto_minirpc.c proto_xdr.c
GEN_H = proto_minirpc.h proto_client.h proto_server.h proto_xdr.h
GEN = $(GEN_C) $(GEN_H)
MAINTAINERCLEANFILES = $(GEN)
BUILT_SOURCES = $(GEN_H)

check_PROGRAMS = test_001_basic
TESTS = $(check_PROGRAMS)

COMMON = common.c common.h client_sync.c server_sync.c $(GEN)

test_001_basic_SOURCES = test_001_basic.c $(COMMON)

%_minirpc.c %_minirpc.h %_client.h %_server.h %_xdr.c %_xdr.h: %.mx ../minirpcgen.in
	../minirpcgen -o $(<:%.mx=%) $(filter-out %.in,$^)
