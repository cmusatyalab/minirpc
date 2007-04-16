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
#include "protocol.h"

#define SRVPORTSTR "58000"

/* XXX copied from libvdisk */
#define warn(s, args...) fprintf(stderr, s "\n", ## args)
#define die(s, args...) do { warn(s, ## args); exit(1); } while (0)

static void request(struct isr_connection *conn, void *conn_data,
			struct ISRMessage *msg) {
	die("Received request from server");
}

static void getchunk_callback(struct isr_connection *conn, void *conn_data,
			struct ISRMessage *request, struct ISRMessage *reply,
			void *msg_data)
{
	if (reply->body.present == MessageBody_PR_status) {
		warn("Received error status from server");
		return;
	}
	warn("==== Received reply to request for chunk %ld",
				request->body.chunkrequest.by.chunk.cid);
}

void list_parcels(struct isr_connection *conn)
{
	struct ISRMessage *request=isr_alloc_message();
	struct ISRMessage *reply;
	int i;
	
	if (request == NULL)
		die("Couldn't allocate message");
	request->body.present=MessageBody_PR_list;
	isr_send_request(conn, request, &reply);
	warn("Received response");
	if (reply == NULL)
		die("Received invalid reply");
	for (i=0; i<reply->body.listreply.list.count; i++) {
		warn("== %.*s", reply->body.listreply.list.array[i]->name.size,
				reply->body.listreply.list.array[i]->name.buf);
	}
}

void request_chunks(struct isr_connection *conn)
{
	struct ISRMessage *request;
	int i;
	
	for (i=0; i<5; i++) {
		request=isr_alloc_message();
		if (request == NULL)
			die("Couldn't allocate message");
		
		request->body.present=MessageBody_PR_chunkrequest;
		request->body.chunkrequest.by.present=ChunkLookupKey_PR_chunk;
		request->body.chunkrequest.by.chunk.cid=i;
		request->body.chunkrequest.by.chunk.plane=ChunkPlane_disk;
		request->body.chunkrequest.want.buf=malloc(1);
		memset(request->body.chunkrequest.want.buf, 0, 1);
		request->body.chunkrequest.want.size=1;
		request->body.chunkrequest.want.bits_unused=8;
		isr_send_request_async(conn, request, getchunk_callback, NULL);
	}
}

int main(int argc, char **argv)
{
	struct isr_conn_set *set;
	struct isr_connection *conn;
	int fd;
	int ret;
	struct addrinfo *info;
	struct addrinfo hints={0};
	
	if (argc != 2)
		die("Usage: %s hostname", argv[0]);
	
	if (isr_conn_set_alloc(&set, 0, request, 16, 16, 16, 140000))
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
	
	isr_conn_add(&conn, set, fd, NULL);
	warn("Sending message");
	list_parcels(conn);
	request_chunks(conn);
	pause();
	return 0;
}
