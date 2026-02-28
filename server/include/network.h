/**
 * @file network.h
 * @brief Provide the same connection interfaces (TCP) on different platforms
 * @version 0.1
 * @date 2025-10-19
 *
 */
#ifndef NETWORK_H
#define NETWORK_H

#include <stdint.h>
#include <stddef.h>

#ifdef _WIN32
#include <winsock2.h>
typedef SOCKET socket_t;
#define INVALID_SOCKET_T INVALID_SOCKET
#else
typedef int socket_t;
#define INVALID_SOCKET_T -1
#endif

/**
 * @brief Initializes the networking library.
 *
 * On Windows, this calls WSAStartup.
 * On POSIX systems, this does nothing.
 * Must be called once before any other network functions.
 *
 * @return int 0 on success, -1 on failure.
 */
int net_init(void);

/**
 * @brief Cleans up the networking library.
 *
 * On Windows, this calls WSACleanup.
 * On POSIX systems, this does nothing.
 * Should be called once when the application is exiting.
 */
void net_cleanup(void);

/**
 * @brief Defines the address family for socket creation.
 */
typedef enum
{
    NET_AF_UNSPEC, // Let the system decide (e.g., IPv6 if available, otherwise IPv4)
    NET_AF_IPV4,   // Force IPv4
    NET_AF_IPV6    // Force IPv6
} net_addr_family_t;

/**
 * @brief Creates a listening socket bound to a specific address and port.
 *
 * The socket is bound to the specified address and port, and set to listen with the given backlog.
 *
 * @param family The address family to use (IPv4, IPv6, or Unspecified).
 * @param bind_address The IP address to bind to (NULL for any address).
 * @param port The port number to listen on (e.g., 21 for FTP).
 * @param backlog The maximum number of pending connections to queue.
 * @retval socket_t The listening socket descriptor on success, or INVALID_SOCKET_T on error.
 */
socket_t net_create_listening_socket(net_addr_family_t family, const char *bind_address, uint16_t port, int backlog);

/**
 * @brief Creates a listening socket within a specified port range.
 *
 * This is useful for FTP passive mode, where a dynamic port is needed.
 * The function tries each port in the range until it finds one that works.
 *
 * @param family The address family to use (IPv4, IPv6, or Unspecified).
 * @param bind_address The IP address to bind to (NULL for any address).
 * @param port_min The minimum port number to try.
 * @param port_max The maximum port number to try.
 * @param backlog The maximum number of pending connections to queue.
 * @param assigned_port A pointer to store the actual port that was assigned (can be NULL).
 * @return The listening socket descriptor on success, or INVALID_SOCKET_T on error.
 */
socket_t net_create_listening_socket_range(net_addr_family_t family, const char *bind_address, uint16_t port_min,
                                           uint16_t port_max, int backlog, uint16_t *assigned_port);

/**
 * @brief Accepts an incoming client connection on a listening socket.
 *
 * This function will block until a client connects.
 *
 * @param listening_socket The server's main listening socket.
 * @param client_ip_buffer A buffer to store the client's IP address string (can be NULL).
 * @param buffer_size The size of the client_ip_buffer.
 * @param client_port A pointer to store the client's port number (can be NULL).
 * @return The new socket descriptor for communicating with the client, or INVALID_SOCKET_T on error.
 */
socket_t net_accept(socket_t listening_socket, char *client_ip_buffer, size_t buffer_size, uint16_t *client_port);

/**
 * @brief Connects to a remote host.
 *
 * This is used for active mode FTP, where the server connects to the client.
 *
 * @param host The hostname or IP address of the server to connect to.
 * @param port The port number to connect to.
 * @return The socket descriptor for the new connection, or INVALID_SOCKET_T on error.
 */
socket_t net_connect(const char *host, uint16_t port);

/**
 * @brief Gets the local address and port of a socket.
 *
 * This is useful for passive mode FTP, to find out which port the OS assigned
 * to a listening socket.
 *
 * @param sock The socket descriptor.
 * @param ip_buffer A buffer to store the local IP address string (can be NULL).
 * @param buffer_size The size of the ip_buffer.
 * @param port A pointer to store the local port number (can be NULL).
 * @return 0 on success, -1 on error.
 */
int net_get_socket_info(socket_t sock, char *ip_buffer, size_t buffer_size, uint16_t *port);

/**
 * @brief Receives data from a connected socket.
 *
 * @param connected_socket The socket to receive data from.
 * @param buffer The buffer to store the received data.
 * @param buffer_size The maximum number of bytes to receive.
 * @return The number of bytes received, 0 if the connection was closed by the peer, or -1 on error.
 */
int net_receive(socket_t connected_socket, void *buffer, size_t buffer_size);

/**
 * @brief Reliably receives a specific amount of data from a connected socket.
 *
 * This function loops until all requested data is received or an error occurs.
 *
 * @param connected_socket The socket to receive data from.
 * @param buffer The buffer to store the received data.
 * @param length The total number of bytes to receive.
 * @return 0 on success, -1 on failure (e.g., connection closed before all data arrived).
 */
int net_receive_all(socket_t connected_socket, void *buffer, size_t length);

/**
 * @brief Receives a line of text from a socket (until CRLF).
 *
 * This is useful for FTP control connections, which use text-based commands.
 * The function reads until it encounters \r\n (CRLF) or the buffer is full.
 *
 * @param connected_socket The socket to receive data from.
 * @param buffer The buffer to store the received line (includes CRLF).
 * @param buffer_size The maximum size of the buffer.
 * @param timeout_ms Timeout in milliseconds. -1 means wait indefinitely.
 * @return The number of bytes received (including CRLF), 0 if connection closed, -1 on error or timeout.
 */
int net_receive_line(socket_t connected_socket, char *buffer, size_t buffer_size, int timeout_ms);

/**
 * @brief Sends data to a connected socket.
 *
 * @param connected_socket The socket to send data to.
 * @param data The data to send.
 * @param length The number of bytes to send.
 * @return The number of bytes sent, or -1 on error.
 */
int net_send(socket_t connected_socket, const void *data, size_t length);

/**
 * @brief Reliably sends a specific amount of data to a connected socket.
 *
 * This function loops until all data is sent or an error occurs.
 *
 * @param connected_socket The socket to send data to.
 * @param data The data to send.
 * @param length The total number of bytes to send.
 * @return 0 on success, -1 on failure (e.g., connection closed or error).
 */
int net_send_all(socket_t connected_socket, const void *data, size_t length);

/**
 * @brief Closes a socket.
 *
 * @param sock The socket descriptor to close.
 */
void net_close_socket(socket_t sock);

/**
 * @brief Shuts down the send side of a socket.
 *
 * After calling this, no more data can be sent on the socket.
 *
 * @param sock The socket descriptor.
 * @return 0 on success, -1 on error.
 */
int net_shutdown_send(socket_t sock);

/**
 * @brief Shuts down the receive side of a socket.
 *
 * After calling this, no more data can be received on the socket.
 *
 * @param sock The socket descriptor.
 * @return 0 on success, -1 on error.
 */
int net_shutdown_recv(socket_t sock);

/**
 * @brief Shuts down both send and receive sides of a socket.
 *
 * This immediately interrupts any blocking I/O operations on the socket.
 *
 * @param sock The socket descriptor.
 * @return 0 on success, -1 on error.
 */
int net_shutdown_both(socket_t sock);

//  Additional utility functions

/**
 * @brief Sets a socket to non-blocking or blocking mode.
 *
 * @param sock The socket descriptor.
 * @param enable 1 to enable non-blocking mode, 0 to disable.
 * @return 0 on success, -1 on error.
 */
int net_set_nonblocking(socket_t sock, int enable);

/**
 * @brief Sets the receive timeout for a socket.
 *
 * @param sock The socket descriptor.
 * @param timeout_ms Timeout in milliseconds. 0 means no timeout (blocking).
 * @return 0 on success, -1 on error.
 */
int net_set_recv_timeout(socket_t sock, int timeout_ms);

/**
 * @brief Sets the send timeout for a socket.
 *
 * @param sock The socket descriptor.
 * @param timeout_ms Timeout in milliseconds. 0 means no timeout (blocking).
 * @return 0 on success, -1 on error.
 */
int net_set_send_timeout(socket_t sock, int timeout_ms);

/**
 * @brief Sets or clears the TCP_NODELAY option.
 *
 * When enabled, this disables Nagle's algorithm, which can reduce latency
 * for small packets at the cost of potentially increased network traffic.
 *
 * @param sock The socket descriptor.
 * @param enable 1 to enable TCP_NODELAY, 0 to disable.
 * @return 0 on success, -1 on error.
 */
int net_set_tcp_nodelay(socket_t sock, int enable);

/**
 * @brief Sets or clears the SO_KEEPALIVE option.
 *
 * When enabled, the system will send keepalive probes to detect dead connections.
 *
 * @param sock The socket descriptor.
 * @param enable 1 to enable keepalive, 0 to disable.
 * @return 0 on success, -1 on error.
 */
int net_set_keepalive(socket_t sock, int enable);

/**
 * @brief Waits for a socket to become readable.
 *
 * @param sock The socket descriptor.
 * @param timeout_ms Timeout in milliseconds. -1 means wait indefinitely.
 * @return 1 if readable, 0 if timeout, -1 on error.
 */
int net_wait_readable(socket_t sock, int timeout_ms);

/**
 * @brief Waits for a socket to become writable.
 *
 * @param sock The socket descriptor.
 * @param timeout_ms Timeout in milliseconds. -1 means wait indefinitely.
 * @return 1 if writable, 0 if timeout, -1 on error.
 */
int net_wait_writable(socket_t sock, int timeout_ms);

/**
 * @brief Checks if there is urgent (out-of-band) data available on the socket.
 *
 * This is used to detect TCP urgent data, which is how ABOR commands are
 * typically sent according to RFC 959.
 *
 * @param sock The socket descriptor.
 * @return 1 if urgent data is available, 0 if not, -1 on error.
 */
int net_has_urgent_data(socket_t sock);

/**
 * @brief Receives urgent (out-of-band) data from a socket.
 *
 * @param sock The socket descriptor.
 * @param buffer The buffer to store the urgent data.
 * @param buffer_size The maximum number of bytes to receive.
 * @return The number of bytes received, 0 if no urgent data, -1 on error.
 */
int net_receive_urgent(socket_t sock, void *buffer, size_t buffer_size);

/**
 * @brief Sets the SO_OOBINLINE option on a socket.
 *
 * When enabled, urgent data is placed in the normal data stream instead of
 * being retrieved separately with MSG_OOB.
 *
 * @param sock The socket descriptor.
 * @param enable 1 to enable OOB inline, 0 to disable.
 * @return 0 on success, -1 on error.
 */
int net_set_oob_inline(socket_t sock, int enable);

/**
 * @brief Gets the last socket error code.
 *
 * @return The error code from the last socket operation.
 */
int net_get_last_error(void);

/**
 * @brief Gets a human-readable error message for a socket error code.
 *
 * @param error_code The error code (from net_get_last_error()).
 * @return A string describing the error. The string is valid until the next call.
 */
const char *net_get_error_string(int error_code);

/**
 * @brief Checks if an error code indicates a "would block" condition.
 *
 * On Windows, this checks for WSAEWOULDBLOCK.
 * On POSIX systems, this checks for EAGAIN or EWOULDBLOCK.
 *
 * @param error_code The error code (from net_get_last_error()).
 * @return 1 if the error indicates "would block", 0 otherwise.
 */
int net_is_would_block(int error_code);

#endif