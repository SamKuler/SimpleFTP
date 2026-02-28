/**
 * @file server.h
 * @brief FTP Server core functionality
 * @version 0.1
 * @date 2025-11-02
 */

#ifndef SERVER_H
#define SERVER_H

#include "network.h"

#include <stdint.h>

/**
 * @brief Server configuration structure
 */
typedef struct
{
    uint16_t port;                    // Port to listen on
    char root_dir[1024];              // Root directory for FTP
    char bind_address[64];            // Address to bind to (e.g., "0.0.0.0" or "127.0.0.1")
    int max_backlog;                  // Maximum pending connections
    int command_timeout_ms;           // Command timeout in milliseconds
    int max_connections;              // Maximum concurrent connections (-1 for unlimited)
    net_addr_family_t address_family; // Address family: NET_AF_IPV4, NET_AF_IPV6, NET_AF_UNSPEC
} server_config_t;

/**
 * @brief Initializes the FTP server with the given configuration
 *
 * This initializes all subsystems (network, logging, commands)
 * and creates the listening socket.
 *
 * @param config Server configuration
 * @return 0 on success, -1 on error
 */
int server_init(const server_config_t *config);

/**
 * @brief Starts the FTP server main loop
 *
 * This runs the main accept loop, accepting client connections
 * and spawning threads to handle them. It blocks until server_stop() is called
 * or a fatal error occurs.
 *
 * @return 0 on success, -1 on error
 */
int server_run(void);

/**
 * @brief Stops the FTP server
 *
 * This signals the server to stop accepting new connections
 * and shut down. It can be called from a signal handler.
 */
void server_stop(void);

/**
 * @brief Cleans up server resources
 *
 * This function should be called after server_run() returns to clean up
 * all allocated resources.
 */
void server_cleanup(void);

/**
 * @brief Checks if the server is currently running
 *
 * @return 1 if running, 0 if stopped
 */
int server_is_running(void);

#endif // SERVER_H
