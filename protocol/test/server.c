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
#include "protocol.h"

#define BACKLOG 16
#define DEBUG
#define SRVPORT 58000

/* XXX copied from libvdisk */
#define warn(s, args...) fprintf(stderr, s ": %s\n", ## args, strerror(errno))
#define ndebug(s, args...) do {} while (0)
#define die(s, args...) do { warn(s, ## args); exit(1); } while (0)
#ifdef DEBUG
#define debug(s, args...) warn(s, ## args)
#else
#define debug(s, args...) do {} while (0)
#endif

void setsockoptval(int fd, int level, int optname, int value)
{
	if (setsockopt(fd, level, optname, &value, sizeof(value)))
		warn("Couldn't setsockopt");
}

static struct ISRMessage *alloc_listreply(void)
{
	struct ISRMessage *ret;
	struct ParcelInfo *cur;
	char *names[]={"one", "two", "three", "four", "five", 0};
	char **name;
	time_t curtime;
	
	ret=isr_alloc_message();
	if (ret == NULL)
		return ret;
	
	ret->body.present=MessageBody_PR_listreply;
	for (name=names; *name != 0; name++) {
		cur=malloc(sizeof(*cur));
		if (cur == NULL)
			die("Malloc failure");
		OCTET_STRING_fromString(&cur->name, *name);
		cur->current.version=12;
		curtime=time(NULL);
		asn_time2GT(&cur->current.checkin, gmtime(&curtime), 1);
		cur->current.chunks=65000;
		asn_sequence_add(&ret->body.listreply.list, cur);
	}
	return ret;
}

static void request(struct isr_connection *conn, void *data,
			struct ISRMessage *msg)
{
	struct ISRMessage *reply;
	
	reply=malloc(sizeof(*reply));
	if (reply == NULL)
		die("malloc failed");
	switch(msg->body.present) {
	case MessageBody_PR_list:
		reply=alloc_listreply();
		break;
	default:
		reply->body.present=MessageBody_PR_status;
		reply->body.status=Status_message_unknown;
		break;
	}
	if (isr_send_reply(conn, msg, reply))
		warn("Couldn't send reply");
}

int main(int argc, char **argv)
{
	int listenfd;
	int fd;
	struct sockaddr_in addr;
	struct isr_conn_set *set;
	struct isr_connection *conn;
	
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
	
	if (isr_conn_set_alloc(&set, 1, request, 16, 16, 16, 140000))
		die("Couldn't allocate connection set");
	
	while (1) {
		fd=accept(listenfd, NULL, 0);
		if (fd < 0) {
			warn("Error accepting connection");
			continue;
		}
		setsockoptval(fd, SOL_SOCKET, SO_KEEPALIVE, 1);
		if (isr_conn_add(&conn, set, fd, NULL)) {
			warn("Error adding connection");
			close(fd);
			continue;
		}
	}
	return 0;
}
