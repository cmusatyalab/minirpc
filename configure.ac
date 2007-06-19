#
# configure.ac - build infrastructure for miniRPC
#
# Copyright (C) 2007 Carnegie Mellon University
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
AC_COPYRIGHT([Copyright (C) 2007 Carnegie Mellon University])
AC_CONFIG_SRCDIR([connection.c])

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

# Checks for libraries.
PKG_CHECK_MODULES([APR], [apr-1])
CFLAGS="$CFLAGS $APR_CFLAGS"
LIBS="$LIBS $APR_LIBS"

# Checks for supported compiler options.
CHECK_COMPILER_OPTION([-fvisibility=hidden], [HAVE_VISIBILITY])

AC_CONFIG_FILES([minirpc.pc
		Makefile
		test/Makefile])
AC_OUTPUT
