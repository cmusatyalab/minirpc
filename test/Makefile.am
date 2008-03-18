AM_CPPFLAGS = -I$(top_srcdir)
AM_CFLAGS = -Wall -Wstrict-prototypes
AM_LDFLAGS = -L$(top_builddir)/lib -lminirpc -lpthread

GEN_C = proto_minirpc.c proto_xdr.c
GEN_H = proto_minirpc.h proto_client.h proto_server.h proto_xdr.h
GEN = $(GEN_C) $(GEN_H)
MAINTAINERCLEANFILES = $(GEN)
BUILT_SOURCES = $(GEN_H)

check_LIBRARIES = libcommon.a
libcommon_a_SOURCES  = common.c common.h client_sync.c client_async.c
libcommon_a_SOURCES += server_sync.c server_async.c $(GEN)
LDADD = libcommon.a

check_SCRIPTS = runtest.sh
CLEANFILES = $(check_SCRIPTS)
EXTRA_DIST = $(check_SCRIPTS:=.in)
$(check_SCRIPTS): %: %.in Makefile
	@echo "Generating $@ from $<"
	@sed -e "s=!!ENABLE_VALGRIND!!=$(enable_valgrind)=g" \
				-e "s=!!VALGRIND!!=$(VALGRIND)=g" \
				-e "s=!!LIBTOOL!!=$(LIBTOOL)=g" $< > $@
	@chmod +x $@

check_PROGRAMS  = test_basic
check_PROGRAMS += test_async_server
check_PROGRAMS += test_async_client
check_PROGRAMS += test_async_both
check_PROGRAMS += test_sequential_clients
check_PROGRAMS += test_simultaneous_clients
check_PROGRAMS += test_bind_fd
check_PROGRAMS += test_errors
check_PROGRAMS += test_max_buf
check_PROGRAMS += test_protocol_err

TESTS_ENVIRONMENT = $(builddir)/runtest.sh
TESTS = $(check_PROGRAMS)

%_minirpc.c %_minirpc.h %_client.h %_server.h %_xdr.c %_xdr.h: %.mx $(top_builddir)/minirpcgen
	$(top_builddir)/minirpcgen -o $(<:%.mx=%) $(filter-out %minirpcgen,$^)
