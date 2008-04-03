/*
 * miniRPC - TCP RPC library with asynchronous operations
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
#define die(args...) do {message(args); exit(1);} while (0)
void start_monitored_dispatcher(struct mrpc_conn_set *set);
struct mrpc_conn_set *spawn_server(unsigned *listen_port,
			const struct mrpc_protocol *protocol,
			mrpc_accept_fn accept, void *set_data, int threads);
void disconnect_fatal(void *conn_data, enum mrpc_disc_reason reason);
void disconnect_normal(void *conn_data, enum mrpc_disc_reason reason);
void disconnect_normal_no_unref(void *conn_data, enum mrpc_disc_reason reason);
void disconnect_ioerr(void *conn_data, enum mrpc_disc_reason reason);
void disconnect_user(void *conn_data, enum mrpc_disc_reason reason);
void disconnect_user_unref(void *conn_data, enum mrpc_disc_reason reason);
void dispatcher_barrier(void);
void handle_ioerr(void *conn_private, char *msg);
void expect_disconnects(int user, int normal, int ioerr);
void expect_ioerrs(int count);
#define expect(cmd, result) do {					\
		int _ret=cmd;						\
		int _expected=result;					\
		if (_ret != _expected)					\
			die("%s returned %d (%s), expected %d (%s)",	\
						#cmd, _ret,		\
						strerror(_ret),		\
						_expected,		\
						strerror(_expected));	\
	} while (0)

/* client_sync.c */
void loop_int_sync(struct mrpc_connection *conn);
void check_int_sync(struct mrpc_connection *conn);
void error_sync(struct mrpc_connection *conn);
void notify_sync(struct mrpc_connection *conn);
void trigger_callback_sync(struct mrpc_connection *conn);
void invalidate_sync(struct mrpc_connection *conn);
mrpc_status_t send_buffer_sync(struct mrpc_connection *conn);
mrpc_status_t recv_buffer_sync(struct mrpc_connection *conn);
void msg_buffer_sync(struct mrpc_connection *conn);
void sync_client_set_ops(struct mrpc_connection *conn);
void sync_client_run(struct mrpc_connection *conn);

/* client_async.c */
void loop_int_async(struct mrpc_connection *conn);
void check_int_async(struct mrpc_connection *conn);
void error_async(struct mrpc_connection *conn);
void trigger_callback_async(struct mrpc_connection *conn);
void async_client_init(void);
void async_client_finish(void);
void async_client_set_ops(struct mrpc_connection *conn);
void async_client_run(struct mrpc_connection *conn);

/* server_sync.c */
void sync_server_set_ops(struct mrpc_connection *conn);
void *sync_server_accept(void *set_data, struct mrpc_connection *conn,
			struct sockaddr *from, socklen_t from_len);

/* server_async.c */
void async_server_init(void);
void async_server_set_ops(struct mrpc_connection *conn);
void *async_server_accept(void *set_data, struct mrpc_connection *conn,
			struct sockaddr *from, socklen_t from_len);

#endif
