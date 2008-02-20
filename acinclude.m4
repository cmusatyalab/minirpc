#
# acinclude.m4 - autoconf macros for miniRPC
#
# Copyright (C) 2007 Carnegie Mellon University
#
# This software is distributed under the terms of the Eclipse Public License,
# Version 1.0 which can be found in the file named LICENSE.  ANY USE,
# REPRODUCTION OR DISTRIBUTION OF THIS SOFTWARE CONSTITUTES RECIPIENT'S
# ACCEPTANCE OF THIS AGREEMENT
#

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


# CHECK_LINK([MESSAGE], [TEST_PROGRAM], [AM_CONDITIONAL_AND_DEFINE_NAME],
#               [DEFINE_DESCRIPTION])
# -----------------------------------------------------------------------
AC_DEFUN([CHECK_LINK], [
	AC_MSG_CHECKING($1)
	AC_LINK_IFELSE([$2], [result=ok], [result=bad])
	if test z$result = zok ; then
		AC_MSG_RESULT([yes])
		AC_DEFINE([$3], 1, [$4])
	else
		AC_MSG_RESULT([no])
	fi
	AM_CONDITIONAL([$3], [test z$result = zok])
])
