#!/bin/sh

export G_DEBUG="gc-friendly"
export G_SLICE="always-malloc"

if [ -z "$USE_VALGRIND" ] ; then
	export MALLOC_CHECK_=2
	$1
else
	../libtool --mode=execute valgrind --error-exitcode=1 \
			--suppressions=valgrind.supp --leak-check=full -q $1
fi
exit $?
