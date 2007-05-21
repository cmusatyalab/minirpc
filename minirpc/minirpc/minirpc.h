#ifndef MINIRPC_H
#define MINIRPC_H

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

struct mrpc_protocol;
struct mrpc_conn_set;
struct mrpc_connection;
struct mrpc_message;

/* connection.c */
int mrpc_conn_set_alloc(struct mrpc_conn_set **new_set,
			const struct mrpc_protocol *protocol, int expected_fds,
			unsigned conn_buckets, unsigned msg_buckets,
			unsigned msg_max_buf_len);
void mrpc_conn_set_free(struct mrpc_conn_set *set);
int mrpc_conn_add(struct mrpc_connection **new_conn, struct mrpc_conn_set *set,
			int fd, void *data);
void mrpc_conn_remove(struct mrpc_connection *conn);

/* message.c */
int mrpc_get_event_fd(struct mrpc_conn_set *set);
int mrpc_dispatch_one(struct mrpc_conn_set *set);
int mrpc_dispatch_all(struct mrpc_conn_set *set);
int mrpc_dispatch_loop(struct mrpc_conn_set *set);
mrpc_status_t mrpc_plug_conn(struct mrpc_connection *conn);
mrpc_status_t mrpc_unplug_conn(struct mrpc_connection *conn);
mrpc_status_t mrpc_unplug_event(struct mrpc_message *msg);

#endif
