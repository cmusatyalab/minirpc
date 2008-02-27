#!/bin/sh

export G_DEBUG="gc-friendly"
export G_SLICE="always-malloc"
export MALLOC_CHECK_=2
ulimit -c 0

$1
exit $?
