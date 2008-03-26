AM_CPPFLAGS = -I$(top_srcdir) -D_GNU_SOURCE
AM_CFLAGS = -Wall -Wstrict-prototypes -Wmissing-prototypes -pthread
AM_LDFLAGS = -lpthread

if HAVE_VISIBILITY
AM_CFLAGS += -fvisibility=hidden
endif

MAINTAINERCLEANFILES = minirpc_xdr.c minirpc_xdr.h
BUILT_SOURCES = minirpc_xdr.h

lib_LTLIBRARIES = libminirpc.la
libminirpc_la_SOURCES = config.c connection.c message.c event.c serialize.c
libminirpc_la_SOURCES += xdr_len.c selfpipe.c pollset.c pollset_poll.c util.c
libminirpc_la_SOURCES += internal.h minirpc.x minirpc_xdr.c minirpc_xdr.h
libminirpc_la_SOURCES += pollset_impl.h
if HAVE_EPOLL
libminirpc_la_SOURCES += pollset_epoll.c
endif

%_xdr.c: %.x
	rm -f $@
	$(RPCGEN) -c $^ | \
		sed -e 's/"$(^:%.x=%\.h)"/"$(^:%.x=%_xdr.h)"/g' > $@ || \
		(rm $@ && false)

%_xdr.h: %.x
	rm -f $@
	$(RPCGEN) -h -o $@ $^

# Libtool sets the .la and .so files executable, for reasons passing
# understanding.
install-exec-hook:
	chmod -x $(DESTDIR)$(libdir)/libminirpc.so
	chmod -x $(DESTDIR)$(libdir)/libminirpc.la
