AM_CPPFLAGS = -Iclasses -Isupport

# autogen.sh reads this
ASNFILES = protocol.asn

include stamp
stamp: $(ASNFILES)
	./compile.sh $^
MAINTAINERCLEANFILES = stamp

flow.c flow.h: $(ASNFILES)
	./genflow.pl -o flow $^
BUILT_SOURCES = flow.h

SUBDIRS = support classes

noinst_LTLIBRARIES = libisrproto.la
libisrproto_la_SOURCES = mux.c messenger.c hash.h list.h protocol.h
libisrproto_la_SOURCES += flow.c flow.h
libisrproto_la_LIBADD = support/libsupport.la classes/libclasses.la
libisrproto_la_LDFLAGS = -static
