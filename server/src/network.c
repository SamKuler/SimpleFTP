/**
 * @file network.c
 * @brief Implementation of the cross-platform networking library.
 *
 * @version 0.1
 * @date 2025-10-19
 */
#define _POSIX_C_SOURCE 200112L
#include "network.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <errno.h>
#endif

/**
 * @brief Flag indicating whether the module has been initialized.
 */
static int g_initialized = 0;

int net_init(void)
{
    if (g_initialized)
        return 0;
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        return -1;
    }
#endif
    g_initialized = 1;
    return 0;
}

void net_cleanup(void)
{
    if (!g_initialized)
        return;
#ifdef _WIN32
    WSACleanup();
#endif
    g_initialized = 0;
}

socket_t net_create_listening_socket(net_addr_family_t family, const char *bind_address, uint16_t port, int backlog)
{
    struct addrinfo hints, *res, *p;
    socket_t listening_socket = INVALID_SOCKET_T;
    int opt = 1;
    char port_str[6]; // max "65535" + null terminator

    snprintf(port_str, sizeof(port_str), "%u", port);

    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; // For wildcard IP address

    // Determine the ip family
    switch (family)
    {
    case NET_AF_IPV4:
        hints.ai_family = AF_INET;
        break;
    case NET_AF_IPV6:
        hints.ai_family = AF_INET6;
        break;
    case NET_AF_UNSPEC:
    default:
        hints.ai_family = AF_UNSPEC;
        break;
    }

    if (getaddrinfo(bind_address, port_str, &hints, &res) != 0)
    {
        return INVALID_SOCKET_T;
    }

    // Access all available addresses
    for (p = res; p != NULL; p = p->ai_next)
    {
        listening_socket = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (listening_socket == INVALID_SOCKET_T)
        {
            continue;
        }

        // Allow reusing the address to avoid "Address already in use" errors on restart
        if (setsockopt(listening_socket, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt)) < 0)
        {
            net_close_socket(listening_socket);
            listening_socket = INVALID_SOCKET_T;
            continue;
        }

        // For IPv6, ensure it's not an IPv4-mapped address (IPv6 only)
        if (p->ai_family == AF_INET6)
        {
            int ipv6_only = 1;
            if (setsockopt(listening_socket, IPPROTO_IPV6, IPV6_V6ONLY, (const char *)&ipv6_only, sizeof(ipv6_only)) < 0)
            {
                // This might fail on older systems, but we can often ignore the error
                // Here keep the if statement for unified structure
            }
        }

        if (bind(listening_socket, p->ai_addr, (int)p->ai_addrlen) < 0)
        {
            net_close_socket(listening_socket);
            listening_socket = INVALID_SOCKET_T;
            continue;
        }

        // If we got here, we successfully bound the socket. No need to find further.
        break;
    }

    freeaddrinfo(res);

    if (listening_socket == INVALID_SOCKET_T)
    {
        return INVALID_SOCKET_T; // Failed to bind to any address
    }

    if (listen(listening_socket, backlog) < 0)
    {
        net_close_socket(listening_socket);
        return INVALID_SOCKET_T;
    }

    return listening_socket;
}

socket_t net_create_listening_socket_range(net_addr_family_t family, const char *bind_address, uint16_t port_min,
                                           uint16_t port_max, int backlog, uint16_t *assigned_port)
{
    socket_t listening_socket = INVALID_SOCKET_T;
    uint16_t port;
    uint16_t range_size;
    uint16_t start_offset;

    if (port_min > port_max)
    {
        return INVALID_SOCKET_T;
    }

    range_size = port_max - port_min + 1;

    // Randomize starting port! To distribute load and avoid conflicts
    // Use time and thread-specific values for better randomization
    start_offset = (uint16_t)((time(NULL) + (uintptr_t)&port) % range_size);

    // Try each port in the range, starting from random offset
    for (uint16_t i = 0; i < range_size; i++)
    {
        port = port_min + ((start_offset + i) % range_size);

        listening_socket = net_create_listening_socket(family, bind_address, port, backlog);
        if (listening_socket != INVALID_SOCKET_T)
        {
            // Successfully bound to this port
            if (assigned_port)
            {
                *assigned_port = port;
            }
            return listening_socket;
        }
    }

    // Failed to bind to any port in the range
    if (assigned_port)
    {
        *assigned_port = 0;
    }
    return INVALID_SOCKET_T;
}

socket_t net_accept(socket_t listening_socket, char *client_ip_buffer, size_t buffer_size, uint16_t *client_port)
{
    struct sockaddr_storage client_addr;
    socklen_t addr_len = sizeof(client_addr);
    socket_t client_socket;

    client_socket = accept(listening_socket, (struct sockaddr *)&client_addr, &addr_len);

    if (client_socket == INVALID_SOCKET_T)
    {
        if (client_port)
            *client_port = 0;
        return INVALID_SOCKET_T;
    }

    if (client_addr.ss_family == AF_INET) // IPv4
    {
        struct sockaddr_in *s = (struct sockaddr_in *)&client_addr;
        if (client_ip_buffer && buffer_size > 0)
        {
            inet_ntop(AF_INET, &s->sin_addr, client_ip_buffer, buffer_size);
        }
        if (client_port)
        {
            *client_port = ntohs(s->sin_port);
        }
    }
    else // AF_INET6 IPv6
    {
        struct sockaddr_in6 *s = (struct sockaddr_in6 *)&client_addr;
        if (client_ip_buffer && buffer_size > 0)
        {
            inet_ntop(AF_INET6, &s->sin6_addr, client_ip_buffer, buffer_size);
        }
        if (client_port)
        {
            *client_port = ntohs(s->sin6_port);
        }
    }

    return client_socket;
}

socket_t net_connect(const char *host, uint16_t port)
{
    struct addrinfo hints, *res, *p;
    socket_t sock = INVALID_SOCKET_T;
    char port_str[6];

    snprintf(port_str, sizeof(port_str), "%u", port);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, port_str, &hints, &res) != 0)
    {
        return INVALID_SOCKET_T;
    }

    for (p = res; p != NULL; p = p->ai_next)
    {
        sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sock == INVALID_SOCKET_T)
        {
            continue;
        }

        if (connect(sock, p->ai_addr, (int)p->ai_addrlen) < 0)
        {
            net_close_socket(sock);
            sock = INVALID_SOCKET_T;
            continue;
        }

        break; // Successfully connected
    }

    freeaddrinfo(res);

    return sock;
}

int net_get_socket_info(socket_t sock, char *ip_buffer, size_t buffer_size, uint16_t *port)
{
    struct sockaddr_storage addr;
    socklen_t addr_len = sizeof(addr);

    if (getsockname(sock, (struct sockaddr *)&addr, &addr_len) < 0)
    {
        return -1;
    }

    if (addr.ss_family == AF_INET)
    {
        struct sockaddr_in *s = (struct sockaddr_in *)&addr;
        if (ip_buffer && buffer_size > 0)
        {
            inet_ntop(AF_INET, &s->sin_addr, ip_buffer, buffer_size);
        }
        if (port)
        {
            *port = ntohs(s->sin_port);
        }
    }
    else // AF_INET6
    {
        struct sockaddr_in6 *s = (struct sockaddr_in6 *)&addr;
        if (ip_buffer && buffer_size > 0)
        {
            inet_ntop(AF_INET6, &s->sin6_addr, ip_buffer, buffer_size);
        }
        if (port)
        {
            *port = ntohs(s->sin6_port);
        }
    }

    return 0;
}

int net_receive(socket_t connected_socket, void *buffer, size_t buffer_size)
{
#ifdef _WIN32
    return recv(connected_socket, (char *)buffer, (int)buffer_size, 0);
#else
    return (int)recv(connected_socket, buffer, buffer_size, 0);
#endif
}

int net_receive_all(socket_t connected_socket, void *buffer, size_t length)
{
    char *ptr = (char *)buffer;
    size_t total_received = 0;

    while (total_received < length)
    {
        int bytes_received = net_receive(connected_socket, ptr + total_received, length - total_received);
        if (bytes_received <= 0)
        {
            // Error or connection closed before receiving all data
            return -1;
        }
        total_received += bytes_received;
    }

    return 0; // Success
}

int net_receive_line(socket_t sock, char *buffer, size_t buffer_size, int timeout_ms)
{
    size_t pos = 0;
    char c;
    int result;

    if (!buffer || buffer_size < 3) // Need at least space for "X\r\n"
    {
        return -1;
    }

    while (pos < buffer_size - 1)
    {
        // Wait for data with timeout
        if (timeout_ms >= 0)
        {
            result = net_wait_readable(sock, timeout_ms);
            if (result <= 0)
            {
                return result; // Timeout or error
            }
        }

        // Receive one character
        result = net_receive(sock, &c, 1);
        if (result <= 0)
        {
            return result; // Connection closed or error
        }

        buffer[pos++] = c;

        // Check for CRLF
        if (pos >= 2 && buffer[pos - 2] == '\r' && buffer[pos - 1] == '\n')
        {
            buffer[pos] = '\0';
            return (int)pos;
        }
    }

    // Line too long
    buffer[buffer_size - 1] = '\0';
    return -1;
}

int net_send(socket_t connected_socket, const void *data, size_t length)
{
#ifdef _WIN32
    return send(connected_socket, (const char *)data, (int)length, 0);
#else
    return (int)send(connected_socket, data, length, 0);
#endif
}

int net_send_all(socket_t connected_socket, const void *data, size_t length)
{
    const char *ptr = (const char *)data;
    size_t total_sent = 0;

    while (total_sent < length)
    {
        int bytes_sent = net_send(connected_socket, ptr + total_sent, length - total_sent);
        if (bytes_sent <= 0)
        {
            // Error or connection closed
            return -1;
        }
        total_sent += bytes_sent;
    }

    return 0; // Success
}

void net_close_socket(socket_t sock)
{
#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
}

int net_shutdown_send(socket_t sock)
{
#ifdef _WIN32
    if (shutdown(sock, SD_SEND) < 0)
    {
        return -1;
    }
#else
    if (shutdown(sock, SHUT_WR) < 0)
    {
        return -1;
    }
#endif
    return 0;
}

int net_shutdown_recv(socket_t sock)
{
#ifdef _WIN32
    if (shutdown(sock, SD_RECEIVE) < 0)
    {
        return -1;
    }
#else
    if (shutdown(sock, SHUT_RD) < 0)
    {
        return -1;
    }
#endif
    return 0;
}

int net_shutdown_both(socket_t sock)
{
#ifdef _WIN32
    if (shutdown(sock, SD_BOTH) < 0)
    {
        return -1;
    }
#else
    if (shutdown(sock, SHUT_RDWR) < 0)
    {
        return -1;
    }
#endif
    return 0;
}

int net_set_nonblocking(socket_t sock, int enable)
{
#ifdef _WIN32
    u_long mode = enable ? 1 : 0;
    if (ioctlsocket(sock, FIONBIO, &mode) != 0)
    {
        return -1;
    }
    return 0;
#else
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags == -1)
    {
        return -1;
    }

    if (enable)
    {
        flags |= O_NONBLOCK;
    }
    else
    {
        flags &= ~O_NONBLOCK;
    }

    if (fcntl(sock, F_SETFL, flags) == -1)
    {
        return -1;
    }

    return 0;
#endif
}

int net_set_recv_timeout(socket_t sock, int timeout_ms)
{
#ifdef _WIN32
    DWORD timeout = (DWORD)timeout_ms;
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout)) < 0)
    {
        return -1;
    }
#else
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0)
    {
        return -1;
    }
#endif
    return 0;
}

int net_set_send_timeout(socket_t sock, int timeout_ms)
{
#ifdef _WIN32
    DWORD timeout = (DWORD)timeout_ms;
    if (setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char *)&timeout, sizeof(timeout)) < 0)
    {
        return -1;
    }
#else
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    if (setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0)
    {
        return -1;
    }
#endif
    return 0;
}

int net_set_tcp_nodelay(socket_t sock, int enable)
{
    int flag = enable ? 1 : 0;
    if (setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (const char *)&flag, sizeof(flag)) < 0)
    {
        return -1;
    }
    return 0;
}

int net_set_keepalive(socket_t sock, int enable)
{
    int flag = enable ? 1 : 0;
    if (setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, (const char *)&flag, sizeof(flag)) < 0)
    {
        return -1;
    }
    return 0;
}

int net_wait_readable(socket_t sock, int timeout_ms)
{
    fd_set read_fds;
    struct timeval tv;
    struct timeval *tv_ptr = NULL;

    FD_ZERO(&read_fds);
    FD_SET(sock, &read_fds);

    if (timeout_ms >= 0)
    {
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        tv_ptr = &tv;
    }

    int result = select((int)sock + 1, &read_fds, NULL, NULL, tv_ptr);

    if (result < 0)
    {
        return -1; // Error
    }
    else if (result == 0)
    {
        return 0; // Timeout
    }
    else
    {
        return 1; // Readable
    }
}

int net_wait_writable(socket_t sock, int timeout_ms)
{
    fd_set write_fds;
    struct timeval tv;
    struct timeval *tv_ptr = NULL;

    FD_ZERO(&write_fds);
    FD_SET(sock, &write_fds);

    if (timeout_ms >= 0)
    {
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        tv_ptr = &tv;
    }

    int result = select((int)sock + 1, NULL, &write_fds, NULL, tv_ptr);

    if (result < 0)
    {
        return -1; // Error
    }
    else if (result == 0)
    {
        return 0; // Timeout
    }
    else
    {
        return 1; // Writable
    }
}

int net_get_last_error(void)
{
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

const char *net_get_error_string(int error_code)
{
    static char buffer[256];

#ifdef _WIN32
    FormatMessageA(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        error_code,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        buffer,
        sizeof(buffer),
        NULL);
#else
    snprintf(buffer, sizeof(buffer), "%s", strerror(error_code));
#endif

    return buffer;
}

int net_is_would_block(int error_code)
{
#ifdef _WIN32
    return (error_code == WSAEWOULDBLOCK);
#else
    return (error_code == EAGAIN || error_code == EWOULDBLOCK);
#endif
}

int net_has_urgent_data(socket_t sock)
{
#ifdef _WIN32
    // Windows doesn't have sockatmark, use select with exception set
    fd_set except_fds;
    struct timeval tv = {0, 0};  // No wait
    
    FD_ZERO(&except_fds);
    FD_SET(sock, &except_fds);
    
    int result = select((int)sock + 1, NULL, NULL, &except_fds, &tv);
    if (result < 0)
    {
        return -1;  // Error
    }
    return FD_ISSET(sock, &except_fds) ? 1 : 0;
#else
    // POSIX: Use sockatmark to check if we're at the urgent data mark
    int atmark = sockatmark(sock);
    if (atmark < 0)
    {
        return -1;  // Error
    }
    
    // Also check for exception condition with select
    fd_set except_fds;
    struct timeval tv = {0, 0};  // No wait
    
    FD_ZERO(&except_fds);
    FD_SET(sock, &except_fds);
    
    int result = select((int)sock + 1, NULL, NULL, &except_fds, &tv);
    if (result < 0)
    {
        return -1;  // Error
    }
    
    // Return true if either at mark or exception is set
    return (atmark > 0 || FD_ISSET(sock, &except_fds)) ? 1 : 0;
#endif
}

int net_receive_urgent(socket_t sock, void *buffer, size_t buffer_size)
{
    if (!buffer || buffer_size == 0)
    {
        return -1;
    }

#ifdef _WIN32
    int result = recv(sock, (char *)buffer, (int)buffer_size, MSG_OOB);
    return result;
#else
    ssize_t result = recv(sock, buffer, buffer_size, MSG_OOB);
    return (int)result;
#endif
}

int net_set_oob_inline(socket_t sock, int enable)
{
    int opt = enable ? 1 : 0;
    
#ifdef _WIN32
    if (setsockopt(sock, SOL_SOCKET, SO_OOBINLINE, (const char *)&opt, sizeof(opt)) < 0)
    {
        return -1;
    }
#else
    if (setsockopt(sock, SOL_SOCKET, SO_OOBINLINE, &opt, sizeof(opt)) < 0)
    {
        return -1;
    }
#endif
    
    return 0;
}
