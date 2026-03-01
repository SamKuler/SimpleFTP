/**
 * @file handlers.c
 * @brief Implementation of FTP command handlers
 * @version 0.5
 * @date 2025-11-06
 */
#include "command.h"
#include "session.h"
#include "protocol.h"
#include "transfer.h"
#include "filesys.h"
#include "filelock.h"
#include "logger.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Previous Handlers for context changing

int cmd_prev_handle_clear_restart(cmd_handler_context_t context, const proto_command_t *cmd)
{
    (void)cmd;

    session_t *session = (session_t *)context;
    if (!session)
    {
        return -1;
    }

    session_clear_restart_offset(session);
    return 0;
}

int cmd_prev_handle_clear_rename(cmd_handler_context_t context, const proto_command_t *cmd)
{
    (void)cmd;

    session_t *session = (session_t *)context;
    if (!session)
    {
        return -1;
    }

    session_clear_rename_state(session);
    return 0;
}

// Clear both restart offset and rename state
int cmd_prev_handle_clear_all(cmd_handler_context_t context, const proto_command_t *cmd)
{
    (void)cmd;

    session_t *session = (session_t *)context;
    if (!session)
    {
        return -1;
    }

    session_clear_restart_offset(session);
    session_clear_rename_state(session);
    return 0;
}

// Access Control Commands

// Login Commands

int cmd_handle_user(cmd_handler_context_t context, const proto_command_t *cmd)
{
    session_t *session = (session_t *)context;

    if (!cmd->has_argument)
    {
        return session_send_response(session, PROTO_RESP_SYNTAX_ERROR_PARAM,
                                     "Syntax error in parameters");
    }

    const char *username = cmd->argument;

    // Check if user exists or if anonymous login is enabled
    int user_exists = auth_user_exists(username);
    int is_anonymous = (strcmp(username, "anonymous") == 0);
    int anonymous_enabled = auth_is_anonymous_enabled();

    // If not anonymous and user doesn't exist, reject
    if (!is_anonymous && !user_exists)
    {
        LOG_WARN("User '%s' not found from %s:%u", username,
                 session->client_ip, session->client_port);
        return session_send_response(session, PROTO_RESP_NOT_LOGGED_IN,
                                     "User not found");
    }

    // If anonymous but anonymous login is disabled, reject
    if (is_anonymous && !anonymous_enabled)
    {
        LOG_WARN("Anonymous login disabled, rejected from %s:%u",
                 session->client_ip, session->client_port);
        return session_send_response(session, PROTO_RESP_NOT_LOGGED_IN,
                                     "Anonymous login not allowed");
    }

    // Set the username in session and update state
    session_set_user(session, username);

    LOG_INFO("User '%s' from %s:%u", username,
             session->client_ip, session->client_port);

    // For anonymous users, send appropriate message
    if (is_anonymous)
    {
        return session_send_response(session, PROTO_RESP_NEED_PASSWORD,
                                     "Anonymous login OK, send your email as password");
    }
    else
    {
        return session_send_response(session, PROTO_RESP_NEED_PASSWORD,
                                     "Username OK, need password");
    }
}

int cmd_handle_pass(cmd_handler_context_t context, const proto_command_t *cmd)
{
    session_t *session = (session_t *)context;

    if (session->state != SESSION_STATE_WAIT_PASSWORD)
    {
        return session_send_response(session, PROTO_RESP_BAD_COMMAND_SEQUENCE,
                                     "Login with USER first");
    }

    // Get password from command argument
    const char *password = cmd->has_argument ? cmd->argument : "";

    // Authenticate
    if (session_authenticate(session, password) != 0)
    {
        LOG_WARN("Authentication failed for user '%s' from %s:%u",
                 session->username, session->client_ip, session->client_port);
        return session_send_response(session, PROTO_RESP_NOT_LOGGED_IN,
                                     "Login incorrect");
    }

    LOG_INFO("User '%s' logged in from %s:%u",
             session->username, session->client_ip, session->client_port);

    return session_send_response(session, PROTO_RESP_USER_LOGGED_IN,
                                 "User logged in, proceed");
}

int cmd_handle_acct(cmd_handler_context_t context, const proto_command_t *cmd)
{
    (void)cmd; // Unused parameter
    session_t *session = (session_t *)context;
    return session_send_response(session, PROTO_RESP_COMMAND_NOT_IMPL,
                                 "ACCT not implemented");
}

int cmd_handle_cwd(cmd_handler_context_t context, const proto_command_t *cmd)
{
    session_t *session = (session_t *)context;

    if (!session->authenticated)
    {
        return session_send_response(session, PROTO_RESP_NOT_LOGGED_IN,
                                     "Please login with USER and PASS");
    }

    if (!cmd->has_argument)
    {
        return session_send_response(session, PROTO_RESP_SYNTAX_ERROR_PARAM,
                                     "Syntax error in parameters");
    }

    if (session_change_directory(session, cmd->argument) != 0)
    {
        return session_send_response(session, PROTO_RESP_FILE_UNAVAILABLE,
                                     "Failed to change directory");
    }

    return session_send_response(session, PROTO_RESP_FILE_ACTION_OK,
                                 "Directory successfully changed");
}

int cmd_handle_cdup(cmd_handler_context_t context, const proto_command_t *cmd)
{
    session_t *session = (session_t *)context;

    if (!session->authenticated)
    {
        return session_send_response(session, PROTO_RESP_NOT_LOGGED_IN,
                                     "Please login with USER and PASS");
    }

    if (cmd->has_argument)
    {
        return session_send_response(session, PROTO_RESP_SYNTAX_ERROR_PARAM,
                                     "CDUP does not take parameters");
    }

    if (session_change_directory(session, "..") != 0)
    {
        return session_send_response(session, PROTO_RESP_FILE_UNAVAILABLE,
                                     "Failed to change to parent directory");
    }

    return session_send_response(session, PROTO_RESP_FILE_ACTION_OK,
                                 "Directory successfully changed");
}

int cmd_handle_smnt(cmd_handler_context_t context, const proto_command_t *cmd)
{
    (void)cmd; // Unused parameter
    session_t *session = (session_t *)context;
    return session_send_response(session, PROTO_RESP_COMMAND_NOT_IMPL,
                                 "SMNT not implemented");
}

// Logout Commands

int cmd_handle_quit(cmd_handler_context_t context, const proto_command_t *cmd)
{
    session_t *session = (session_t *)context;

    if (cmd->has_argument)
    {
        return session_send_response(session, PROTO_RESP_SYNTAX_ERROR_PARAM,
                                     "QUIT does not take parameters");
    }

    // Calculate session duration
    time_t session_duration = time(NULL) - session->connect_time;

    // Log the logout with statistics
    if (session->authenticated && session->username[0] != '\0')
    {
        LOG_INFO("User '%s' logging out from %s:%u - Stats: %llu bytes uploaded, %llu bytes downloaded, %u files up, %u files down, %u commands, %ld seconds",
                 session->username, session->client_ip, session->client_port,
                 session->bytes_uploaded, session->bytes_downloaded,
                 session->files_uploaded, session->files_downloaded,
                 session->commands_received, (long)session_duration);
    }
    else
    {
        LOG_INFO("Client %s:%u disconnecting (not logged in) - %u commands, %ld seconds",
                 session->client_ip, session->client_port,
                 session->commands_received, (long)session_duration);
    }

    session->should_quit = 1;

    // Send goodbye response with statistics (221 Service closing control connection)
    if (session->authenticated)
    {
        // Send multi-line response with statistics for authenticated users
        session_send_response_multiline(session, PROTO_RESP_CLOSING_CONTROL,
                                        "Goodbye! Session statistics:");

        char stat_line[256];
        snprintf(stat_line, sizeof(stat_line),
                 "  Data uploaded: %llu bytes", session->bytes_uploaded);
        session_send_response_multiline(session, PROTO_RESP_CLOSING_CONTROL, stat_line);

        snprintf(stat_line, sizeof(stat_line),
                 "  Data downloaded: %llu bytes", session->bytes_downloaded);
        session_send_response_multiline(session, PROTO_RESP_CLOSING_CONTROL, stat_line);

        snprintf(stat_line, sizeof(stat_line),
                 "  Files uploaded: %u", session->files_uploaded);
        session_send_response_multiline(session, PROTO_RESP_CLOSING_CONTROL, stat_line);

        snprintf(stat_line, sizeof(stat_line),
                 "  Files downloaded: %u", session->files_downloaded);
        session_send_response_multiline(session, PROTO_RESP_CLOSING_CONTROL, stat_line);

        snprintf(stat_line, sizeof(stat_line),
                 "  Commands received: %u", session->commands_received);
        session_send_response_multiline(session, PROTO_RESP_CLOSING_CONTROL, stat_line);

        snprintf(stat_line, sizeof(stat_line),
                 "  Session duration: %ld seconds", (long)session_duration);
        session_send_response_multiline(session, PROTO_RESP_CLOSING_CONTROL, stat_line);

        return session_send_response(session, PROTO_RESP_CLOSING_CONTROL,
                                     "Closing connection");
    }
    else
    {
        // Simple goodbye for non-authenticated users
        char goodbye_msg[256];
        snprintf(goodbye_msg, sizeof(goodbye_msg),
                 "Goodbye. Session duration: %ld seconds",
                 (long)session_duration);
        return session_send_response(session, PROTO_RESP_CLOSING_CONTROL, goodbye_msg);
    }
}

int cmd_handle_rein(cmd_handler_context_t context, const proto_command_t *cmd)
{
    session_t *session = (session_t *)context;

    if (cmd->has_argument)
    {
        return session_send_response(session, PROTO_RESP_SYNTAX_ERROR_PARAM,
                                     "REIN does not take parameters");
    }

    LOG_INFO("Reinitializing session for %s:%u", session->client_ip, session->client_port);

    // Close any existing data connections
    session_close_data_connection(session);

    pthread_mutex_lock(&session->lock);

    // Reset authentication state
    session->authenticated = 0;
    session->state = SESSION_STATE_CONNECTED;
    memset(session->username, 0, sizeof(session->username));
    session->permissions = AUTH_PERM_NONE;

    // Reset directory state
    strcpy(session->current_dir, "/");
    memset(session->user_home_dir, 0, sizeof(session->user_home_dir));

    // Reset transfer parameters to defaults
    session->transfer_type = PROTO_TYPE_ASCII;
    session->transfer_mode = PROTO_MODE_STREAM;
    session->data_structure = PROTO_STRU_FILE;

    // Reset data connection mode
    session->data_mode = SESSION_DATA_MODE_NONE;
    memset(session->active_ip, 0, sizeof(session->active_ip));
    session->active_port = 0;
    session->passive_port = 0;

    // Clear command state
    session->restart_offset = 0;
    session->rename_pending = 0;
    memset(session->rename_from, 0, sizeof(session->rename_from));

    // Clear transfer state
    session->transfer_should_abort = 0;
    session->transfer_in_progress = 0;
    session->transfer_thread = 0;
    session->transfer_thread_state = TRANSFER_THREAD_IDLE;
    session->transfer_params = NULL;
    session->transfer_result = TRANSFER_STATUS_OK;

    // Do not reset statistics on REIN

    pthread_mutex_unlock(&session->lock);

    return session_send_response(session, PROTO_RESP_SERVICE_READY,
                                 "Service ready for new user");
}

// Transfer Parameter Commands

int cmd_handle_port(cmd_handler_context_t context, const proto_command_t *cmd)
{
    session_t *session = (session_t *)context;

    if (!session->authenticated)
    {
        return session_send_response(session, PROTO_RESP_NOT_LOGGED_IN,
                                     "Please login with USER and PASS");
    }

    if (!cmd->has_argument)
    {
        return session_send_response(session, PROTO_RESP_SYNTAX_ERROR_PARAM,
                                     "Syntax error in parameters");
    }

    proto_port_params_t port_params;
    if (proto_parse_port(cmd->argument, &port_params) != 0)
    {
        return session_send_response(session, PROTO_RESP_SYNTAX_ERROR_PARAM,
                                     "Invalid PORT parameters");
    }

    char client_ip[64];
    uint16_t client_port;

    if (proto_port_to_address(&port_params, client_ip, sizeof(client_ip),
                              &client_port) != 0)
    {
        return session_send_response(session, PROTO_RESP_SYNTAX_ERROR_PARAM,
                                     "Invalid PORT address");
    }

    if (session_set_port(session, client_ip, client_port) != 0)
    {
        return session_send_response(session, PROTO_RESP_LOCAL_ERROR,
                                     "Failed to set PORT mode");
    }

    LOG_DEBUG("PORT mode set: %s:%u", client_ip, client_port);

    return session_send_response(session, PROTO_RESP_OK,
                                 "PORT command successful");
}

int cmd_handle_pasv(cmd_handler_context_t context, const proto_command_t *cmd)
{
    session_t *session = (session_t *)context;

    if (!session->authenticated)
    {
        return session_send_response(session, PROTO_RESP_NOT_LOGGED_IN,
                                     "Please login with USER and PASS");
    }

    if (cmd->has_argument)
    {
        return session_send_response(session, PROTO_RESP_SYNTAX_ERROR_PARAM,
                                     "PASV does not take parameters");
    }

    // Get server IP (simplified - use control connection IP)
    char server_ip[64];
    uint16_t dummy_port;

    if (net_get_socket_info(session->control_socket, server_ip,
                            sizeof(server_ip), &dummy_port) != 0)
    {
        return session_send_response(session, PROTO_RESP_LOCAL_ERROR,
                                     "Failed to get server address");
    }

    // Set passive mode (ports 20000-65535)
    if (session_set_pasv(session, 20000, 65535, server_ip) != 0)
    {
        return session_send_response(session, PROTO_RESP_LOCAL_ERROR,
                                     "Failed to enter passive mode");
    }

    // Build PASV response
    proto_pasv_params_t pasv_params;
    if (proto_address_to_pasv(server_ip, session->passive_port, &pasv_params) != 0)
    {
        session_close_data_connection(session); // Clean up on error
        return session_send_response(session, PROTO_RESP_LOCAL_ERROR,
                                     "Failed to format PASV response");
    }

    char response[256];
    if (proto_format_pasv_response(response, sizeof(response), &pasv_params) < 0)
    {
        session_close_data_connection(session); // Clean up on error
        return session_send_response(session, PROTO_RESP_LOCAL_ERROR,
                                     "Failed to format PASV response");
    }

    LOG_DEBUG("PASV mode: %s:%u", server_ip, session->passive_port);

    // Send the pre-formatted response directly
    int result = net_send_all(session->control_socket, response, strlen(response));
    if (result != 0)
    {
        LOG_ERROR("Failed to send PASV response");
        session_close_data_connection(session);
        return -1;
    }

    return 0;
}

int cmd_handle_type(cmd_handler_context_t context, const proto_command_t *cmd)
{
    session_t *session = (session_t *)context;

    if (!session->authenticated)
    {
        return session_send_response(session, PROTO_RESP_NOT_LOGGED_IN,
                                     "Please login with USER and PASS");
    }

    if (!cmd->has_argument)
    {
        return session_send_response(session, PROTO_RESP_SYNTAX_ERROR_PARAM,
                                     "Syntax error in parameters");
    }

    proto_transfer_type_t type;
    if (proto_parse_type(cmd->argument, &type) != 0)
    {
        return session_send_response(session, PROTO_RESP_SYNTAX_ERROR_PARAM,
                                     "Invalid type parameter");
    }

    if (type == PROTO_TYPE_EBCDIC)
    {
        return session_send_response(session, PROTO_RESP_COMMAND_NOT_IMPL_PARAM,
                                     "Type not supported (EBCDIC not supported)");
    }

    session_set_type(session, type);

    const char *type_name = (type == PROTO_TYPE_ASCII)    ? "A"
                            : (type == PROTO_TYPE_BINARY) ? "I"
                                                          : "E";

    char msg[128];
    snprintf(msg, sizeof(msg), "Type set to %s.", type_name);

    return session_send_response(session, PROTO_RESP_OK, msg);
}

int cmd_handle_stru(cmd_handler_context_t context, const proto_command_t *cmd)
{
    session_t *session = (session_t *)context;

    if (!session->authenticated)
    {
        return session_send_response(session, PROTO_RESP_NOT_LOGGED_IN,
                                     "Please login with USER and PASS");
    }

    if (!cmd->has_argument)
    {
        return session_send_response(session, PROTO_RESP_SYNTAX_ERROR_PARAM,
                                     "Syntax error in parameters");
    }

    proto_data_structure_t structure;
    if (proto_parse_stru(cmd->argument, &structure) != 0)
    {
        return session_send_response(session, PROTO_RESP_SYNTAX_ERROR_PARAM,
                                     "Invalid structure parameter");
    }

    // Check if the requested structure is supported
    if (structure != PROTO_STRU_FILE)
    {
        // Server only supports File structure
        const char *stru_name = (structure == PROTO_STRU_RECORD) ? "Record"
                                : (structure == PROTO_STRU_PAGE) ? "Page"
                                                                 : "Unknown";
        LOG_WARN("Unsupported structure type requested: %s", stru_name);

        return session_send_response(session, PROTO_RESP_COMMAND_NOT_IMPL_PARAM,
                                     "Structure not supported (only File structure)");
    }

    // Set the structure (File is the default and only supported)
    session_set_structure(session, structure);

    LOG_DEBUG("Structure set to File for user '%s'", session->username);

    return session_send_response(session, PROTO_RESP_OK,
                                 "Structure set to File");
}

int cmd_handle_mode(cmd_handler_context_t context, const proto_command_t *cmd)
{
    session_t *session = (session_t *)context;

    if (!session->authenticated)
    {
        return session_send_response(session, PROTO_RESP_NOT_LOGGED_IN,
                                     "Please login with USER and PASS");
    }

    if (!cmd->has_argument)
    {
        return session_send_response(session, PROTO_RESP_SYNTAX_ERROR_PARAM,
                                     "Syntax error in parameters");
    }

    proto_transfer_mode_t mode;
    if (proto_parse_mode(cmd->argument, &mode) != 0)
    {
        return session_send_response(session, PROTO_RESP_SYNTAX_ERROR_PARAM,
                                     "Invalid mode parameter");
    }

    if (mode != PROTO_MODE_STREAM)
    {
        return session_send_response(session, PROTO_RESP_COMMAND_NOT_IMPL_PARAM,
                                     "Mode not supported (only Stream mode supported)");
    }

    session_set_mode(session, mode);

    return session_send_response(session, PROTO_RESP_OK,
                                 "Mode set to Stream");
}

// FTP Service Commands

int cmd_handle_retr(cmd_handler_context_t context, const proto_command_t *cmd)
{
    session_t *session = (session_t *)context;

    if (!session->authenticated)
    {
        return session_send_response(session, PROTO_RESP_NOT_LOGGED_IN,
                                     "Please login with USER and PASS");
    }

    if (!cmd->has_argument)
    {
        return session_send_response(session, PROTO_RESP_SYNTAX_ERROR_PARAM,
                                     "Syntax error in parameters");
    }

    // Check if data transfer mode is set (PASV or PORT must be called first)
    if (session->data_mode == SESSION_DATA_MODE_NONE)
    {
        return session_send_response(session, PROTO_RESP_BAD_COMMAND_SEQUENCE,
                                     "Use PASV or PORT first");
    }

    // Check path access permission (READ required for downloading)
    if (!session_check_path_access(session, cmd->argument, AUTH_PERM_READ))
    {
        LOG_WARN("User '%s' denied read access to: %s", session->username, cmd->argument);
        return session_send_response(session, PROTO_RESP_FILE_UNAVAILABLE,
                                     "Permission denied");
    }

    char abs_path[SESSION_MAX_PATH];
    if (session_resolve_path(session, cmd->argument, abs_path, sizeof(abs_path)) != 0)
    {
        return session_send_response(session, PROTO_RESP_FILE_UNAVAILABLE,
                                     "Invalid path");
    }

    // Check if file exists and is a regular file (not a directory)
    if (!fs_path_exists(abs_path))
    {
        return session_send_response(session, PROTO_RESP_FILE_UNAVAILABLE,
                                     "File not found");
    }

    // Get restart offset (for REST + RETR)
    long long offset = session_get_restart_offset(session);

    // Error handling variables
    int response = -1;
    int lock_acquired = 0;
    int data_connection_opened = 0;

    // Use do-while(0) for structured error handling
    do
    {
        // Try to acquire shared lock without blocking
        // If file is being written, fail immediately so client can retry
        if (file_lock_try_acquire_shared(abs_path) != 0)
        {
            response = session_send_response(session, PROTO_RESP_FILE_ACTION_ABORTED,
                                             "File is busy, try again later");
            break;
        }
        lock_acquired = 1;

        // Revalidate file state while holding the lock
        if (!fs_path_exists(abs_path))
        {
            response = session_send_response(session, PROTO_RESP_FILE_UNAVAILABLE,
                                             "File not found");
            break;
        }

        if (fs_is_directory(abs_path))
        {
            response = session_send_response(session, PROTO_RESP_FILE_UNAVAILABLE,
                                             "Cannot download a directory");
            break;
        }

        long long file_size = fs_get_file_size(abs_path);
        if (file_size < 0)
        {
            response = session_send_response(session, PROTO_RESP_FILE_UNAVAILABLE,
                                             "Cannot read file");
            break;
        }

        if (offset > file_size)
        {
            response = session_send_response(session, PROTO_RESP_FILE_UNAVAILABLE,
                                             "Invalid restart offset");
            break;
        }

        // Inform client that transfer is starting (150 reply)
        char msg[PROTO_MAX_RESPONSE_LINE];
        snprintf(msg, sizeof(msg), "Opening %s mode data connection for %s (%lld bytes)",
                 (session->transfer_type == PROTO_TYPE_ASCII) ? "ASCII" : "BINARY",
                 cmd->argument, file_size - offset);
        if (session_send_response(session, PROTO_RESP_FILE_STATUS_OK, msg) != 0)
        {
            response = -1;
            break;
        }

        if (session_open_data_connection(session, 10000) != 0)
        {
            response = session_send_response(session, PROTO_RESP_CANT_OPEN_DATA,
                                             "Can't open data connection");
            break;
        }
        data_connection_opened = 1;

        // Start transfer thread, clear restart offset
        session_clear_restart_offset(session);

        // Prepare transfer parameters
        transfer_params_t params;
        memset(&params, 0, sizeof(params));
        params.operation = TRANSFER_OP_SEND_FILE;
        strncpy(params.filepath, abs_path, sizeof(params.filepath) - 1);
        params.offset = offset;
        params.type = session->transfer_type;
        params.lock_acquired = lock_acquired; // Transfer lock ownership to thread

        // Start async transfer thread
        if (session_start_transfer_thread(session, &params) != 0)
        {
            response = session_send_response(session, PROTO_RESP_LOCAL_ERROR,
                                             "Failed to start transfer");
            break;
        }

        // Transfer started successfully, response will be sent by transfer thread
        // Data connection will be closed by the transfer thread
        // File lock will be released by the transfer thread
        response = 0;
        lock_acquired = 0;          // Don't release lock here, transfer thread will do it
        data_connection_opened = 0; // Don't close data connection here
    } while (0);

    if (data_connection_opened)
    {
        session_close_data_connection(session);
    }

    if (lock_acquired)
    {
        file_lock_release_shared(abs_path);
    }

    return response;
}

int cmd_handle_stor(cmd_handler_context_t context, const proto_command_t *cmd)
{
    session_t *session = (session_t *)context;

    if (!session->authenticated)
    {
        return session_send_response(session, PROTO_RESP_NOT_LOGGED_IN,
                                     "Please login with USER and PASS");
    }

    if (!cmd->has_argument)
    {
        return session_send_response(session, PROTO_RESP_SYNTAX_ERROR_PARAM,
                                     "Syntax error in parameters");
    }

    // Check if data transfer mode is set (PASV or PORT must be called first)
    if (session->data_mode == SESSION_DATA_MODE_NONE)
    {
        return session_send_response(session, PROTO_RESP_BAD_COMMAND_SEQUENCE,
                                     "Use PASV or PORT first");
    }

    // Check path access permission (WRITE required for uploading)
    if (!session_check_path_access(session, cmd->argument, AUTH_PERM_WRITE))
    {
        LOG_WARN("User '%s' denied write access to: %s", session->username, cmd->argument);
        return session_send_response(session, PROTO_RESP_FILE_UNAVAILABLE,
                                     "Permission denied");
    }

    char abs_path[SESSION_MAX_PATH];
    if (session_resolve_path(session, cmd->argument, abs_path, sizeof(abs_path)) != 0)
    {
        return session_send_response(session, PROTO_RESP_FILE_UNAVAILABLE,
                                     "Invalid path");
    }

    // Check if path exists and is a directory (cannot overwrite directory)
    if (fs_is_directory(abs_path))
    {
        return session_send_response(session, PROTO_RESP_FILE_UNAVAILABLE,
                                     "Cannot upload to a directory");
    }

    // Get restart offset (for REST + STOR)
    long long offset = session_get_restart_offset(session);

    int response = -1;
    int lock_acquired = 0;
    int data_connection_opened = 0;

    // Use do-while(0) for structured error handling
    do
    {
        // Try to acquire exclusive lock without blocking
        // If file is being read/written, fail immediately so client can retry
        if (file_lock_try_acquire_exclusive(abs_path) != 0)
        {
            response = session_send_response(session, PROTO_RESP_FILE_ACTION_ABORTED,
                                             "File is busy, try again later");
            break;
        }
        lock_acquired = 1;

        // Revalidate file state while holding the lock
        if (offset > 0)
        {
            if (!fs_path_exists(abs_path))
            {
                response = session_send_response(session, PROTO_RESP_FILE_UNAVAILABLE,
                                                 "File does not exist for resume");
                break;
            }

            long long file_size = fs_get_file_size(abs_path);
            if (file_size < 0)
            {
                response = session_send_response(session, PROTO_RESP_FILE_UNAVAILABLE,
                                                 "Cannot read file");
                break;
            }

            if (offset > file_size)
            {
                response = session_send_response(session, PROTO_RESP_FILE_UNAVAILABLE,
                                                 "Invalid restart offset");
                break;
            }
        }
        else
        {
            // Fresh upload should replace existing file to avoid stale data
            if (fs_path_exists(abs_path) && fs_delete_file(abs_path) != 0)
            {
                LOG_WARN("User '%s' cannot overwrite file: %s", session->username, abs_path);
                response = session_send_response(session, PROTO_RESP_FILE_UNAVAILABLE,
                                                 "Cannot overwrite existing file");
                break;
            }
        }

        // Inform client that transfer is starting (150 reply)
        char msg[PROTO_MAX_RESPONSE_LINE];
        snprintf(msg, sizeof(msg), "Opening %s mode data connection for %s",
                 (session->transfer_type == PROTO_TYPE_ASCII) ? "ASCII" : "BINARY",
                 cmd->argument);
        if (session_send_response(session, PROTO_RESP_FILE_STATUS_OK, msg) != 0)
        {
            response = -1;
            break;
        }

        if (session_open_data_connection(session, 10000) != 0)
        {
            response = session_send_response(session, PROTO_RESP_CANT_OPEN_DATA,
                                             "Can't open data connection");
            break;
        }
        data_connection_opened = 1;

        // Start transfer thread, clear restart offset
        session_clear_restart_offset(session);

        // Prepare transfer parameters
        transfer_params_t params;
        memset(&params, 0, sizeof(params));
        params.operation = TRANSFER_OP_RECV_FILE;
        strncpy(params.filepath, abs_path, sizeof(params.filepath) - 1);
        params.offset = offset;
        params.type = session->transfer_type;
        params.lock_acquired = lock_acquired; // Transfer lock ownership to thread

        // Start async transfer thread
        if (session_start_transfer_thread(session, &params) != 0)
        {
            response = session_send_response(session, PROTO_RESP_LOCAL_ERROR,
                                             "Failed to start transfer");
            break;
        }

        // Transfer started successfully, response will be sent by transfer thread
        // Data connection will be closed by the transfer thread
        // File lock will be released by the transfer thread
        response = 0;
        lock_acquired = 0;          // Don't release lock here, transfer thread will do it
        data_connection_opened = 0; // Don't close data connection here
    } while (0);

    if (data_connection_opened)
    {
        session_close_data_connection(session);
    }

    if (lock_acquired)
    {
        file_lock_release_exclusive(abs_path);
    }

    return response;
}

int cmd_handle_appe(cmd_handler_context_t context, const proto_command_t *cmd)
{
    session_t *session = (session_t *)context;

    if (!session->authenticated)
    {
        return session_send_response(session, PROTO_RESP_NOT_LOGGED_IN,
                                     "Please login with USER and PASS");
    }

    if (!cmd->has_argument)
    {
        return session_send_response(session, PROTO_RESP_SYNTAX_ERROR_PARAM,
                                     "Syntax error in parameters");
    }

    // Check if data transfer mode is set (PASV or PORT must be called first)
    if (session->data_mode == SESSION_DATA_MODE_NONE)
    {
        return session_send_response(session, PROTO_RESP_BAD_COMMAND_SEQUENCE,
                                     "Use PASV or PORT first");
    }

    // Check path access permission (WRITE required for uploading)
    if (!session_check_path_access(session, cmd->argument, AUTH_PERM_WRITE))
    {
        LOG_WARN("User '%s' denied write access to: %s", session->username, cmd->argument);
        return session_send_response(session, PROTO_RESP_FILE_UNAVAILABLE,
                                     "Permission denied");
    }

    char abs_path[SESSION_MAX_PATH];
    if (session_resolve_path(session, cmd->argument, abs_path, sizeof(abs_path)) != 0)
    {
        return session_send_response(session, PROTO_RESP_FILE_UNAVAILABLE,
                                     "Invalid path");
    }

    // Check if path exists and is a directory (cannot append to directory)
    if (fs_is_directory(abs_path))
    {
        return session_send_response(session, PROTO_RESP_FILE_UNAVAILABLE,
                                     "Cannot append to a directory");
    }

    int response = -1;
    int lock_acquired = 0;
    int data_connection_opened = 0;

    // Use do-while(0) for structured error handling
    do
    {
        // Try to acquire exclusive lock without blocking
        // If file is being read/written, fail immediately so client can retry
        if (file_lock_try_acquire_exclusive(abs_path) != 0)
        {
            response = session_send_response(session, PROTO_RESP_FILE_ACTION_ABORTED,
                                             "File is busy, try again later");
            break;
        }
        lock_acquired = 1;

        // Determine offset: if file exists, append to end; otherwise start from 0
        long long offset = 0;
        if (fs_path_exists(abs_path))
        {
            offset = fs_get_file_size(abs_path);
            if (offset < 0)
            {
                response = session_send_response(session, PROTO_RESP_FILE_UNAVAILABLE,
                                                 "Cannot read file");
                break;
            }
        }

        // Inform client that transfer is starting (150 reply)
        char msg[PROTO_MAX_RESPONSE_LINE];
        snprintf(msg, sizeof(msg), "Opening %s mode data connection for %s",
                 (session->transfer_type == PROTO_TYPE_ASCII) ? "ASCII" : "BINARY",
                 cmd->argument);
        if (session_send_response(session, PROTO_RESP_FILE_STATUS_OK, msg) != 0)
        {
            response = -1;
            break;
        }

        if (session_open_data_connection(session, 10000) != 0)
        {
            response = session_send_response(session, PROTO_RESP_CANT_OPEN_DATA,
                                             "Can't open data connection");
            break;
        }
        data_connection_opened = 1;

        // Prepare transfer parameters
        transfer_params_t params;
        memset(&params, 0, sizeof(params));
        params.operation = TRANSFER_OP_RECV_FILE;
        strncpy(params.filepath, abs_path, sizeof(params.filepath) - 1);
        params.offset = offset;
        params.type = session->transfer_type;
        params.lock_acquired = lock_acquired; // Transfer lock ownership to thread

        // Start async transfer thread
        if (session_start_transfer_thread(session, &params) != 0)
        {
            response = session_send_response(session, PROTO_RESP_LOCAL_ERROR,
                                             "Failed to start transfer");
            break;
        }

        // Transfer started successfully, response will be sent by transfer thread
        // Data connection will be closed by the transfer thread
        // File lock will be released by the transfer thread
        response = 0;
        lock_acquired = 0;          // Don't release lock here, transfer thread will do it
        data_connection_opened = 0; // Don't close data connection here
    } while (0);

    if (data_connection_opened)
    {
        session_close_data_connection(session);
    }

    if (lock_acquired)
    {
        file_lock_release_exclusive(abs_path);
    }

    return response;
}

int cmd_handle_rest(cmd_handler_context_t context, const proto_command_t *cmd)
{
    session_t *session = (session_t *)context;

    if (!session->authenticated)
    {
        return session_send_response(session, PROTO_RESP_NOT_LOGGED_IN,
                                     "Please login with USER and PASS");
    }

    if (!cmd->has_argument)
    {
        return session_send_response(session, PROTO_RESP_SYNTAX_ERROR_PARAM,
                                     "Syntax error in parameters");
    }

    // Parse the restart offset
    char *endptr;
    long long offset = strtoll(cmd->argument, &endptr, 10);

    if (*endptr != '\0' || offset < 0)
    {
        return session_send_response(session, PROTO_RESP_SYNTAX_ERROR_PARAM,
                                     "Invalid restart offset");
    }

    // Set the restart offset
    if (session_set_restart_offset(session, offset) != 0)
    {
        return session_send_response(session, PROTO_RESP_LOCAL_ERROR,
                                     "Failed to set restart offset");
    }

    char response[PROTO_MAX_RESPONSE_LINE];
    snprintf(response, sizeof(response), "Restart position accepted (%lld)", offset);

    return session_send_response(session, PROTO_RESP_FILE_ACTION_PENDING, response);
}

int cmd_handle_list(cmd_handler_context_t context, const proto_command_t *cmd)
{
    session_t *session = (session_t *)context;

    if (!session->authenticated)
    {
        return session_send_response(session, PROTO_RESP_NOT_LOGGED_IN,
                                     "Please login with USER and PASS");
    }

    // Check if data transfer mode is set (PASV or PORT must be called first)
    if (session->data_mode == SESSION_DATA_MODE_NONE)
    {
        return session_send_response(session, PROTO_RESP_BAD_COMMAND_SEQUENCE,
                                     "Use PASV or PORT first");
    }

    // Get path argument, default to current directory
    const char *path = cmd->has_argument ? cmd->argument : ".";

    // Check path access permission (READ required for listing)
    if (!session_check_path_access(session, path, AUTH_PERM_READ))
    {
        LOG_WARN("User '%s' denied read access to: %s", session->username, path);
        return session_send_response(session, PROTO_RESP_FILE_UNAVAILABLE,
                                     "Permission denied");
    }

    char abs_path[SESSION_MAX_PATH];
    if (session_resolve_path(session, path, abs_path, sizeof(abs_path)) != 0)
    {
        return session_send_response(session, PROTO_RESP_FILE_UNAVAILABLE,
                                     "Invalid path");
    }

    // Check if path exists
    if (!fs_path_exists(abs_path))
    {
        return session_send_response(session, PROTO_RESP_FILE_UNAVAILABLE,
                                     "Path not found");
    }

    int response = -1;
    int data_connection_opened = 0;

    // Use do-while(0) for structured error handling
    do
    {
        // Inform client that transfer is starting
        if (session_send_response(session, PROTO_RESP_FILE_STATUS_OK,
                                  "Opening data connection for directory listing") != 0)
        {
            response = -1;
            break;
        }

        // Open data connection
        if (session_open_data_connection(session, 10000) != 0)
        {
            response = session_send_response(session, PROTO_RESP_CANT_OPEN_DATA,
                                             "Can't open data connection");
            break;
        }
        data_connection_opened = 1;

        // Prepare transfer parameters
        transfer_params_t params;
        memset(&params, 0, sizeof(params));
        params.operation = TRANSFER_OP_SEND_LIST;
        strncpy(params.filepath, abs_path, sizeof(params.filepath) - 1);
        params.offset = 0;
        params.type = session->transfer_type;
        params.lock_acquired = 0; // LIST doesn't need file locks

        // Start async transfer thread
        if (session_start_transfer_thread(session, &params) != 0)
        {
            response = session_send_response(session, PROTO_RESP_LOCAL_ERROR,
                                             "Failed to start transfer");
            break;
        }

        // Transfer started successfully, response will be sent by transfer thread
        // Data connection will be closed by the transfer thread
        response = 0;
        data_connection_opened = 0; // Don't close data connection here
    } while (0);

    if (data_connection_opened)
    {
        session_close_data_connection(session);
    }

    return response;
}

int cmd_handle_nlst(cmd_handler_context_t context, const proto_command_t *cmd)
{
    session_t *session = (session_t *)context;

    if (!session->authenticated)
    {
        return session_send_response(session, PROTO_RESP_NOT_LOGGED_IN,
                                     "Please login with USER and PASS");
    }

    // Check if data transfer mode is set (PASV or PORT must be called first)
    if (session->data_mode == SESSION_DATA_MODE_NONE)
    {
        return session_send_response(session, PROTO_RESP_BAD_COMMAND_SEQUENCE,
                                     "Use PASV or PORT first");
    }

    // Get path argument, default to current directory
    const char *path = cmd->has_argument ? cmd->argument : ".";

    // Check path access permission (READ required for listing)
    if (!session_check_path_access(session, path, AUTH_PERM_READ))
    {
        LOG_WARN("User '%s' denied read access to: %s", session->username, path);
        return session_send_response(session, PROTO_RESP_FILE_UNAVAILABLE,
                                     "Permission denied");
    }

    char abs_path[SESSION_MAX_PATH];
    if (session_resolve_path(session, path, abs_path, sizeof(abs_path)) != 0)
    {
        return session_send_response(session, PROTO_RESP_FILE_UNAVAILABLE,
                                     "Invalid path");
    }

    // Check if path exists
    if (!fs_path_exists(abs_path))
    {
        return session_send_response(session, PROTO_RESP_FILE_UNAVAILABLE,
                                     "Path not found");
    }

    int response = -1;
    int data_connection_opened = 0;

    // Use do-while(0) for structured error handling
    do
    {
        // Inform client that transfer is starting
        if (session_send_response(session, PROTO_RESP_FILE_STATUS_OK,
                                  "Opening data connection for name list") != 0)
        {
            response = -1;
            break;
        }

        // Open data connection
        if (session_open_data_connection(session, 10000) != 0)
        {
            response = session_send_response(session, PROTO_RESP_CANT_OPEN_DATA,
                                             "Can't open data connection");
            break;
        }
        data_connection_opened = 1;

        // Prepare transfer parameters
        transfer_params_t params;
        memset(&params, 0, sizeof(params));
        params.operation = TRANSFER_OP_SEND_NLST;
        strncpy(params.filepath, abs_path, sizeof(params.filepath) - 1);
        params.offset = 0;
        params.type = session->transfer_type;
        params.lock_acquired = 0; // NLST doesn't need file locks

        // Start async transfer thread
        if (session_start_transfer_thread(session, &params) != 0)
        {
            response = session_send_response(session, PROTO_RESP_LOCAL_ERROR,
                                             "Failed to start transfer");
            break;
        }

        // Transfer started successfully, response will be sent by transfer thread
        // Data connection will be closed by the transfer thread
        response = 0;
        data_connection_opened = 0; // Don't close data connection here
    } while (0);

    if (data_connection_opened)
    {
        session_close_data_connection(session);
    }

    return response;
}

int cmd_handle_pwd(cmd_handler_context_t context, const proto_command_t *cmd)
{
    session_t *session = (session_t *)context;

    if (!session->authenticated)
    {
        return session_send_response(session, PROTO_RESP_NOT_LOGGED_IN,
                                     "Please login with USER and PASS");
    }

    if (cmd->has_argument)
    {
        return session_send_response(session, PROTO_RESP_SYNTAX_ERROR_PARAM,
                                     "PWD does not take parameters");
    }

    char cwd[SESSION_MAX_PATH - 32]; // Extra space for quotes and message to suppress overflow warnings
    if (session_get_current_directory(session, cwd, sizeof(cwd)) != 0)
    {
        return session_send_response(session, PROTO_RESP_LOCAL_ERROR,
                                     "Failed to get current directory");
    }

    char response[PROTO_MAX_RESPONSE_LINE];
    snprintf(response, sizeof(response), "\"%s\" is current directory", cwd);

    return session_send_response(session, PROTO_RESP_PATH_CREATED, response);
}

int cmd_handle_mkd(cmd_handler_context_t context, const proto_command_t *cmd)
{
    session_t *session = (session_t *)context;

    if (!session->authenticated)
    {
        return session_send_response(session, PROTO_RESP_NOT_LOGGED_IN,
                                     "Please login with USER and PASS");
    }

    if (!cmd->has_argument)
    {
        return session_send_response(session, PROTO_RESP_SYNTAX_ERROR_PARAM,
                                     "Syntax error in parameters");
    }

    // Check path access permission (MKDIR required for creating directories)
    if (!session_check_path_access(session, cmd->argument, AUTH_PERM_MKDIR))
    {
        LOG_WARN("User '%s' denied mkdir access to: %s", session->username, cmd->argument);
        return session_send_response(session, PROTO_RESP_FILE_UNAVAILABLE,
                                     "Permission denied");
    }

    char abs_path[SESSION_MAX_PATH];
    if (session_resolve_path(session, cmd->argument, abs_path, sizeof(abs_path)) != 0)
    {
        return session_send_response(session, PROTO_RESP_FILE_UNAVAILABLE,
                                     "Invalid path");
    }

    // Check if directory already exists
    if (fs_path_exists(abs_path))
    {
        return session_send_response(session, PROTO_RESP_FILE_UNAVAILABLE,
                                     "Directory already exists");
    }

    // Create the directory
    if (fs_create_directory(abs_path) != 0)
    {
        return session_send_response(session, PROTO_RESP_FILE_UNAVAILABLE,
                                     "Failed to create directory");
    }

    char response[PROTO_MAX_RESPONSE_LINE];
    snprintf(response, sizeof(response), "\"%s\" directory created", cmd->argument);

    return session_send_response(session, PROTO_RESP_PATH_CREATED, response);
}

int cmd_handle_rmd(cmd_handler_context_t context, const proto_command_t *cmd)
{
    session_t *session = (session_t *)context;

    if (!session->authenticated)
    {
        return session_send_response(session, PROTO_RESP_NOT_LOGGED_IN,
                                     "Please login with USER and PASS");
    }

    if (!cmd->has_argument)
    {
        return session_send_response(session, PROTO_RESP_SYNTAX_ERROR_PARAM,
                                     "Syntax error in parameters");
    }

    // Check path access permission (RMDIR required for removing directories)
    if (!session_check_path_access(session, cmd->argument, AUTH_PERM_RMDIR))
    {
        LOG_WARN("User '%s' denied rmdir access to: %s", session->username, cmd->argument);
        return session_send_response(session, PROTO_RESP_FILE_UNAVAILABLE,
                                     "Permission denied");
    }

    char abs_path[SESSION_MAX_PATH];
    if (session_resolve_path(session, cmd->argument, abs_path, sizeof(abs_path)) != 0)
    {
        return session_send_response(session, PROTO_RESP_FILE_UNAVAILABLE,
                                     "Invalid path");
    }

    // Check if path exists and is a directory
    if (!fs_path_exists(abs_path))
    {
        return session_send_response(session, PROTO_RESP_FILE_UNAVAILABLE,
                                     "Directory not found");
    }

    if (!fs_is_directory(abs_path))
    {
        return session_send_response(session, PROTO_RESP_FILE_UNAVAILABLE,
                                     "Path is not a directory");
    }

    // Remove the directory
    if (fs_delete_directory(abs_path, 0) != 0)
    {
        return session_send_response(session, PROTO_RESP_FILE_UNAVAILABLE,
                                     "Failed to remove directory");
    }

    return session_send_response(session, PROTO_RESP_FILE_ACTION_OK,
                                 "Directory removed");
}

int cmd_handle_rnfr(cmd_handler_context_t context, const proto_command_t *cmd)
{
    session_t *session = (session_t *)context;

    if (!session->authenticated)
    {
        return session_send_response(session, PROTO_RESP_NOT_LOGGED_IN,
                                     "Please login with USER and PASS");
    }

    if (!cmd->has_argument)
    {
        return session_send_response(session, PROTO_RESP_SYNTAX_ERROR_PARAM,
                                     "Syntax error in parameters");
    }

    // Check rename permission on source
    if (!session_check_path_access(session, cmd->argument, AUTH_PERM_RENAME))
    {
        LOG_WARN("User '%s' denied rename access to: %s", session->username, cmd->argument);
        return session_send_response(session, PROTO_RESP_FILE_UNAVAILABLE,
                                     "Permission denied");
    }

    // Resolve and check if file/directory exists
    char abs_path[SESSION_MAX_PATH];
    if (session_resolve_path(session, cmd->argument, abs_path, sizeof(abs_path)) != 0)
    {
        return session_send_response(session, PROTO_RESP_FILE_UNAVAILABLE,
                                     "Invalid path");
    }

    if (!fs_path_exists(abs_path))
    {
        return session_send_response(session, PROTO_RESP_FILE_UNAVAILABLE,
                                     "File or directory not found");
    }

    // Try to acquire exclusive lock without blocking to check if file is busy
    // We acquire and immediately release it just to check availability
    if (file_lock_try_acquire_exclusive(abs_path) != 0)
    {
        return session_send_response(session, PROTO_RESP_FILE_ACTION_ABORTED,
                                     "File is busy, try again later");
    }
    file_lock_release_exclusive(abs_path);

    // Store the source path for rename
    if (session_set_rename_from(session, abs_path) != 0)
    {
        return session_send_response(session, PROTO_RESP_LOCAL_ERROR,
                                     "Failed to store rename source");
    }

    return session_send_response(session, PROTO_RESP_FILE_ACTION_PENDING,
                                 "File exists, ready for destination name");
}

int cmd_handle_rnto(cmd_handler_context_t context, const proto_command_t *cmd)
{
    session_t *session = (session_t *)context;

    if (!session->authenticated)
    {
        return session_send_response(session, PROTO_RESP_NOT_LOGGED_IN,
                                     "Please login with USER and PASS");
    }

    if (!cmd->has_argument)
    {
        return session_send_response(session, PROTO_RESP_SYNTAX_ERROR_PARAM,
                                     "Syntax error in parameters");
    }

    // Get the source path from RNFR
    char from_path[SESSION_MAX_PATH];
    if (session_get_rename_from(session, from_path, sizeof(from_path)) != 0)
    {
        return session_send_response(session, PROTO_RESP_BAD_COMMAND_SEQUENCE,
                                     "Bad sequence of commands (use RNFR first)");
    }

    // Check rename permission for destination
    // We need to check the destination directory for write access
    if (!session_check_path_access(session, cmd->argument, AUTH_PERM_RENAME))
    {
        LOG_WARN("User '%s' denied rename access to destination: %s", session->username, cmd->argument);
        return session_send_response(session, PROTO_RESP_FILE_UNAVAILABLE,
                                     "Permission denied for destination");
    }

    // Resolve destination path
    char to_path[SESSION_MAX_PATH];
    if (session_resolve_path(session, cmd->argument, to_path, sizeof(to_path)) != 0)
    {
        return session_send_response(session, PROTO_RESP_FILE_UNAVAILABLE,
                                     "Invalid destination path");
    }

    // Check if destination already exists
    if (fs_path_exists(to_path))
    {
        return session_send_response(session, PROTO_RESP_FILE_UNAVAILABLE,
                                     "Destination already exists");
    }

    // Get parent directory of destination
    char dest_parent[SESSION_MAX_PATH];
    if (fs_get_parent_directory(to_path, dest_parent, sizeof(dest_parent)) == 0)
    {
        // Check if parent directory exists
        if (!fs_path_exists(dest_parent))
        {
            return session_send_response(session, PROTO_RESP_FILE_UNAVAILABLE,
                                         "Destination directory does not exist");
        }
        if (!fs_is_directory(dest_parent))
        {
            return session_send_response(session, PROTO_RESP_FILE_UNAVAILABLE,
                                         "Invalid destination path");
        }
    }

    int response = -1;
    int lock_acquired = 0;

    // Use do-while(0) for structured error handling
    do
    {
        // Try to acquire exclusive lock on source file without blocking
        // If file is being read/written, fail immediately so client can retry
        if (file_lock_try_acquire_exclusive(from_path) != 0)
        {
            response = session_send_response(session, PROTO_RESP_FILE_ACTION_ABORTED,
                                             "File is busy, try again later");
            break;
        }
        lock_acquired = 1;

        // Revalidate source file exists while holding lock
        if (!fs_path_exists(from_path))
        {
            response = session_send_response(session, PROTO_RESP_FILE_UNAVAILABLE,
                                             "Source file no longer exists");
            break;
        }

        // Check again if destination exists
        if (fs_path_exists(to_path))
        {
            response = session_send_response(session, PROTO_RESP_FILE_UNAVAILABLE,
                                             "Destination already exists");
            break;
        }

        // Perform the rename
        if (fs_rename(from_path, to_path) != 0)
        {
            response = session_send_response(session, PROTO_RESP_FILE_UNAVAILABLE,
                                             "Rename failed");
            break;
        }

        LOG_INFO("User '%s' renamed '%s' to '%s'", session->username, from_path, to_path);
        response = session_send_response(session, PROTO_RESP_FILE_ACTION_OK,
                                         "Rename successful");
    } while (0);

    if (lock_acquired)
    {
        // Lock is on the old path (from_path), which may no longer exist after rename
        file_lock_release_exclusive(from_path);
    }

    // Clear the stored RNFR path regardless of success/failure
    session_clear_rename_state(session);

    return response;
}

int cmd_handle_dele(cmd_handler_context_t context, const proto_command_t *cmd)
{
    session_t *session = (session_t *)context;

    if (!session->authenticated)
    {
        return session_send_response(session, PROTO_RESP_NOT_LOGGED_IN,
                                     "Please login with USER and PASS");
    }

    if (!cmd->has_argument)
    {
        return session_send_response(session, PROTO_RESP_SYNTAX_ERROR_PARAM,
                                     "Syntax error in parameters");
    }

    // Check delete permission
    if (!session_check_path_access(session, cmd->argument, AUTH_PERM_DELETE))
    {
        LOG_WARN("User '%s' denied delete access to: %s", session->username, cmd->argument);
        return session_send_response(session, PROTO_RESP_FILE_UNAVAILABLE,
                                     "Permission denied");
    }

    char abs_path[SESSION_MAX_PATH];
    if (session_resolve_path(session, cmd->argument, abs_path, sizeof(abs_path)) != 0)
    {
        return session_send_response(session, PROTO_RESP_FILE_UNAVAILABLE,
                                     "Invalid path");
    }

    // Check if path exists
    if (!fs_path_exists(abs_path))
    {
        return session_send_response(session, PROTO_RESP_FILE_UNAVAILABLE,
                                     "File not found");
    }

    // Check if it's a directory (use RMD for directories)
    if (fs_is_directory(abs_path))
    {
        return session_send_response(session, PROTO_RESP_FILE_UNAVAILABLE,
                                     "Cannot delete directory with DELE (use RMD)");
    }

    int response = -1;
    int lock_acquired = 0;

    // Use do-while(0) for structured error handling
    do
    {
        // Try to acquire exclusive lock without blocking
        // If file is being read/written, fail immediately so client can retry
        if (file_lock_try_acquire_exclusive(abs_path) != 0)
        {
            response = session_send_response(session, PROTO_RESP_FILE_ACTION_ABORTED,
                                             "File is busy, try again later");
            break;
        }
        lock_acquired = 1;

        // Revalidate file state while holding the lock
        if (!fs_path_exists(abs_path))
        {
            response = session_send_response(session, PROTO_RESP_FILE_UNAVAILABLE,
                                             "File no longer exists");
            break;
        }

        if (fs_is_directory(abs_path))
        {
            response = session_send_response(session, PROTO_RESP_FILE_UNAVAILABLE,
                                             "Cannot delete directory with DELE (use RMD)");
            break;
        }

        // Delete the file
        if (fs_delete_file(abs_path) != 0)
        {
            response = session_send_response(session, PROTO_RESP_FILE_UNAVAILABLE,
                                             "Failed to delete file");
            break;
        }

        LOG_INFO("User '%s' deleted file: %s", session->username, abs_path);
        response = session_send_response(session, PROTO_RESP_FILE_ACTION_OK,
                                         "File deleted");
    } while (0);

    if (lock_acquired)
    {
        file_lock_release_exclusive(abs_path);
    }

    return response;
}

int cmd_handle_abor(cmd_handler_context_t context, const proto_command_t *cmd)
{
    (void)cmd; // Unused parameter

    session_t *session = (session_t *)context;
    if (!session)
    {
        return -1;
    }

    transfer_thread_state_t thread_state = session_get_transfer_thread_state(session);
    int transfer_thread_active = (thread_state == TRANSFER_THREAD_RUNNING);

    if (!transfer_thread_active)
    {
        // No transfer was in progress
        // Still close data connection in case it's open
        session_close_data_connection(session);
        return session_send_response(session, PROTO_RESP_DATA_CONN_OPEN_NO_TRANSFER,
                                     "No transfer in progress");
    }

    // Transfer is active - abort it
    session_set_transfer_should_abort(session);

    // Close data connection to unblock any blocking I/O
    session_close_data_connection(session);

    // Send 426 response to indicate abnormal termination
    return session_send_response(session, PROTO_RESP_CONN_CLOSED,
                                 "Data connection closed; transfer aborted");
}

// Informational commands

int cmd_handle_syst(cmd_handler_context_t context, const proto_command_t *cmd)
{
    (void)cmd; // Unused parameter

    session_t *session = (session_t *)context;
    if (!session)
    {
        return -1;
    }

    // Return system type information
    return session_send_response(session, PROTO_RESP_SYSTEM_TYPE,
                                 "UNIX Type: L8");
}

int cmd_handle_noop(cmd_handler_context_t context, const proto_command_t *cmd)
{
    (void)cmd; // Unused parameter

    session_t *session = (session_t *)context;
    if (!session)
    {
        return -1;
    }

    // No operation, just acknowledge
    return session_send_response(session, PROTO_RESP_OK,
                                 "OK");
}

int cmd_handle_size(cmd_handler_context_t context, const proto_command_t *cmd)
{
    session_t *session = (session_t *)context;

    if (!session->authenticated)
    {
        return session_send_response(session, PROTO_RESP_NOT_LOGGED_IN,
                                     "Please login with USER and PASS");
    }

    if (!cmd->has_argument)
    {
        return session_send_response(session, PROTO_RESP_SYNTAX_ERROR_PARAM,
                                     "Syntax error in parameters");
    }

    // Check path access permission (READ required for getting file size)
    if (!session_check_path_access(session, cmd->argument, AUTH_PERM_READ))
    {
        LOG_WARN("User '%s' denied read access to: %s", session->username, cmd->argument);
        return session_send_response(session, PROTO_RESP_FILE_UNAVAILABLE,
                                     "Permission denied");
    }

    char abs_path[SESSION_MAX_PATH];
    if (session_resolve_path(session, cmd->argument, abs_path, sizeof(abs_path)) != 0)
    {
        return session_send_response(session, PROTO_RESP_FILE_UNAVAILABLE,
                                     "Invalid path");
    }

    // Check if file exists and is a regular file (not a directory)
    if (!fs_path_exists(abs_path))
    {
        return session_send_response(session, PROTO_RESP_FILE_UNAVAILABLE,
                                     "File not found");
    }

    if (fs_is_directory(abs_path))
    {
        return session_send_response(session, PROTO_RESP_FILE_UNAVAILABLE,
                                     "Cannot get size of a directory");
    }

    // Check if file is locked and acquire shared lock
    if (file_lock_is_exclusive_locked(abs_path))
    {
        return session_send_response(session, PROTO_RESP_FILE_ACTION_ABORTED,
                                     "File is busy, try again later");
    }

    // Acquire shared lock to ensure file is not being modified
    if (file_lock_acquire_shared(abs_path) != 0)
    {
        return session_send_response(session, PROTO_RESP_FILE_ACTION_ABORTED,
                                     "File is busy, try again later");
    }

    long long file_size = fs_get_file_size(abs_path);
    file_lock_release_shared(abs_path);

    if (file_size < 0)
    {
        return session_send_response(session, PROTO_RESP_FILE_UNAVAILABLE,
                                     "Cannot read file");
    }

    char response[PROTO_MAX_RESPONSE_LINE];
    snprintf(response, sizeof(response), "%lld", file_size);

    return session_send_response(session, PROTO_RESP_FILE_STATUS, response);
}

int cmd_handle_mdtm(cmd_handler_context_t context, const proto_command_t *cmd)
{
    session_t *session = (session_t *)context;

    if (!session->authenticated)
    {
        return session_send_response(session, PROTO_RESP_NOT_LOGGED_IN,
                                     "Please login with USER and PASS");
    }

    if (!cmd->has_argument)
    {
        return session_send_response(session, PROTO_RESP_SYNTAX_ERROR_PARAM,
                                     "Syntax error in parameters");
    }

    // Check path access permission (READ required for getting file modification time)
    if (!session_check_path_access(session, cmd->argument, AUTH_PERM_READ))
    {
        LOG_WARN("User '%s' denied read access to: %s", session->username, cmd->argument);
        return session_send_response(session, PROTO_RESP_FILE_UNAVAILABLE,
                                     "Permission denied");
    }

    char abs_path[SESSION_MAX_PATH];
    if (session_resolve_path(session, cmd->argument, abs_path, sizeof(abs_path)) != 0)
    {
        return session_send_response(session, PROTO_RESP_FILE_UNAVAILABLE,
                                     "Invalid path");
    }

    // Check if file exists and is a regular file (not a directory)
    if (!fs_path_exists(abs_path))
    {
        return session_send_response(session, PROTO_RESP_FILE_UNAVAILABLE,
                                     "File not found");
    }

    if (fs_is_directory(abs_path))
    {
        return session_send_response(session, PROTO_RESP_FILE_UNAVAILABLE,
                                     "Cannot get modification time of a directory");
    }

    // Check if file is locked and acquire shared lock
    if (file_lock_is_exclusive_locked(abs_path))
    {
        return session_send_response(session, PROTO_RESP_FILE_ACTION_ABORTED,
                                     "File is busy, try again later");
    }

    // Acquire shared lock to ensure file is not being modified
    if (file_lock_acquire_shared(abs_path) != 0)
    {
        return session_send_response(session, PROTO_RESP_FILE_ACTION_ABORTED,
                                     "File is busy, try again later");
    }

    time_t mtime = fs_get_file_mtime(abs_path);
    file_lock_release_shared(abs_path);

    if (mtime < 0)
    {
        return session_send_response(session, PROTO_RESP_FILE_UNAVAILABLE,
                                     "Cannot read file");
    }

    // Format time as YYYYMMDDHHMMSS
    struct tm *tm_info = gmtime(&mtime);
    if (!tm_info)
    {
        return session_send_response(session, PROTO_RESP_FILE_UNAVAILABLE,
                                     "Cannot format time");
    }

    char time_str[15]; // YYYYMMDDHHMMSS + null
    strftime(time_str, sizeof(time_str), "%Y%m%d%H%M%S", tm_info);

    char response[PROTO_MAX_RESPONSE_LINE];
    snprintf(response, sizeof(response), "%s", time_str);

    return session_send_response(session, PROTO_RESP_FILE_STATUS, response);
}

int cmd_handle_feat(cmd_handler_context_t context, const proto_command_t *cmd)
{
    (void)cmd; // Unused parameter

    session_t *session = (session_t *)context;
    if (!session)
    {
        return -1;
    }

    // Send multiline response
    if (session_send_response_multiline(session, PROTO_RESP_SYSTEM_STATUS, "Extensions supported:") != 0)
        return -1;

    if (session_send_response_multiline(session, PROTO_RESP_SYSTEM_STATUS, " SIZE") != 0)
        return -1;
    if (session_send_response_multiline(session, PROTO_RESP_SYSTEM_STATUS, " MDTM") != 0)
        return -1;
    if (session_send_response_multiline(session, PROTO_RESP_SYSTEM_STATUS, " REST STREAM") != 0)
        return -1;

    return session_send_response(session, PROTO_RESP_SYSTEM_STATUS, "End");
}
