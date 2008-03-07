SRCFILES  = main.dox common.dox example.dox
SRCFILES += $(top_srcdir)/minirpc/minirpc.h
SRCFILES += example_minirpc.h example_client.h example_server.h example_xdr.h

html-local: doxygen.stamp

doxygen.stamp: Doxyfile $(SRCFILES)
	SRCFILES="$(SRCFILES)" DOCPATH=$(srcdir) doxygen Doxyfile >/dev/null
	touch doxygen.stamp

maintainer-clean-local:
	rm -rf doxygen.stamp html
