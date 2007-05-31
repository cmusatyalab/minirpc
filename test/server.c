#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>
#include <minirpc/minirpc.h>
#include <minirpc/list.h>
#include "test_server.h"

#define DEBUG

/* XXX copied from libvdisk */
#define warn(s, args...) fprintf(stderr, s "\n", ## args)
#define ndebug(s, args...) do {} while (0)
#define die(s, args...) do { warn(s, ## args); exit(1); } while (0)
#ifdef DEBUG
#define debug(s, args...) warn(s, ## args)
#else
#define debug(s, args...) do {} while (0)
#endif

struct message_list_node {
	struct list_head lh;
	struct mrpc_message *msg;
	int num;
};

static struct list_head pending;
static pthread_mutex_t lock;
static pthread_cond_t cond;
static pthread_t callback_thread;

mrpc_status_t do_query(void *conn_data, struct mrpc_message *msg,
			TestRequest *in, TestReply *out)
{
	warn("Query, value %d", in->num);
	out->num=in->num;
	return MINIRPC_OK;
}

mrpc_status_t do_query_async_reply(void *conn_data, struct mrpc_message *msg,
			TestRequest *in, TestReply *out)
{
	struct message_list_node *node=malloc(sizeof(*node));
	warn("Query, value %d, pending", in->num);
	INIT_LIST_HEAD(&node->lh);
	node->msg=msg;
	node->num=in->num;
	pthread_mutex_lock(&lock);
	list_add(&node->lh, &pending);
	pthread_cond_signal(&cond);
	pthread_mutex_unlock(&lock);
	return MINIRPC_PENDING;
}

mrpc_status_t do_call(void *conn_data, struct mrpc_message *msg,
			TestRequest *req)
{
	warn("Received call(): %d", req->num);
	return MINIRPC_OK;
}

mrpc_status_t do_error(void *conn_data, struct mrpc_message *msg,
			TestReply *out)
{
	warn("Received error call");
	return 1;
}

mrpc_status_t do_ping(void *conn_data, struct mrpc_message *msg)
{
	warn("Received ping");
	return MINIRPC_OK;
}

mrpc_status_t do_invalidate_ops(void *conn_data, struct mrpc_message *msg)
{
	if (test_server_set_operations(conn_data, NULL))
		warn("Couldn't set operations");
	return MINIRPC_OK;
}

void do_notify(void *conn_data, struct mrpc_message *msg, TestNotify *req)
{
	warn("Received notify(): %d", req->num);
}

static const struct test_server_operations ops = {
	.query = do_query,
	.query_async_reply = do_query_async_reply,
	.call = do_call,
	.error = do_error,
	.invalidate_ops = do_invalidate_ops,
	.notify = do_notify,
	.ping = do_ping
};

void ops_disconnect(void *conn_data, enum mrpc_disc_reason reason)
{
	warn("Disconnect: %d", reason);
}

void *ops_accept(void *set_data, struct mrpc_connection *conn,
			struct sockaddr *from, socklen_t fromlen)
{
	warn("New connection");
	if (test_server_set_operations(conn, &ops))
		warn("Error setting operations struct");
	return conn;
}

static const struct mrpc_config config = {
	.protocol = &test_server
};

static const struct mrpc_set_operations set_ops = {
	.accept = ops_accept,
	.disconnect = ops_disconnect
};

static void *run_callbacks(void *ignored)
{
	struct message_list_node *node;
	struct TestReply reply;

	while (1) {
		pthread_mutex_lock(&lock);
		while (list_is_empty(&pending))
			pthread_cond_wait(&cond, &lock);
		node=list_first_entry(&pending, struct message_list_node, lh);
		list_del_init(&node->lh);
		pthread_mutex_unlock(&lock);

		warn("Sending async reply, value %d", node->num);
		reply.num=node->num;
		test_query_async_reply_send_async_reply(node->msg, &reply);
		free(node);
	}
}

int main(int argc, char **argv)
{
	struct mrpc_conn_set *set;
	const char *err;

	if (mrpc_conn_set_alloc(&config, &set_ops, NULL, &set))
		die("Couldn't allocate connection set");
	INIT_LIST_HEAD(&pending);
	pthread_mutex_init(&lock, NULL);
	pthread_cond_init(&cond, NULL);
	if (pthread_create(&callback_thread, NULL, run_callbacks, NULL))
		die("Couldn't start callback thread");
	if (!mrpc_listen(set, NULL, 58000, &err))
		die("%s", err);
	mrpc_dispatch_loop(set);
	return 0;
}
