#ifndef PROTOCOL_H
#define PROTOCOL_H

struct minirpc_conn_set;
struct minirpc_connection;
struct minirpc_protocol;

int minirpc_conn_set_alloc(struct minirpc_conn_set **new_set, int is_server,
			request_fn *func, int expected_fds,
			unsigned conn_buckets, unsigned msg_buckets,
			unsigned msg_buf_len);
void minirpc_conn_set_free(struct minirpc_conn_set *set);

int minirpc_conn_add(struct minirpc_connection **new_conn,
			struct minirpc_conn_set *set, int fd, void *data);
void minirpc_conn_remove(struct minirpc_connection *conn);

#endif
