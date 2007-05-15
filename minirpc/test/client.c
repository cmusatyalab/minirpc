#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <netdb.h>
#include "minirpc.h"
#include "test_client.h"

#define SRVPORTSTR "58000"

/* XXX copied from libvdisk */
#define warn(s, args...) fprintf(stderr, s "\n", ## args)
#define die(s, args...) do { warn(s, ## args); exit(1); } while (0)

void query_sync(struct mrpc_connection *conn)
{
	struct TestRequest request;
	struct TestReply *reply;
	mrpc_status_t ret;
	
	request.num=12;
	ret=test_query(conn, &request, &reply);
	if (ret)
		die("query returned %d", ret);
	if (reply->num != 12)
		die("reply body contained %d", reply->num);
	free_TestRequest(&request, 0);
	free_TestReply(reply, 1);
}

void query_callback(void *conn_private, void *msg_private, int status,
			TestReply *reply)
{
	int request=(int)msg_private;
	
	warn("Request %d returned reply %d", request, reply->num);
	free_TestReply(reply, 1);
}

void query_async(struct mrpc_connection *conn)
{
	struct TestRequest request;
	mrpc_status_t ret;
	int i;
	
	for (i=0; i<5; i++) {
		request.num=i;
		ret=test_query_async(conn, query_callback, (void*)i, &request);
		if (ret)
			die("query iteration %d returned %d", i, ret);
	}
	free_TestRequest(&request, 0);
}

int main(int argc, char **argv)
{
	struct mrpc_conn_set *set;
	struct mrpc_connection *conn;
	int fd;
	int ret;
	struct addrinfo *info;
	struct addrinfo hints={0};
	
	if (argc != 2)
		die("Usage: %s hostname", argv[0]);
	
	if (mrpc_conn_set_alloc(&set, &test_client, 16, 16, 16, 16000))
		die("Couldn't allocate conn set");
	
	hints.ai_family=PF_INET;
	hints.ai_socktype=SOCK_STREAM;
	ret=getaddrinfo(argv[1], SRVPORTSTR, &hints, &info);
	if (ret)
		die("Couldn't look up %s: %s", argv[1], gai_strerror(ret));
	fd=socket(info->ai_family, info->ai_socktype, 0);
	if (fd == -1)
		die("Couldn't create socket: %s", strerror(errno));
	ret=connect(fd, info->ai_addr, info->ai_addrlen);
	if (ret)
		die("Couldn't connect to host: %s", strerror(errno));
	freeaddrinfo(info);
	
	mrpc_conn_add(&conn, set, fd, NULL);
	warn("Sending messages");
	query_sync(conn);
	query_async(conn);
	pause();
	return 0;
}
