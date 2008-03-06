SRCFILES = minirpc/minirpc.h
SRCPATHS = $(foreach file,$(SRCFILES),$(top_srcdir)/$(file))

html-local: doxygen.stamp

doxygen.stamp: Doxyfile $(SRCPATHS)
	SRCPATHS=$(SRCPATHS) doxygen Doxyfile >/dev/null
	touch doxygen.stamp

maintainer-clean-local:
	rm -rf doxygen.stamp html man
