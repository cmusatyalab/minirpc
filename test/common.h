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

#ifndef TEST_COMMON_H
#define TEST_COMMON_H

#include <stdlib.h>
#include <errno.h>
#include <pthread.h>
#include <minirpc/minirpc.h>
#include "proto_client.h"
#include "proto_server.h"

/* common.c */
void _message(const char *file, int line, const char *func, const char *fmt,
			...);
#define message(args...) _message(__FILE__, __LINE__, __func__, args)
#define test_fail(args...) message(args)
#define die(args...) do {message(args); exit(1);} while (0)
struct mrpc_conn_set *spawn_server(int *listen_port,
			const struct mrpc_config *config, void *set_data,
			int threads);
void disconnect_fatal(void *conn_data, enum mrpc_disc_reason reason);
void disconnect_normal(void *conn_data, enum mrpc_disc_reason reason);
void disconnect_ioerr(void *conn_data, enum mrpc_disc_reason reason);

/* client_sync.c */
void loop_int_sync(struct mrpc_connection *conn);
void check_int_sync(struct mrpc_connection *conn);
void error_sync(struct mrpc_connection *conn);
void notify_sync(struct mrpc_connection *conn);
void invalidate_sync(struct mrpc_connection *conn);
void sync_client_run(struct mrpc_connection *conn);

/* server_sync.c */
void *sync_server_accept(void *set_data, struct mrpc_connection *conn,
			struct sockaddr *from, socklen_t from_len);

#endif
