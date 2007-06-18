AM_CPPFLAGS = -D_GNU_SOURCE
AM_CFLAGS = -Wall -Wstrict-prototypes -Wmissing-prototypes
AM_LDFLAGS = -lpthread

if HAVE_VISIBILITY
AM_CFLAGS += -fvisibility=hidden
endif

MAINTAINERCLEANFILES = minirpc_xdr.c minirpc_xdr.h
BUILT_SOURCES = minirpc_xdr.h

lib_LTLIBRARIES = libminirpc.la
libminirpc_la_SOURCES = connection.c message.c event.c serialize.c xdr_len.c
libminirpc_la_SOURCES += internal.h minirpc.x minirpc_xdr.c minirpc_xdr.h

EXTHEADERS = minirpc.h protocol.h list.h hash.h
nobase_include_HEADERS = $(foreach hf,$(EXTHEADERS),minirpc/$(hf))

bin_SCRIPTS = minirpcgen
CLEANFILES = minirpcgen

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = minirpc.pc

SUBDIRS = . test
EXTRA_DIST = LICENSE CHANGES minirpcgen.in

%_xdr.c: %.x
	rm -f $@
	$(RPCGEN) -c $^ | \
		sed -e 's/"$(^:%.x=%\.h)"/"$(^:%.x=%_xdr.h)"/g' > $@ || \
		(rm $@ && false)

%_xdr.h: %.x
	rm -f $@
	$(RPCGEN) -h -o $@ $^

minirpcgen: minirpcgen.in Makefile
	@echo "Generating $@ from $<"
	@sed -e "s=!!RPCGEN!!=$(RPCGEN)=g" \
				-e "s=!!VERSION!!=$(PACKAGE_VERSION)=g" $< > $@
	@chmod +x $@

# Libtool sets the .la and .so files executable, for reasons passing
# understanding.
install-exec-hook:
	chmod -x $(DESTDIR)$(libdir)/libminirpc.so
	chmod -x $(DESTDIR)$(libdir)/libminirpc.la
