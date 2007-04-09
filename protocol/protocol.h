#ifndef PROTOCOL_H
#define PROTOCOL_H

#include "ISRMessage.h"

typedef (void)(struct isr_connection *conn, void *conn_private,
			struct ISRMessage *msg) new_request_fn;

typedef (void)(struct isr_connection *conn, void *conn_data,
			struct ISRMessage *request, struct ISRMessage *reply,
			void *msg_data) reply_callback_fn;

#endif
