#ifndef PROTOCOL_H
#define PROTOCOL_H

#include "ISRMessage.h"

struct isr_conn_set;
struct isr_connection;

typedef void (request_fn)(struct isr_connection *conn, void *conn_data,
			struct ISRMessage *msg);

typedef void (reply_callback_fn)(struct isr_connection *conn, void *conn_data,
			struct ISRMessage *request, struct ISRMessage *reply,
			void *msg_data);

#endif
