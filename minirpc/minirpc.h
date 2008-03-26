/*
 * miniRPC - TCP RPC library with asynchronous operations
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
#include <stdint.h>

#ifdef DOXYGEN
/**
 * @brief An opaque handle to a protocol role definition
 * @ingroup setup
 */
struct mrpc_protocol {};

/**
 * @brief An opaque handle to a connection set
 * @ingroup setup
 */
struct mrpc_conn_set {};

/**
 * @brief An opaque handle to a connection
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
 *	An error occurred during serialization/deserialization
 * @param	MINIRPC_PROCEDURE_UNAVAIL
 *	The requested procedure is not available at this time
 * @param	MINIRPC_INVALID_ARGUMENT
 *	An invalid argument was provided
 * @param	MINIRPC_INVALID_PROTOCOL
 *	The implied protocol role does not match the connection
 * @param	MINIRPC_NETWORK_FAILURE
 *	The action could not be completed due to a temporary or permanent
 *	network problem
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
 */
enum mrpc_disc_reason {
	MRPC_DISC_USER,
	MRPC_DISC_CLOSED,
	MRPC_DISC_IOERR
};

/**
 * @brief Statistics counters maintained by a connection
 * @ingroup conn
 * @param	MRPC_CONNCTR_SEND_BYTES
 *	The number of bytes sent on the connection
 * @param	MRPC_CONNCTR_RECV_BYTES
 *	The number of bytes received on the connection
 * @param	MRPC_CONNCTR_SEND_MSGS
 *	The number of miniRPC protocol messages sent on the connection
 * @param	MRPC_CONNCTR_RECV_MSGS
 *	The number of miniRPC protocol messages received on the connection
 * @param	MRPC_CONNCTR_NR
 *	Sentinel constant which evaluates to the number of counters supported.
 *	This does not correspond to an actual counter; passing it to
 *	mrpc_conn_get_counter() will result in EINVAL.
 */
enum mrpc_conn_counter {
	MRPC_CONNCTR_SEND_BYTES,
	MRPC_CONNCTR_RECV_BYTES,
	MRPC_CONNCTR_SEND_MSGS,
	MRPC_CONNCTR_RECV_MSGS,
	MRPC_CONNCTR_NR
};

/**
 * @addtogroup setup
 * @{
 */

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
 * @sa mrpc_set_accept_func
 *
 * This function is called when a new connection arrives on a listening socket
 * created with mrpc_listen().  At minimum, the function must set the
 * connection's operations struct using the protocol-specific set_operations
 * function; otherwise, no incoming messages for the connection will be
 * processed.
 *
 * @c from is no longer valid after the callback returns.
 */
typedef void *(mrpc_accept_fn)(void *set_data, struct mrpc_connection *conn,
			struct sockaddr *from, socklen_t from_len);

/**
 * @brief Event callback fired on connection close
 * @param	conn_data
 *	The cookie associated with the connection
 * @param	reason
 *	The reason the connection was closed
 * @sa mrpc_set_disconnect_func
 *
 * If supplied, this callback is fired when a connection is closed for any
 * reason, including when explicitly requested by the application (with
 * mrpc_conn_close()).  Once the callback returns, the application will not
 * receive further events on this connection and should make no further
 * miniRPC calls against it.
 */
typedef void (mrpc_disconnect_fn)(void *conn_data,
			enum mrpc_disc_reason reason);

/**
 * @brief Event callback fired on I/O error
 * @param	conn_data
 *	The cookie associated with the connection
 * @param	message
 *	A string describing the error
 * @sa mrpc_set_ioerr_func
 *
 * If supplied, this callback is fired whenever miniRPC encounters an I/O or
 * XDR error it wishes to report to the application.  @c message is in a
 * format suitable for logging.  @c message is no longer valid once the
 * callback returns.
 */
typedef void (mrpc_ioerr_fn)(void *conn_data, char *message);

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
 * @param	protocol
 *	Protocol role definition for connections in this connection set
 * @param	set_data
 *	An application-specific cookie for this connection set
 * @stdreturn
 *
 * Create a connection set, associate it with the specified protocol role
 * and application-specific pointer, start its background thread, and return
 * a handle to the connection set.  If @c set_data is NULL, set the
 * application-specific pointer to the connection set handle returned in
 * @c new_set.
 */
int mrpc_conn_set_create(struct mrpc_conn_set **new_set,
			const struct mrpc_protocol *protocol, void *set_data);

/**
 * @brief Destroy a connection set
 * @param	set
 *	The set to destroy
 *
 * Destroy the specified connection set.  This takes the following steps:
 *
 * -# Close all listening sockets
 * -# Close all active connections and wait for their disconnect functions
 * to be fired
 * -# Shut down all threads started with mrpc_start_dispatch_thread(), and
 * cause all other dispatch functions to return ENXIO
 * -# Wait for dispatching threads to call mrpc_dispatcher_remove()
 * -# Shut down the background thread associated with the connection set
 * -# Free the set's data structures
 *
 * The application must ensure that it does not start any dispatchers,
 * create any connections, or initiate RPCs on existing connections
 * during or after the execution of this function.  However, the
 * application should continue to dispatch events against the connection
 * set (if it is doing its own dispatching) until the dispatcher functions
 * return ENXIO.  Note that the accept function may still be called after
 * the call to mrpc_conn_set_destroy(); any connections arriving in this
 * window will be automatically closed along with the others.
 *
 * Once this function returns, the connection set is invalid and should
 * not be used in further API calls.
 *
 * This function must not be called from an event handler.
 */
void mrpc_conn_set_destroy(struct mrpc_conn_set *set);

/**
 * @brief Set the function to be called when a new connection arrives on a
 *	listening socket
 * @param	set
 *	The connection set to configure
 * @param	func
 *	The accept function
 * @stdreturn
 * @sa mrpc_accept_fn
 *
 * The application must set an accept function before calling mrpc_listen()
 * on @c set.
 */
int mrpc_set_accept_func(struct mrpc_conn_set *set, mrpc_accept_fn *func);

/**
 * @brief Set the function to be called when a connection is closed for any
 *	reason
 * @param	set
 *	The connection set to configure
 * @param	func
 *	The disconnect function, or NULL for none
 * @stdreturn
 * @sa mrpc_disconnect_fn
 *
 * By default, no disconnect function is provided.
 */
int mrpc_set_disconnect_func(struct mrpc_conn_set *set,
			mrpc_disconnect_fn *func);

/**
 * @brief Set the function to be called when a connection encounters an I/O
 *	error
 * @param	set
 *	The connection set to configure
 * @param	func
 *	The ioerr function, or NULL for none
 * @stdreturn
 * @sa mrpc_ioerr_fn
 *
 * By default, no ioerr function is provided.
 */
int mrpc_set_ioerr_func(struct mrpc_conn_set *set, mrpc_ioerr_fn *func);

/**
 * @brief Set the maximum length of a received message payload
 * @param	set
 *	The connection set to configure
 * @param	len
 *	The maximum payload length in bytes.  Must be greater than zero.
 * @stdreturn
 *
 * Set the maximum length, in bytes, of an XDR-encoded message received from
 * the remote system.  The default value is 16384.  Requests exceeding this
 * threshold will be rejected and ::MINIRPC_ENCODING_ERR will be returned to
 * the sender. Replies exceeding this threshold will be treated as though the
 * remote system returned ::MINIRPC_ENCODING_ERR.  Unidirectional messages
 * exceeding the threshold will be dropped.
 *
 * This is intended only as a DoS prevention measure, and should be
 * set to a value larger than any legitimate message possible in your
 * protocol.
 */
int mrpc_set_max_buf_len(struct mrpc_conn_set *set, unsigned len);

/**
 * @brief Set the number of accepted connections that can be waiting in the
 *	kernel
 * @param	set
 *	The connection set to configure
 * @param	backlog
 *	The length of the backlog queue.  Must be greater than zero.
 * @stdreturn
 *
 * Set the maximum number of connections that can be queued in the kernel
 * waiting for accept(); this corresponds to the @c backlog parameter
 * to the listen() system call.  The default value is 16.  The new setting
 * will only affect future calls to mrpc_listen(); existing listening sockets
 * will not be affected.
 */
int mrpc_set_listen_backlog(struct mrpc_conn_set *set, unsigned backlog);

/**
 * @brief Set the number of milliseconds to back off if accept() fails
 * @param	set
 *	The connection set to configure
 * @param	ms
 *	The number of milliseconds to wait.  Must be greater than zero.
 * @stdreturn
 *
 * If an error occurs while accepting a connection on a listening socket,
 * miniRPC will wait this many milliseconds before trying to accept any more
 * connections on that socket.  Existing connections are not affected.  Such
 * an error can occur, for example, if the process runs out of available file
 * descriptors.  The default value is 1000 ms.
 */
int mrpc_set_accept_backoff(struct mrpc_conn_set *set, unsigned ms);


/**
 * @}
 * @addtogroup conn
 * @{
 */

/**
 * @brief Create a new connection handle
 * @param[out]	new_conn
 *	The resulting connection handle, or NULL on error
 * @param	set
 *	The set to associate with this connection
 * @param	data
 *	An application-specific cookie for this connection
 * @stdreturn
 *
 * Allocate a new connection handle and associate it with the given connection
 * set and application-specific pointer.  This handle can then be used to make
 * an outgoing connection with mrpc_connect(), or can be bound to an existing
 * socket with mrpc_bind_fd().  Before the connection is completed using one of
 * these functions, the only valid operations on the connection handle are:
 * - Set the operations structure using the set_operations function for this
 * protocol role
 * - Plug it with mrpc_plug_conn()
 * - Destroy it with mrpc_conn_close()
 *
 * If @c data is NULL, the application-specific pointer is set to the
 * connection handle returned in @c new_conn.   */
int mrpc_conn_create(struct mrpc_connection **new_conn,
			struct mrpc_conn_set *set, void *data);

/**
 * @brief Make a new outgoing connection
 * @param	conn
 *	The connection handle to use
 * @param	host
 *	The hostname or address of the remote listener
 * @param	port
 *	The TCP port number of the remote listener
 * @stdreturn
 *
 * @bug list error codes
 *
 * Make a new outgoing connection to the specified remote host and port
 * and associate it with the given connection handle.  The specified
 * handle must not have been connected already.  If @c host is NULL, miniRPC
 * will connect to the loopback address.
 *
 * This function can only be called against connections with a client
 * protocol role.
 *
 * If the protocol allows the server to issue the first RPC on the connection,
 * the application should ensure that the correct operations structure is set
 * on the connection handle before calling this function.
 */
int mrpc_connect(struct mrpc_connection *conn, const char *host,
			unsigned port);

/**
 * @brief Start listening for incoming connections
 * @param	set
 *	The set to associate with this listener
 * @param	listenaddr
 *	The hostname or address to listen on
 * @param[in,out] port
 *	The port number to listen on
 * @return 0 if at least one listening socket is created, or the POSIX error
 *	code associated with the last error encountered
 *
 * Start listening for incoming connections on the given address and port
 * number, and fire the connection set's accept function whenever one arrives.
 * If more than one address meets the specified criteria, more than one
 * listening socket may be bound.  If @c listenaddr is NULL, miniRPC will
 * listen on any local interface.  If the value pointed to by @c port is
 * zero, miniRPC will bind to a random unused port, and will return the
 * chosen port number in @c port.
 *
 * This function will return EINVAL if @c set has a client protocol role
 * or if no accept function has been set with mrpc_set_accept_func().
 */
int mrpc_listen(struct mrpc_conn_set *set, const char *listenaddr,
			unsigned *port);

/**
 * @brief Bind an existing file descriptor to a connection handle
 * @param	conn
 *	The connection handle
 * @param	fd
 *	The file descriptor to bind
 * @stdreturn
 *
 * Associate the specified socket with an existing miniRPC connection handle.
 * The handle must not have been connected already.  The handle may have
 * either a client or server role.  The connection set's accept function
 * will @em not be called.  To avoid races, the application should ensure
 * that the operations structure is set on the connection handle, if
 * necessary, @em before calling this function.
 *
 * The specified file descriptor must be associated with a connected socket,
 * not a listening socket or another type of file.  After this call, the
 * socket will be managed by miniRPC; the application must not read, write,
 * or close it directly.
 */
int mrpc_bind_fd(struct mrpc_connection *conn, int fd);

/**
 * @brief Get statistics counter value for the specified connection
 * @param	conn
 *	The connection handle
 * @param	counter
 *	The particular counter being requested
 * @param[out]	result
 *	The current value of the counter
 * @stdreturn
 */
int mrpc_conn_get_counter(struct mrpc_connection *conn,
			enum mrpc_conn_counter counter, uint64_t *result);

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
 * function, will occur on this connection.  If two threads call
 * mrpc_conn_close() at once, the second call will return EALREADY, and this
 * guarantee will not apply to that call.
 *
 * The application should not make further API calls against the connection.
 * The application must not free any supporting data structures until the
 * connection set's disconnect function is called for the connection, since
 * further events may be pending.  In addition, the application should not
 * assume that the disconnect function's @c reason argument will be
 * ::MRPC_DISC_USER, since the connection may have been terminated for
 * another reason before mrpc_conn_close() was called.
 *
 * If the specified connection handle was allocated with mrpc_conn_create()
 * but has never been successfully connected with mrpc_connect() or
 * mrpc_bind_fd(), mrpc_conn_close() will immediately free the connection
 * handle.
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
 * Start a background thread to dispatch events.  This thread will persist
 * until the connection set is destroyed, at which point it will exit.  This
 * function can be called more than once; each call will create a new thread.
 * This is the simplest way to start a dispatcher for a connection set.
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
