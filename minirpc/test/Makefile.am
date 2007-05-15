AM_CPPFLAGS = -I..
AM_CFLAGS = -Wall -Wstrict-prototypes
AM_LDFLAGS = -L.. -lminirpc -lpthread

GEN_C = test_minirpc.c test_xdr.c
GEN_H = test_minirpc.h test_client.h test_server.h test_xdr.h
GEN = $(GEN_C) $(GEN_H)

EXTRA_PROGRAMS = client server
client_SOURCES = client.c $(GEN)
server_SOURCES = server.c $(GEN)

%_minirpc.c %_minirpc.h %_client.h %_server.h %_xdr.c %_xdr.h: %.mx ../minirpcgen.pl
	../minirpcgen.pl -o $(<:%.mx=%) $(filter-out %.pl,$^)
