/*
 * miniRPC - TCP RPC library with asynchronous operations and TLS support
 *
 * Copyright (C) 2007 Carnegie Mellon University
 *
 * This software is distributed under the terms of the Eclipse Public License,
 * Version 1.0 which can be found in the file named LICENSE.  ANY USE,
 * REPRODUCTION OR DISTRIBUTION OF THIS SOFTWARE CONSTITUTES RECIPIENT'S
 * ACCEPTANCE OF THIS AGREEMENT
 */

#ifndef MINIRPC_H
#define MINIRPC_H

#include <sys/types.h>
#include <sys/socket.h>
#include <apr_pools.h>
#include <apr_network_io.h>
#include <apr_errno.h>

struct mrpc_protocol;
struct mrpc_conn_set;
struct mrpc_connection;
struct mrpc_message;

enum mrpc_status_codes {
	MINIRPC_OK			=  0,
	MINIRPC_PENDING			= -1,
	MINIRPC_ENCODING_ERR		= -2,
	MINIRPC_NOMEM			= -3,
	MINIRPC_PROCEDURE_UNAVAIL	= -4,
	MINIRPC_INVALID_ARGUMENT	= -5,
	MINIRPC_INVALID_PROTOCOL	= -6,
	MINIRPC_NETWORK_FAILURE		= -7,
};
typedef int mrpc_status_t;

struct mrpc_config {
	const struct mrpc_protocol *protocol;
	unsigned max_fds;
	unsigned msg_max_buf_len;
	unsigned listen_backlog;
};

enum mrpc_disc_reason {
	MRPC_DISC_CLOSED,
	MRPC_DISC_IOERR,
	MRPC_DISC_DESYNC
};

struct mrpc_set_operations {
	void *(*accept)(void *set_data, struct mrpc_connection *conn,
				apr_sockaddr_t *from);
	void (*disconnect)(void *conn_data, enum mrpc_disc_reason reason);
	void (*ioerr)(void *conn_data, char *message);
};

/* connection.c */
apr_status_t mrpc_conn_set_alloc(struct mrpc_conn_set **new_set,
			const struct mrpc_config *config,
			const struct mrpc_set_operations *ops,
			void *set_data, apr_pool_t *parent_pool);
void mrpc_conn_set_free(struct mrpc_conn_set *set);
apr_status_t mrpc_conn_set_alloc_subpool(apr_pool_t **new_pool,
			struct mrpc_conn_set *set);
apr_status_t mrpc_connect(struct mrpc_connection **new_conn,
			struct mrpc_conn_set *set, char *host, unsigned port,
			void *data);
apr_status_t mrpc_listen(struct mrpc_conn_set *set, char *listenaddr,
			unsigned port, int *bound);
apr_status_t mrpc_bind_fd(struct mrpc_connection **new_conn,
			struct mrpc_conn_set *set, apr_socket_t *sock,
			apr_pool_t *conn_pool, void *data);
void mrpc_conn_close(struct mrpc_connection *conn);

/* event.c */
apr_status_t mrpc_get_event_fd(apr_file_t **file, struct mrpc_conn_set *set,
			apr_pool_t *pool);
int mrpc_dispatch_one(struct mrpc_conn_set *set);
int mrpc_dispatch_all(struct mrpc_conn_set *set);
apr_status_t mrpc_dispatch_loop(struct mrpc_conn_set *set);
mrpc_status_t mrpc_plug_conn(struct mrpc_connection *conn);
mrpc_status_t mrpc_unplug_conn(struct mrpc_connection *conn);
mrpc_status_t mrpc_unplug_message(struct mrpc_message *msg);

#endif
