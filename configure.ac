#
# configure.ac - build infrastructure for miniRPC
#
# Copyright (C) 2007-2008 Carnegie Mellon University
#
# This software is distributed under the terms of the Eclipse Public License,
# Version 1.0 which can be found in the file named LICENSE.  ANY USE,
# REPRODUCTION OR DISTRIBUTION OF THIS SOFTWARE CONSTITUTES RECIPIENT'S
# ACCEPTANCE OF THIS AGREEMENT
#

## Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)
AC_INIT([miniRPC], [0.1])
AM_INIT_AUTOMAKE([foreign])
AC_COPYRIGHT([Copyright (C) 2007-2008 Carnegie Mellon University])
AC_CONFIG_SRCDIR([lib/connection.c])

# Generating a config.h prevents the introduction of several lines' worth of
# -D flags on every gcc command line
AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_MAKE_SET
AC_PROG_LIBTOOL

AC_PATH_PROG([RPCGEN], [rpcgen])
AC_ARG_VAR([RPCGEN], [RPC compiler command])
if test z$RPCGEN = z ; then
	AC_MSG_ERROR([could not locate rpcgen])
fi

AC_PATH_PROG([PERL], [perl])
AC_ARG_VAR([PERL], [path to Perl])
if test z$PERL = z ; then
	AC_MSG_ERROR([Perl not found])
fi

AC_PATH_PROG([DOXYGEN], [doxygen])
AC_ARG_VAR([DOXYGEN], [path to Doxygen])
AM_CONDITIONAL([HAVE_DOXYGEN], [test z$DOXYGEN != z])

AC_ARG_ENABLE([valgrind], [AS_HELP_STRING([--enable-valgrind],
			[run test suite under Valgrind [default=no]])],,
			[enable_valgrind=no])
AC_ARG_VAR([VALGRIND], [path to Valgrind])
AC_MSG_CHECKING([whether to use Valgrind])
if test z$enable_valgrind = zyes ; then
	AC_MSG_RESULT([yes])
	AC_PATH_PROG([VALGRIND], [valgrind])
	if test z$VALGRIND = z ; then
		AC_MSG_ERROR([could not locate valgrind])
	fi
else
	AC_MSG_RESULT([no])
fi
AC_SUBST([enable_valgrind])

# Checks for libraries.
PKG_CHECK_MODULES([GLIB], [glib-2.0])
PKG_CHECK_MODULES([GTHREAD], [gthread-2.0])
CFLAGS="$CFLAGS $GLIB_CFLAGS $GTHREAD_CFLAGS"
LIBS="$LIBS $GLIB_LIBS $GTHREAD_LIBS"

# Checks for supported compiler options.
CHECK_COMPILER_OPTION([-fvisibility=hidden], [HAVE_VISIBILITY])

# Check for epoll support.
CHECK_LINK([for epoll], [AC_LANG_PROGRAM([#include <sys/epoll.h>],
			[int fd=epoll_create(8)])], HAVE_EPOLL,
			[Define if the C library supports the epoll system calls.])

# Generate the library version code.
VERSION_CODE=`echo $PACKAGE_VERSION | tr . ' ' | {
	read maj min rev
	test -z "$rev" && rev=0
	echo $((($maj << 16) + ($min << 8) + $rev))
}`
AC_SUBST([VERSION_CODE])

AC_CONFIG_FILES([minirpc.pc
		Makefile
		doc/Makefile
		minirpc/Makefile
		lib/Makefile
		test/Makefile])
AC_OUTPUT
