/*
 * miniRPC - TCP RPC library with asynchronous operations and TLS support
 *
 * Copyright (C) 2007-2008 Carnegie Mellon University
 *
 * This software is distributed under the terms of the Eclipse Public License,
 * Version 1.0 which can be found in the file named LICENSE.  ANY USE,
 * REPRODUCTION OR DISTRIBUTION OF THIS SOFTWARE CONSTITUTES RECIPIENT'S
 * ACCEPTANCE OF THIS AGREEMENT
 */

/**
 * @file
 * @brief Common interface to the miniRPC library
 */

#ifndef MINIRPC_H
#define MINIRPC_H

#include <sys/types.h>
#include <sys/socket.h>

#ifdef DOXYGEN
/**
 * @brief An opaque handle to a protocol role definition
 */
struct mrpc_protocol {};

/**
 * @brief An opaque handle to a connection set
 */
struct mrpc_conn_set {};

/**
 * @brief An opaque handle to an open connection
 */
struct mrpc_connection {};

/**
 * @brief An opaque handle to a protocol message
 */
struct mrpc_message {};
#else  /* DOXYGEN */
struct mrpc_protocol;
struct mrpc_conn_set;
struct mrpc_connection;
struct mrpc_message;
#endif /* DOXYGEN */

/**
 * @brief Error codes used by the miniRPC protocol
 * @param	MINIRPC_OK
 *	Success
 * @param	MINIRPC_PENDING
 *	Special return code used by request handlers to indicate that they
 *	will complete the request asynchronously
 * @param	MINIRPC_ENCODING_ERR
 *	XXX
 * @param	MINIRPC_PROCEDURE_UNAVAIL
 *	XXX
 * @param	MINIRPC_INVALID_ARGUMENT
 *	XXX
 * @param	MINIRPC_INVALID_PROTOCOL
 *	XXX
 * @param	MINIRPC_NETWORK_FAILURE
 *	The network connection has failed
 *
 * - these are only used on the wire
 * - negative for minirpc errors, positive for your own errors
 */
enum mrpc_status_codes {
	MINIRPC_OK			=  0,
	MINIRPC_PENDING			= -1,
	MINIRPC_ENCODING_ERR		= -2,
	MINIRPC_PROCEDURE_UNAVAIL	= -3,
	MINIRPC_INVALID_ARGUMENT	= -4,
	MINIRPC_INVALID_PROTOCOL	= -5,
	MINIRPC_NETWORK_FAILURE		= -6,
};

/**
 * @brief Error code returned by protocol operations
 *
 * Protocol-specific request and reply functions return error codes of this
 * type.  Negative error codes are defined by miniRPC (see
 * ::mrpc_status_codes), while positive error codes are defined by the
 * protocol.
 */
typedef int mrpc_status_t;

/**
 * @brief Reasons that a connection could have been closed
 * @param	MRPC_DISC_USER
 *	The connection was closed with mrpc_conn_close()
 * @param	MRPC_DISC_CLOSED
 *	The connection was closed by the remote end
 * @param	MRPC_DISC_IOERR
 *	The connection was closed due to an I/O error
 * @param	MRPC_DISC_DESYNC
 *	XXX
 */
enum mrpc_disc_reason {
	MRPC_DISC_USER,
	MRPC_DISC_CLOSED,
	MRPC_DISC_IOERR,
	MRPC_DISC_DESYNC
};

/** @defgroup setup Setup
 * @{ */

/**
 * @brief Configuration parameters for a connection set
 *
 * Foo.
 */
struct mrpc_config {
	const struct mrpc_protocol *protocol;
	void *(*accept)(void *set_data, struct mrpc_connection *conn,
				struct sockaddr *from, socklen_t from_len);
	void (*disconnect)(void *conn_data, enum mrpc_disc_reason reason);
	void (*ioerr)(void *conn_data, char *message);
	unsigned msg_max_buf_len;
	unsigned listen_backlog;
};

/**
 * @brief Initialize the miniRPC library
 *
 * The application must call this function before any threads have been
 * started.
 */
int mrpc_init(void);

/**
 * @brief Create a connection set
 * @param[out]	new_set
 *	The resulting connection set, or NULL on error
 * @param	config
 *	The configuration to use.  A copy of the configuration struct is
 *	stored in the connection set, so the application need not keep it
 *	around.
 * @param	set_data
 *	An application-specific cookie for this connection set
 * @stdreturn
 *
 * - starts backgorund thread
 * - meaning of set_data=NULL
 */
int mrpc_conn_set_create(struct mrpc_conn_set **new_set,
			const struct mrpc_config *config, void *set_data);

/**
 * @brief Destroy a connection set
 * @param	set
 *	The set to destroy
 *
 * - Describe shutdown semantics
 * - Do not call from event handler
 */
void mrpc_conn_set_destroy(struct mrpc_conn_set *set);


/** @}
 * @defgroup conn Connection Handling
 * @{
 */

/**
 * @brief Make a new outgoing connection
 * @param[out]	new_conn
 *	The resulting connection handle, or NULL on error
 * @param	set
 *	The set to associate with this connection
 * @param	host
 *	The hostname or address of the remote listener
 * @param	port
 *	The TCP port number of the remote listener
 * @param	data
 *	An application-specific cookie for this connection
 * @stdreturn
 *
 * - list error codes?
 * - meaning of host/port NULL
 * - meaning of data==NULL
 */
int mrpc_connect(struct mrpc_connection **new_conn, struct mrpc_conn_set *set,
			const char *host, unsigned port, void *data);
/**
 * @brief Start listening for incoming connections
 * @param	set
 *	The set to associate with this listener
 * @param	listenaddr
 *	The hostname or address to listen on
 * @param[in,out] port
 *	The port number to listen on
 * @param[out]	bound
 *	The number of listeners created
 * @stdreturn
 *
 * - in/out semantics of port
 * - meaning of host NULL, port NULL
 * - is bound optional?
 * - explain bound
 * - explain return value if one listen operation failed, and put in
 * return field
 * - return semantics of outparams on error
 */
int mrpc_listen(struct mrpc_conn_set *set, const char *listenaddr,
			unsigned *port, int *bound);

/**
 * @brief Bind an existing file descriptor to a connection set
 * @param[out]	new_conn
 *	The resulting connection handle, or NULL on error
 * @param	set
 *	The set to associate with this connection
 * @param	fd
 *	The file descriptor to bind
 * @param	data
 *	An application-specific cookie for this connection
 * @stdreturn
 *
 * - must be an active socket, not a listener.  can we test for this in
 *   the program?
 * - ownership of socket passes to minirpc
 * - meaning of data==NULL
 */
int mrpc_bind_fd(struct mrpc_connection **new_conn, struct mrpc_conn_set *set,
			int fd, void *data);
/**
 * @brief Close an existing connection
 * @param	conn
 *	The connection to close
 *
 * - blocking semantics
 * - what the app should not do after the call
 * - return codes
 * - app should not expect normal close in disconnect event
 * - may be called from event handler
 */
int mrpc_conn_close(struct mrpc_connection *conn);

/**
 * @brief Close all listeners against a connection set
 * @param	set
 *	The connection set
 *
 * Close all listening sockets associated with the connection set.  The
 * application can use this e.g. while shutting down, to prevent additional
 * connections from being accepted while it is shutting down the existing ones.
 *
 * Note that there may be unprocessed accept events in the event queue, so
 * the application must not assume that no more accept notifications will
 * arrive.
 */
void mrpc_listen_close(struct mrpc_conn_set *set);


/** @}
 * @defgroup event Event Processing
 * @{
 */

/**
 * @brief Start a dispatcher thread for a connection set
 * @param	set
 *	The connection set
 * @stdreturn
 *
 * - Thread will persist until conn set is destroyed
 * - Simplest model
 */
int mrpc_start_dispatch_thread(struct mrpc_conn_set *set);

/**
 * @brief Notify miniRPC that the current thread will dispatch events for this
 *	connection set
 * @param	set
 *	The connection set
 *
 * - When this is required
 */
void mrpc_dispatcher_add(struct mrpc_conn_set *set);

/**
 * @brief Notify miniRPC that the current thread will no longer dispatch
 *	events for this connection set
 * @param	set
 *	The connection set
 */
void mrpc_dispatcher_remove(struct mrpc_conn_set *set);

/**
 * @brief Dispatch events from this thread until the connection set is
 *	destroyed
 * @param	set
 *	The connection set
 *
 * - Return values
 * - Must be within dispatcher_add
 * - may not be called recursively
 */
int mrpc_dispatch_loop(struct mrpc_conn_set *set);

/**
 * @brief Obtain a file descriptor which will be readable when there are
 *	events to process
 * @param	set
 *	The connection set
 * @param[out]	fd
 *	The file descriptor
 * @stdreturn
 *
 * - do not read or write the fd
 * - when to close the fd
 */
int mrpc_get_event_fd(struct mrpc_conn_set *set, int *fd);

/**
 * @brief Dispatch events from this thread and then return
 * @param	set
 *	The connection set
 * @param	max
 *	The maximum number of events to dispatch, or 0 for no limit
 * @sa mrpc_get_event_fd()
 *
 * Dispatch events until there are no more events to process or until
 * @c max events have been processed.
 *
 * - return semantics
 * - may not be called recursively
 */
int mrpc_dispatch(struct mrpc_conn_set *set, int max);

/**
 * @brief Disable event processing for a connection
 * @param	conn
 *	The connection
 * @stdreturn
 *
 * - refcounted: recursive calls acceptable
 * - semantics of when we return
 * - can be called from event handler?
 */
int mrpc_plug_conn(struct mrpc_connection *conn);

/**
 * @brief Re-enable event processing for a connection
 * @param	conn
 *	The connection
 * @stdreturn
 *
 * - refcounted
 */
int mrpc_unplug_conn(struct mrpc_connection *conn);

/**
 * @brief Prevent an event handler from plugging the event queue
 * @param	msg
 *	The message handle - XXX
 *
 * - stdreturn?
 * - explain plugging semantics - one message at a time unless you call this
 */
int mrpc_unplug_message(struct mrpc_message *msg);

/** @} */

#endif
