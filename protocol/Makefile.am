AM_CPPFLAGS = -Iclasses -Isupport

include stamp
stamp: protocol.asn
	./compile.sh $<
MAINTAINERCLEANFILES = stamp

SUBDIRS = support classes

noinst_LTLIBRARIES = libisrproto.la
libisrproto_la_SOURCES = 
libisrproto_la_LIBADD = support/libsupport.la classes/libclasses.la
libisrproto_la_LDFLAGS = -static

# XXX autoreconf fails if make fragments don't exist
