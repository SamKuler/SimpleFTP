/**
 * @file server.c
 * @brief FTP Server core functionality implementation
 * @version 0.2
 * @date 2025-11-03
 */

#include "server.h"
#include "network.h"
#include "logger.h"
#include "session.h"
#include "command.h"
#include "protocol.h"
#include "filesys.h"
#include "auth.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

// Server state
static volatile int g_server_running = 0;
static socket_t g_listening_socket = INVALID_SOCKET_T;
static server_config_t g_config;

// Connection tracking
static volatile int g_current_connections = 0;
static pthread_mutex_t g_connection_mutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * @brief Client session thread function
 *
 * Handles all communication with a single FTP client.
 *
 * @param arg Pointer to session_t structure
 * @return NULL
 */
static void *client_thread(void *arg)
{
    session_t *session = (session_t *)arg;
    if (!session)
    {
        LOG_ERROR("Client thread received NULL session");
        return NULL;
    }

    LOG_INFO("Client thread started for %s:%u", session->client_ip, session->client_port);

    // Configure socket to receive urgent data inline
    if (net_set_oob_inline(session->control_socket, 1) != 0)
    {
        LOG_WARN("Failed to set OOB inline mode for client %s:%u", session->client_ip, session->client_port);
    }

    // Send welcome message (220)
    if (session_send_response(session, PROTO_RESP_SERVICE_READY, "FTP Server Ready") != 0)
    {
        LOG_ERROR("Failed to send welcome message");
        session_destroy(session);

        // Decrement connection count
        pthread_mutex_lock(&g_connection_mutex);
        g_current_connections--;
        pthread_mutex_unlock(&g_connection_mutex);

        return NULL;
    }

    char command_buffer[1024];
    proto_command_t cmd;

    // Main command loop
    while (!session->should_quit && g_server_running)
    {
        LOG_DEBUG("Waiting for command from client %s:%u",
                  session->client_ip, session->client_port);
        int has_urgent = net_has_urgent_data(session->control_socket);
        if (has_urgent > 0)
        {
            LOG_INFO("Urgent data detected - priority command expected (likely ABOR)");
        }
        // Receive command from client
        int bytes_received = net_receive_line(session->control_socket,
                                              command_buffer,
                                              sizeof(command_buffer),
                                              g_config.command_timeout_ms);

        if (bytes_received <= 0)
        {
            if (bytes_received == 0)
            {
                LOG_INFO("Client %s:%u disconnected",
                         session->client_ip, session->client_port);
            }
            else
            {
                LOG_WARN("Error receiving command from client %s:%u",
                         session->client_ip, session->client_port);
            }
            break;
        }

        // Update last activity time
        session_update_activity(session);

        // Parse the command
        if (proto_parse_command(command_buffer, &cmd) != 0)
        {
            LOG_WARN("Failed to parse command: %s", command_buffer);
            session_send_response(session, PROTO_RESP_SYNTAX_ERROR, "Syntax error, command unrecognized");
            continue;
        }

        // Increment command counter
        session->commands_received++;

        // Special logging for urgent commands
        if (has_urgent > 0)
        {
            LOG_INFO("Client %s:%u: [URGENT] %s %s",
                     session->client_ip, session->client_port,
                     cmd.command, cmd.has_argument ? cmd.argument : "");
        }
        else
        {
            LOG_INFO("Client %s:%u: %s %s",
                     session->client_ip, session->client_port,
                     cmd.command, cmd.has_argument ? cmd.argument : "");
        }

        // Dispatch command to handler
        // ABOR will be dispatched through the normal command handler mechanism
        if (cmd_dispatch((cmd_handler_context_t)session, &cmd) != 0)
        {
            // Command not recognized or handler failed
            if (!cmd_is_registered(cmd.command))
            {
                LOG_WARN("Unknown command: %s", cmd.command);
                session_send_response(session, PROTO_RESP_COMMAND_NOT_IMPL, "Command not implemented");
            }
            else
            {
                LOG_WARN("Command handler failed: %s", cmd.command);
                // Handler sends appropriate error response
            }
        }
        LOG_DEBUG("Finished processing command: %s", cmd.command);
    }

    LOG_INFO("Client session ended for %s:%u", session->client_ip, session->client_port);

    // Clean up session
    session_destroy(session);

    // Decrement connection count
    pthread_mutex_lock(&g_connection_mutex);
    g_current_connections--;
    pthread_mutex_unlock(&g_connection_mutex);

    return NULL;
}

int server_init(const server_config_t *config)
{
    if (!config)
    {
        fprintf(stderr, "Invalid server configuration\n");
        return -1;
    }

    // Copy config
    g_config = *config;

    LOG_INFO("=== FTP Server Initializing ===");
    LOG_INFO("Port: %u", g_config.port);
    LOG_INFO("Bind address: %s", g_config.bind_address);
    LOG_INFO("Root directory: %s", g_config.root_dir);
    LOG_INFO("Max backlog: %d", g_config.max_backlog);
    LOG_INFO("Command timeout: %d ms", g_config.command_timeout_ms);
    LOG_INFO("Max connections: %d", g_config.max_connections);
    LOG_INFO("Address family: %d", g_config.address_family);

    // Verify root directory exists
    if (!fs_is_directory(g_config.root_dir))
    {
        LOG_ERROR("Root directory does not exist: %s", g_config.root_dir);
        LOG_INFO("Creating root directory...");
        if (fs_create_directory(g_config.root_dir) != 0)
        {
            LOG_ERROR("Failed to create root directory");
            return -1;
        }
    }

    // Initialize network subsystem
    if (net_init() != 0)
    {
        LOG_ERROR("Failed to initialize network subsystem");
        return -1;
    }

    // Initialize authentication module
    if (auth_init() != 0)
    {
        LOG_ERROR("Failed to initialize authentication module");
        net_cleanup();
        return -1;
    }

    // Enable anonymous login by default and set default home directory
    auth_set_anonymous_enabled(1);
    // REQUIRED BY HOMEWORK, PRODUCTION ENVIRONMENT SHOULD RESTRICT PERMISSIONS
    auth_set_anonymous_defaults("/", AUTH_PERM_ALL);

    // Try to load user database (optional, will warn if file doesn't exist)
    auth_load_users("users.db");

    // Initialize command module
    if (cmd_init() != 0)
    {
        LOG_ERROR("Failed to initialize command module");
        auth_cleanup();
        net_cleanup();
        return -1;
    }

    // Register standard FTP command handlers
    if (cmd_register_standard_handlers() != 0)
    {
        LOG_ERROR("Failed to register command handlers");
        cmd_cleanup();
        auth_cleanup();
        net_cleanup();
        return -1;
    }

    LOG_INFO("Registered %d command handlers", cmd_get_handler_count());
    LOG_DEBUG("All registered commands:\n%s", cmd_get_all_registered_commands());

    // Create listening socket
    g_listening_socket = net_create_listening_socket(g_config.address_family,
                                                     g_config.bind_address,
                                                     g_config.port,
                                                     g_config.max_backlog);
    if (g_listening_socket == INVALID_SOCKET_T)
    {
        LOG_ERROR("Failed to create listening socket on port %u", g_config.port);
        cmd_cleanup();
        auth_cleanup();
        net_cleanup();
        return -1;
    }

    LOG_INFO("Server initialized successfully");
    g_server_running = 1;

    return 0;
}

int server_run(void)
{
    if (!g_server_running)
    {
        LOG_ERROR("Server not initialized");
        return -1;
    }

    LOG_INFO("Server listening on port %u", g_config.port);
    LOG_INFO("Waiting for connections...");

    // Main accept loop
    while (g_server_running)
    {
        char client_ip[64];
        uint16_t client_port;

        // Accept incoming connection
        socket_t client_socket = net_accept(g_listening_socket,
                                            client_ip, sizeof(client_ip),
                                            &client_port);

        if (client_socket == INVALID_SOCKET_T)
        {
            if (g_server_running)
            {
                LOG_ERROR("Failed to accept client connection");
            }
            continue;
        }

        LOG_INFO("Accepted connection from %s:%u", client_ip, client_port);

        // Check if server is busy (connection limit exceeded)
        int is_busy = 0;
        if (g_config.max_connections > 0)
        {
            pthread_mutex_lock(&g_connection_mutex);
            if (g_current_connections >= g_config.max_connections)
            {
                is_busy = 1;
            }
            else
            {
                g_current_connections++;
            }
            pthread_mutex_unlock(&g_connection_mutex);
        }
        else
        {
            // No limit, just increment connection count
            pthread_mutex_lock(&g_connection_mutex);
            g_current_connections++;
            pthread_mutex_unlock(&g_connection_mutex);
        }

        // If server is busy, send busy response and close connection
        if (is_busy)
        {
            LOG_WARN("Server busy, rejecting connection from %s:%u (max connections: %d)",
                     client_ip, client_port, g_config.max_connections);

            // Send busy response (421 Service not available)
            // Session has not been established yet,
            // use raw proto_format_response() to generate message.
            char busy_response[PROTO_MAX_RESPONSE_LINE];
            proto_format_response(busy_response, sizeof(busy_response), PROTO_RESP_SERVICE_NOT_AVAIL, "Service not available, too many connections");
            net_send_all(client_socket, busy_response, strlen(busy_response));

            // Close connection
            net_close_socket(client_socket);
            continue;
        }

        // Create session for this client
        session_t *session = session_create(client_socket, client_ip,
                                            client_port, g_config.root_dir,
                                            g_config.bind_address);
        if (!session)
        {
            LOG_ERROR("Failed to create session for client %s:%u", client_ip, client_port);
            net_close_socket(client_socket);

            // Decrement connection count on session creation failure
            pthread_mutex_lock(&g_connection_mutex);
            g_current_connections--;
            pthread_mutex_unlock(&g_connection_mutex);

            continue;
        }

        // Create thread to handle this client
        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, client_thread, session) != 0)
        {
            LOG_ERROR("Failed to create thread for client %s:%u", client_ip, client_port);
            session_destroy(session);

            // Decrement connection count on session creation failure
            pthread_mutex_lock(&g_connection_mutex);
            g_current_connections--;
            pthread_mutex_unlock(&g_connection_mutex);

            continue;
        }

        // Detach thread so it cleans up automatically when done
        pthread_detach(thread_id);

        LOG_DEBUG("Created thread for client %s:%u", client_ip, client_port);
    }

    LOG_INFO("Server stopped accepting connections");

    return 0;
}

void server_stop(void)
{
    LOG_INFO("Stopping server...");
    g_server_running = 0;

    // Close listening socket to unblock accept()
    if (g_listening_socket != INVALID_SOCKET_T)
    {
        net_close_socket(g_listening_socket);
        g_listening_socket = INVALID_SOCKET_T;
    }
}

void server_cleanup(void)
{
    LOG_INFO("Cleaning up server resources...");

    if (g_listening_socket != INVALID_SOCKET_T)
    {
        net_close_socket(g_listening_socket);
        g_listening_socket = INVALID_SOCKET_T;
    }

    cmd_cleanup();
    auth_cleanup();
    net_cleanup();

    g_server_running = 0;

    // Reset connection count
    pthread_mutex_lock(&g_connection_mutex);
    g_current_connections = 0;
    pthread_mutex_unlock(&g_connection_mutex);

    LOG_INFO("Server cleanup completed");
}

int server_is_running(void)
{
    return g_server_running;
}
