/*
 * miniRPC - TCP RPC library with asynchronous operations and TLS support
 *
 * Copyright (C) 2007-2008 Carnegie Mellon University
 *
 * This software is distributed under the terms of the Eclipse Public License,
 * Version 1.0 which can be found in the file named LICENSE.  ANY USE,
 * REPRODUCTION OR DISTRIBUTION OF THIS SOFTWARE CONSTITUTES RECIPIENT'S
 * ACCEPTANCE OF THIS AGREEMENT
 */

#define MINIRPC_INTERNAL
#include "internal.h"

exported int mrpc_init(void)
{
	if (!g_thread_supported())
		g_thread_init(NULL);
	return 0;
}
