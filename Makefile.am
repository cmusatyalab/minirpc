bin_SCRIPTS = minirpcgen
CLEANFILES = minirpcgen

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = minirpc.pc

SUBDIRS = . minirpc lib doc test
EXTRA_DIST = LICENSE CHANGES minirpcgen.in

minirpcgen: minirpcgen.in Makefile
	@echo "Generating $@ from $<"
	@sed -e "s=!!RPCGEN!!=$(RPCGEN)=g" \
				-e "s=!!PERLPATH!!=$(PERL)=g" \
				-e "s=!!VERSION!!=$(PACKAGE_VERSION)=g" $< > $@
	@chmod +x $@
