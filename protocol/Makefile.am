AM_CFLAGS = -Wall -Wstrict-prototypes -Wmissing-prototypes

#flow.c flow.h: $(ASNFILES) genflow.pl
#	./genflow.pl -o flow $(filter-out %.pl,$^)
#BUILT_SOURCES = flow.h

SUBDIRS = test

noinst_LTLIBRARIES = libisrproto.la
libisrproto_la_SOURCES = connection.c message.c
libisrproto_la_SOURCES += hash.h list.h protocol.h internal.h
libisrproto_la_SOURCES += flow.c flow.h
libisrproto_la_LDFLAGS = -static
