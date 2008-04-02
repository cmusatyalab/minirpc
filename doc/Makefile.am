DOCHEADERS = $(top_srcdir)/minirpc/minirpc.h
DOCFILES   = main.dox common.dox example.dox
DOCFILES  += example_minirpc.h example_client.h example_server.h example_xdr.h
EXAMPLES   = example.mx

SRCPATHS  = $(foreach file,$(DOCFILES),$(srcdir)/$(file)) $(DOCHEADERS)
EXTRA_DIST = $(DOCFILES) $(EXAMPLES)

if HAVE_DOXYGEN
doxygen.stamp: $(srcdir)/Doxyfile $(SRCPATHS) $(EXAMPLES)
	rm -rf html
	SRCFILES="$(SRCPATHS)" DOCPATH=$(srcdir) $(DOXYGEN) $< >/dev/null
	touch doxygen.stamp
else
doxygen.stamp:
	@echo "doxygen command not found.  Documentation will not be built."
endif

html-local: doxygen.stamp

installdirs-local:
	$(MKDIR_P) $(DESTDIR)$(htmldir)

install-html-local: html-local installdirs-local
	-[ -d html ] && $(INSTALL_DATA) html/* $(DESTDIR)$(htmldir)

uninstall-local:
	rm -f $(DESTDIR)$(htmldir)/*

clean-local:
	rm -rf doxygen.stamp html
