#
# acinclude.m4 - autoconf macros for miniRPC
#
# Copyright (C) 2007 Carnegie Mellon University
#
# This software is distributed under the terms of the Eclipse Public
# License, Version 1.0 which can be found in the file named LICENSE.Eclipse.
# ANY USE, REPRODUCTION OR DISTRIBUTION OF THIS SOFTWARE CONSTITUTES
# RECIPIENT'S ACCEPTANCE OF THIS AGREEMENT
#

# FIND_LIBRARY([PRETTY_NAME], [LIBRARY_NAME], [LIBRARY_FUNCTION],
#              [HEADER_LIST], [PATH_LIST])
# The paths in PATH_LIST are searched to determine whether they contain the
# first header in HEADER_LIST.  If so, that path is added to the include and
# library paths.  Then the existence of all headers in HEADER_LIST, and of
# LIBRARY_FUNCTION within LIBRARY_NAME, is validated.  $FOUND_PATH is
# set to the name of the directory we've decided on.
# -----------------------------------------------------------------------------
AC_DEFUN([FIND_LIBRARY], [
	AC_MSG_CHECKING([for $1])
	for firsthdr in $4; do break; done
	found_lib=0
	for path in $5
	do
		if test -r $path/include/$firsthdr ; then
			found_lib=1
			CPPFLAGS="$CPPFLAGS -I${path}"
			LDFLAGS="$LDFLAGS -L${path}"
			AC_MSG_RESULT([$path])
			break
		fi
	done
	
	if test $found_lib = 0 ; then
		AC_MSG_RESULT([not found])
		AC_MSG_ERROR([cannot find $1 in $5])
	fi
	
	# By default, AC_CHECK_LIB([foo], ...) will add "-lfoo" to the linker
	# flags for ALL programs and libraries, which is not what we want.
	# We put a no-op in the third argument to disable this behavior.
	AC_CHECK_HEADERS([$4],, AC_MSG_FAILURE([cannot find $1 headers]))
	AC_CHECK_LIB([$2], [$3], :, AC_MSG_FAILURE([cannot find $1 library]))
	FOUND_PATH=$path
])


# CHECK_COMPILE([MESSAGE], [TEST_PROGRAM], [DEFINE_IF_SUCCEEDS],
#               [DEFINE_DESCRIPTION])
# --------------------------------------------------------------
AC_DEFUN([CHECK_COMPILE], [
	AC_MSG_CHECKING($1)
	AC_COMPILE_IFELSE([$2], [result=ok], [result=bad])
	if test z$result = zok ; then
		AC_MSG_RESULT([yes])
		AC_DEFINE([$3], 1, [$4])
	else
		AC_MSG_RESULT([no])
	fi
])

# CHECK_COMPILER_OPTION([OPTION], [DEFINE_IF_SUCCEEDS],
#		[SUBST_OPTION_IF_SUCCEEDS])
# -----------------------------------------------------------------------
AC_DEFUN([CHECK_COMPILER_OPTION], [
	AC_MSG_CHECKING([if compiler supports $1])
	saved_cflags="$CFLAGS"
	CFLAGS="$saved_cflags $1"
	AC_COMPILE_IFELSE([AC_LANG_SOURCE([])], [result=ok], [result=bad])
	if test z$result = zok ; then
		AC_MSG_RESULT([yes])
		AC_DEFINE([$2], 1, [Define to 1 if your compiler supports the $1 option.])
		AC_SUBST([$3], [$1])
	else
		AC_MSG_RESULT([no])
	fi
	CFLAGS="$saved_cflags"
])
