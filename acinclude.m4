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


# CHECK_COMPILER_OPTION([OPTION], [SYMBOL])
# If the compiler supports the command line option OPTION, define the cpp
# symbol SYMBOL to 1.  Also, define an automake conditional named SYMBOL
# indicating whether the option is supported.
# -----------------------------------------------------------------------
AC_DEFUN([CHECK_COMPILER_OPTION], [
	AC_MSG_CHECKING([if compiler supports $1])
	saved_cflags="$CFLAGS"
	CFLAGS="$saved_cflags $1"
	AC_COMPILE_IFELSE([AC_LANG_SOURCE([])], [result=ok], [result=bad])
	if test z$result = zok ; then
		AC_MSG_RESULT([yes])
		AC_DEFINE([$2], 1, [Define to 1 if your compiler supports the $1 option.])
	else
		AC_MSG_RESULT([no])
	fi
	AM_CONDITIONAL([$2], [test z$result = zok])
	CFLAGS="$saved_cflags"
])
