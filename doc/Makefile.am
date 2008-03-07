SRCFILES  = minirpc/minirpc.h doc/example_minirpc.h doc/example_client.h
SRCFILES += doc/example_server.h doc/example_xdr.h
SRCPATHS  = $(foreach file,$(SRCFILES),$(top_srcdir)/$(file))

html-local: doxygen.stamp

doxygen.stamp: Doxyfile $(SRCPATHS)
	SRCFILES="$(SRCPATHS)" DOCPATH=$(srcdir) doxygen Doxyfile >/dev/null
	touch doxygen.stamp

maintainer-clean-local:
	rm -rf doxygen.stamp html
