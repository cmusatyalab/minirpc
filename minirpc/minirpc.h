#ifndef MINIRPC_H
#define MINIRPC_H

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

#endif
