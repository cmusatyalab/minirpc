AM_CPPFLAGS = -Iclasses -Isupport

include protocol.mk

noinst_LIBRARIES = libisrproto.a
libisrproto_a_SOURCES = $(SUPPORT_FILES) $(CLASS_FILES)
MAINTAINERCLEANFILES = classes support

protocol.mk: protocol.asn
	./compile.sh $<

# XXX don't build objects in root dir
