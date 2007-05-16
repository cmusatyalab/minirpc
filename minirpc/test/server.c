#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>
#include "minirpc.h"
#include "test_server.h"
#include "list.h"

#define BACKLOG 16
#define DEBUG
#define SRVPORT 58000

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
static pthread_t runner_thread;
static pthread_t callback_thread;

void setsockoptval(int fd, int level, int optname, int value)
{
	if (setsockopt(fd, level, optname, &value, sizeof(value)))
		warn("Couldn't setsockopt");
}

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

mrpc_status_t do_error(void *conn_data, struct mrpc_message *msg)
{
	return 1;
}

mrpc_status_t do_invalidate_ops(void *conn_data, struct mrpc_message *msg)
{
	if (test_server_set_operations(*(void**)conn_data, NULL))
		warn("Couldn't set operations");
	return MINIRPC_OK;
}

void do_notify(void *conn_data, struct mrpc_message *msg, TestNotify *req)
{
	warn("Received notify(): %d", req->num);
}

struct test_server_operations ops = {
	.query = do_query,
	.query_async_reply = do_query_async_reply,
	.call = do_call,
	.error = do_error,
	.invalidate_ops = do_invalidate_ops,
	.notify = do_notify
};

static void *runner(void *set)
{
	mrpc_dispatch_loop(set);
	return NULL;
}

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
		
		reply.num=node->num;
		test_query_async_reply_send_async_reply(node->msg, &reply);
		free(node);
	}
}

int main(int argc, char **argv)
{
	int listenfd;
	int fd;
	struct sockaddr_in addr;
	struct mrpc_conn_set *set;
	struct mrpc_connection *conn;
	void **ptrbuf;
	
	listenfd=socket(PF_INET, SOCK_STREAM, 0);
	if (listenfd == -1)
		die("Couldn't create socket");
	setsockoptval(listenfd, SOL_SOCKET, SO_REUSEADDR, 1);
	addr.sin_family=AF_INET;
	addr.sin_addr.s_addr=htonl(INADDR_ANY);
	addr.sin_port=htons(SRVPORT);
	if (bind(listenfd, (struct sockaddr*)&addr, sizeof(addr)))
		die("Couldn't bind socket to port %d", SRVPORT);
	if (listen(listenfd, BACKLOG))
		die("Couldn't listen on socket");
	
	if (mrpc_conn_set_alloc(&set, &test_server, 16, 16, 16, 140000))
		die("Couldn't allocate connection set");
	INIT_LIST_HEAD(&pending);
	pthread_mutex_init(&lock, NULL);
	pthread_cond_init(&cond, NULL);
	if (pthread_create(&runner_thread, NULL, runner, set))
		die("Couldn't start runner thread");
	if (pthread_create(&callback_thread, NULL, run_callbacks, NULL))
		die("Couldn't start callback thread");
	
	while (1) {
		fd=accept(listenfd, NULL, 0);
		if (fd < 0) {
			warn("Error accepting connection");
			continue;
		}
		setsockoptval(fd, SOL_SOCKET, SO_KEEPALIVE, 1);
		warn("Accepted connection");
		/* XXX shouldn't be necessary */
		ptrbuf=malloc(sizeof(*ptrbuf));
		if (mrpc_conn_add(&conn, set, fd, (void*)ptrbuf)) {
			warn("Error adding connection");
			close(fd);
			continue;
		}
		*ptrbuf=conn;
		warn("Added connection");
		if (test_server_set_operations(conn, &ops)) {
			warn("Error setting operations struct");
			continue;
		}
	}
	return 0;
}
