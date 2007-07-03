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

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <netdb.h>
#include <pthread.h>
#include <minirpc/minirpc.h>
#include "test_client.h"

/* XXX copied from libvdisk */
#define warn(s, args...) fprintf(stderr, s "\n", ## args)
#define die(s, args...) do { warn(s, ## args); exit(1); } while (0)

static pthread_t thread;

void query_sync(struct mrpc_connection *conn)
{
	struct TestRequest request;
	struct TestReply *reply;
	mrpc_status_t ret;

	warn("Sending sync query");
	request.num=12;
	ret=test_query(conn, &request, &reply);
	if (ret)
		die("query returned %d", ret);
	if (reply->num != 12)
		die("reply body contained %d", reply->num);
	free_TestRequest(&request, 0);
	free_TestReply(reply, 1);
	warn("...success");
}

void call_sync(struct mrpc_connection *conn)
{
	struct TestRequest request;
	mrpc_status_t ret;

	warn("Sending sync call");
	request.num=12;
	ret=test_call(conn, &request);
	if (ret)
		die("query returned %d", ret);
	free_TestRequest(&request, 0);
	warn("...success");
}

void error_sync(struct mrpc_connection *conn)
{
	struct TestReply *reply;
	mrpc_status_t ret;

	warn("Sending sync error");
	ret=test_error(conn, &reply);
	if (ret != 1)
		die("query returned %d", ret);
	warn("...success");
}

void query_callback(void *conn_private, void *msg_private,
			struct mrpc_message *msg, int status, TestReply *reply)
{
	int request=(int)msg_private;

	if (status)
		warn("Request %d returned error %d", request, status);
	else
		warn("Request %d returned reply %d", request, reply->num);
	free_TestReply(reply, 1);
}

void query_client_async(struct mrpc_connection *conn)
{
	struct TestRequest request;
	mrpc_status_t ret;
	int i;

	warn("Sending client-async queries");
	for (i=0; i<5; i++) {
		request.num=i;
		ret=test_query_async(conn, query_callback, (void*)i, &request);
		if (ret)
			die("query iteration %d returned %d", i, ret);
	}
	free_TestRequest(&request, 0);
	warn("...success");
}

void query_server_async(struct mrpc_connection *conn)
{
	struct TestRequest request;
	struct TestReply *reply;
	mrpc_status_t ret;

	warn("Sending server-async query");
	request.num=12;
	ret=test_query_async_reply(conn, &request, &reply);
	if (ret)
		die("query returned %d", ret);
	if (reply->num != 12)
		die("reply body contained %d", reply->num);
	free_TestRequest(&request, 0);
	free_TestReply(reply, 1);
	warn("...success");
}

void notify(struct mrpc_connection *conn)
{
	struct TestNotify notify;
	mrpc_status_t ret;

	warn("Sending notify");
	notify.num=12;
	ret=test_notify(conn, &notify);
	if (ret)
		die("notify returned %d", ret);
	free_TestNotify(&notify, 0);
	warn("...success");
}

void invalidate(struct mrpc_connection *conn)
{
	int ret;

	warn("Testing connectivity");
	ret=test_ping(conn);
	if (ret)
		die("Ping returned %d", ret);
	warn("Sending invalidate");
	ret=test_invalidate_ops(conn);
	if (ret)
		die("Invalidate returned %d", ret);
	warn("Testing invalidation");
	ret=test_ping(conn);
	if (ret != MINIRPC_PROCEDURE_UNAVAIL)
		die("Ping returned %d", ret);
}

void *runner(void *set)
{
	mrpc_dispatch_loop(set);
	return NULL;
}

void ops_disconnect(void *conn_data, enum mrpc_disc_reason reason)
{
	warn("Disconnect: %d", reason);
}

static const struct mrpc_config config = {
	.protocol = &test_client
};

static const struct mrpc_set_operations set_ops = {
	.disconnect = ops_disconnect
};

int main(int argc, char **argv)
{
	struct mrpc_conn_set *set;
	struct mrpc_connection *conn;
	int ret;
	apr_status_t stat;
	char errbuf[128];

	if (argc != 2)
		die("Usage: %s hostname", argv[0]);

	if (mrpc_conn_set_alloc(&config, &set_ops, NULL, &set))
		die("Couldn't allocate conn set");

	stat=mrpc_connect(set, argv[1], 58000, NULL, &conn);
	if (stat) {
		apr_strerror(stat, errbuf, sizeof(errbuf));
		die("%s", errbuf);
	}

	ret=pthread_create(&thread, NULL, runner, set);
	if (ret)
		die("Couldn't create runner thread: %s", strerror(errno));

	warn("Sending messages");
	query_sync(conn);
	query_client_async(conn);
	query_server_async(conn);
	call_sync(conn);
	error_sync(conn);
	notify(conn);
	invalidate(conn);
	return 0;
}
