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
 * @ingroup setup
 */
struct mrpc_conn_set {};

/**
 * @brief An opaque handle to an open connection
 * @ingroup conn
 */
struct mrpc_connection {};

/**
 * @brief An opaque handle to a protocol message
 * @ingroup event
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

/**
 * @brief Configuration parameters for a connection set
 * @ingroup setup
 */
struct mrpc_config {
	/**
	 * @brief Protocol role definition for connections in the associated
	 *		connection set
	 */
	const struct mrpc_protocol *protocol;

	/**
	 * @brief Event callback fired on arrival of a new connection
	 * @param	set_data
	 *	The cookie associated with the connection set
	 * @param	conn
	 *	The handle to the newly-created connection
	 * @param	from
	 *	The address of the remote end of the connection
	 * @param	from_len
	 *	The length of the @c from structure
	 * @return The application-specific cookie to be associated with this
	 *		connection
	 *
	 * This method must be provided if #protocol specifies a server
	 * role, and must be NULL if it specifies a client.  At minimum,
	 * the method must set the connection's operations struct using
	 * the protocol-specific set_operations function; otherwise, no
	 * incoming messages for the connection will be processed.
	 *
	 * @c from is no longer valid after the callback returns.
	 */
	void *(*accept)(void *set_data, struct mrpc_connection *conn,
				struct sockaddr *from, socklen_t from_len);

	/**
	 * @brief Event callback fired on connection close
	 * @param	conn_data
	 *	The cookie associated with the connection
	 * @param	reason
	 *	The reason the connection was closed
	 *
	 * If non-NULL, this callback is fired when a connection is closed
	 * for any reason, including when explicitly requested by the
	 * application (with mrpc_conn_close()).  Once the callback returns,
	 * the application will not receive further events on this connection
	 * and should make no further miniRPC calls against it.
	 */
	void (*disconnect)(void *conn_data, enum mrpc_disc_reason reason);

	/**
	 * @brief Event callback fired on I/O error
	 * @param	conn_data
	 *	The cookie associated with the connection
	 * @param	message
	 *	A string describing the error
	 *
	 * If non-NULL, this callback is fired whenever miniRPC encounters
	 * an I/O error it wishes to report to the application.  @c message
	 * is in a format suitable for logging.  @c message is no longer valid
	 * once the callback returns.
	 */
	void (*ioerr)(void *conn_data, char *message);

	/**
	 * @brief Maximum length of a received message payload
	 *
	 * The maximum length, in bytes, of an XDR-encoded message received
	 * from the remote system.  If zero, a default will be used.
	 * Requests exceeding this threshold will be rejected and
	 * ::MRPC_ENCODING_ERROR will be returned to the sender.
	 * Other messages exceeding the threshold will be dropped.
	 *
	 * This is intended only as a DoS prevention measure, and should be
	 * set to a value larger than any legitimate message possible in your
	 * protocol.
	 *
	 * @bug We should wake up the waiter and give them a suitable error
	 * code.
	 * @bug We don't actually return an error to the sender
	 */
	unsigned msg_max_buf_len;

	/**
	 * @brief Number of accepted connections that can be waiting in the
	 *		kernel
	 *
	 * The maximum number of connections that can be queued in the kernel
	 * waiting for accept(); this corresponds to the @c backlog parameter
	 * to the listen() system call.  If zero, a default will be used.
	 */
	unsigned listen_backlog;
};


/**
 * @addtogroup setup
 * @{
 */

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


/**
 * @}
 * @addtogroup conn
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
 * @bug Check for set == NULL
 * @bug Allow new_conn == NULL if data == NULL
 *
 * - list error codes?
 *
 * Make a new outgoing connection to the specified remote host and port,
 * associate it with the given connection set and application-specific
 * pointer, and return a handle to the new connection.  If @c data is
 * NULL, the application-specific pointer is set to the connection handle
 * returned in @c new_conn.  If @c host is NULL, miniRPC will connect
 * to the loopback address.
 *
 * This function can only be called against connection sets with a client
 * protocol role.
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
 * @return 0 if at least one listening socket is created, or the POSIX error
 *	code associated with the last error encountered
 * @bug Handle set == NULL
 * @bug Eliminate bound
 *
 * Start listening for incoming connections on the given address and port
 * number, and fire the connection set's accept method whenever one arrives.
 * If more than one address meets the specified criteria, more than one
 * listening socket may be bound; the number of sockets created is returned
 * in @c bound if it is non-NULL.  If @c listenaddr is NULL, miniRPC will
 * listen on any local interface.  If the value pointed to by @c port is
 * zero, miniRPC will bind to a random unused port, and will return the
 * chosen port number in @c port.
 *
 * This function can only be called against connection sets with a server
 * protocol role.
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
 * @bug Should check that the fd is an active socket, not a listener
 * @bug Check for set == NULL
 *
 * Create a miniRPC connection for an existing socket, associate it with
 * the specified connection set and application-specific pointer, and return
 * a handle to the new connection.  After this call, the socket will be managed
 * by miniRPC; the application must not read, write, or close it directly.
 *
 * The specified file descriptor must be associated with a connected socket,
 * not a listening socket or another type of file.  The @c data parameter
 * may be NULL; in this case, the application-specific pointer is set to
 * the connection handle returned in @c new_conn.
 */
int mrpc_bind_fd(struct mrpc_connection **new_conn, struct mrpc_conn_set *set,
			int fd, void *data);
/**
 * @brief Close an existing connection
 * @param	conn
 *	The connection to close
 * @return 0 on success, or EALREADY if mrpc_conn_close() has already been
 *	called on this connection
 *
 * Close the connection specified by @c conn.  Protocol messages already
 * queued for transmission will be sent before the socket is closed.
 * Any pending synchronous RPCs will return ::MINIRPC_NETWORK_FAILURE,
 * and asynchronous RPCs will have their callbacks fired with a status
 * code of ::MINIRPC_NETWORK_FAILURE.  Other events queued for the
 * application will be dropped.
 *
 * Once this function returns, the application is guaranteed that no further
 * events, other than ::MINIRPC_NETWORK_FAILURE returns and the disconnect
 * method, will occur on this connection.  If two threads call
 * mrpc_conn_close() at once, the second call will return EALREADY, and this
 * guarantee will not apply to that call.
 *
 * The application must not free any supporting data structures until the
 * connection set's disconnect method is called for the connection, since
 * further events may be pending.  Simple synchronous clients with no
 * dispatcher may free supporting data structures as soon as this function
 * returns.  In either case, the application should not make further
 * API calls against the connection.  In addition, applications with
 * disconnect methods should not assume that the method's @c reason
 * argument will be ::MRPC_DISC_USER, since the connection may have been
 * terminated for another reason before mrpc_conn_close() was called.
 *
 * This function may be called from an event handler, including an event
 * handler for the connection being closed.
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


/**
 * @}
 * @addtogroup event
 * @{
 */

/**
 * @brief Start a dispatcher thread for a connection set
 * @param	set
 *	The connection set
 * @stdreturn
 *
 * This is the simplest way to start a dispatcher for the given connection
 * set.  miniRPC will start a background thread to dispatch events; this
 * thread will persist until the connection set is destroyed, at which point
 * it will exit.  This function can be called more than once; each call
 * will create a new thread.
 *
 * Unlike with mrpc_dispatch() and mrpc_dispatch_loop(), the caller does not
 * need to register the dispatcher thread with mrpc_dispatcher_add().  The
 * background thread handles this for you.
 */
int mrpc_start_dispatch_thread(struct mrpc_conn_set *set);

/**
 * @brief Notify miniRPC that the current thread will dispatch events for this
 *	connection set
 * @param	set
 *	The connection set
 *
 * Any thread which calls mrpc_dispatch() or mrpc_dispatch_loop() must call
 * mrpc_dispatcher_add() before it starts dispatching for the specified
 * connection set.
 */
void mrpc_dispatcher_add(struct mrpc_conn_set *set);

/**
 * @brief Notify miniRPC that the current thread will no longer dispatch
 *	events for this connection set
 * @param	set
 *	The connection set
 *
 * Any thread which calls mrpc_dispatch() or mrpc_dispatch_loop() must call
 * mrpc_dispatcher_remove() when it decides it will no longer dispatch for
 * the specified connection set.
 */
void mrpc_dispatcher_remove(struct mrpc_conn_set *set);

/**
 * @brief Dispatch events from this thread until the connection set is
 *	destroyed
 * @param	set
 *	The connection set
 * @return ENXIO if the connection set is being destroyed, or a POSIX
 *	error code on other error
 *
 * Start dispatching events for the given connection set, and do not return
 * until the connection set is being destroyed.  The thread must call
 * mrpc_dispatcher_add() before calling this function, and
 * mrpc_dispatcher_remove() afterward.  This function must not be called
 * from an event handler.
 */
int mrpc_dispatch_loop(struct mrpc_conn_set *set);

/**
 * @brief Dispatch events from this thread and then return
 * @param	set
 *	The connection set
 * @param	max
 *	The maximum number of events to dispatch, or 0 for no limit
 * @sa mrpc_get_event_fd()
 * @return ENXIO if the connection set is being destroyed, 0 if more events
 *	are pending, or EAGAIN if the event queue is empty
 *
 * Dispatch events until there are no more events to process or until
 * @c max events have been processed.  The calling thread must call
 * mrpc_dispatcher_add() before calling this function for the first time.
 *
 * If this function returns ENXIO, the connection set is being destroyed.
 * The application must stop calling this function, and must call
 * mrpc_dispatcher_remove() to indicate its intent to do so.
 *
 * This function must not be called from an event handler.
 */
int mrpc_dispatch(struct mrpc_conn_set *set, int max);

/**
 * @brief Obtain a file descriptor which will be readable when there are
 *	events to process
 * @param	set
 *	The connection set
 * @return The file descriptor
 * @bug We don't wake the event FD when the conn set is being destroyed
 *
 * Returns a file descriptor which can be passed to select()/poll() to
 * determine when the connection set has events to process.  This can be
 * used to embed processing of miniRPC events into an application-specific
 * event loop.  When the descriptor is readable, the connection set has
 * events to be dispatched; the application can call mrpc_dispatch() to
 * handle them.
 *
 * The application must not read, write, or close the provided file
 * descriptor.  Once mrpc_dispatch() returns ENXIO, indicating that the
 * connection set is being shut down, the application must stop polling
 * on the descriptor.
 */
int mrpc_get_event_fd(struct mrpc_conn_set *set);

/**
 * @brief Disable event processing for a connection
 * @param	conn
 *	The connection
 * @stdreturn
 * @bug We could block until we're guaranteed that no other events will
 * fire against the conn
 *
 * Prevent miniRPC from processing further events for the specified
 * connection until mrpc_unplug_conn() has been called.  This function
 * can be called from an event handler.
 *
 * The application may call this function more than once against the same
 * connection.  Event processing for the connection will not resume until
 * the application makes the corresponding number of calls to
 * mrpc_unplug_conn().
 *
 * Note that there is a window after the function is called in which new
 * events may still be fired against the connection.  This cannot occur,
 * however, if the function is called from an event handler for the
 * specified connection, unless that handler has called
 * mrpc_unplug_message().
 */
int mrpc_plug_conn(struct mrpc_connection *conn);

/**
 * @brief Re-enable event processing for a connection
 * @param	conn
 *	The connection
 * @stdreturn
 *
 * Allow miniRPC to process events against a connection which has been
 * plugged with mrpc_plug_conn().  If mrpc_plug_conn() has been called
 * more than once, the connection will not be unplugged until
 * mrpc_unplug_conn() has been called a corresponding number of times.
 */
int mrpc_unplug_conn(struct mrpc_connection *conn);

/**
 * @brief Prevent an event handler from plugging the event queue
 * @param	msg
 *	The message handle passed to the event handler
 * @stdreturn
 *
 * By default, only one event handler for a given connection can run at a
 * time, even if there are additional events queued for the connection.  This
 * allows the application to avoid handling concurrency issues within a
 * connection.  However, in certain cases, the application may wish to allow
 * events on a connection to be processed in parallel, and to handle the
 * resulting concurrency issues itself.
 *
 * This function indicates to miniRPC that the specified protocol message
 * should no longer block the handling of additional events on its associated
 * connection.  The argument is the opaque message handle passed to the
 * event handler function.  Note that this call is effective @em only
 * for the event associated with the specified message; it will have no effect
 * on any other event.
 */
int mrpc_unplug_message(struct mrpc_message *msg);

/**
 * @}
 */

#endif
