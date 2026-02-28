/**
 * @file transfer.c
 * @brief FTP data transfer operations implementation
 * @version 0.1
 * @date 2025-11-02
 */
#define _DEFAULT_SOURCE
#include "transfer.h"

#include "session.h"
#include "filesys.h"
#include "filelock.h"
#include "network.h"
#include "logger.h"
#include "utils.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

#ifndef _WIN32
#include <sys/stat.h>
#include <pwd.h>
#include <grp.h>
#endif

// Forward declarations
static int format_list_line(const fs_file_info_t *info,
                            char *buffer,
                            size_t buffer_size);
static transfer_status_t send_listing(session_t *session,
                                      const char *dirpath,
                                      const char *filter_name);

transfer_status_t transfer_send_file(session_t *session, const char *filepath, long long offset)
{
    if (!session || !filepath)
    {
        LOG_ERROR("Invalid parameters for transfer_send_file");
        return TRANSFER_STATUS_INTERNAL_ERROR;
    }

    // Verify data socket is valid
    if (session->data_socket == INVALID_SOCKET_T)
    {
        LOG_ERROR("Data socket is not open for SEND transfer (data_mode=%d, client=%s:%u)",
                  session->data_mode, session->client_ip, session->client_port);
        return TRANSFER_STATUS_CONN_ERROR;
    }

    long long file_size = fs_get_file_size(filepath);
    if (file_size < 0)
    {
        LOG_ERROR("Cannot get file size: %s", filepath);
        return TRANSFER_STATUS_IO_ERROR;
    }

    if (offset > file_size)
    {
        LOG_ERROR("Offset %lld exceeds file size %lld", offset, file_size);
        return TRANSFER_STATUS_IO_ERROR;
    }

    char *buffer = malloc(TRANSFER_BUFFER_SIZE);
    if (!buffer)
    {
        LOG_ERROR("Failed to allocate transfer buffer");
        return TRANSFER_STATUS_INTERNAL_ERROR;
    }

    long long remaining = file_size - offset;
    long long current_offset = offset;
    long long total_sent = 0;
    transfer_status_t status = TRANSFER_STATUS_OK;

    LOG_INFO("Starting file transfer: %s (size: %lld, offset: %lld)",
             filepath, file_size, offset);

    while (remaining > 0)
    {
        // Check if transfer has been aborted before attempting I/O
        if (session_should_abort_transfer(session))
        {
            LOG_INFO("File transfer aborted: %s", filepath);
            status = TRANSFER_STATUS_ABORTED;
            break;
        }

        size_t to_read = (remaining > TRANSFER_BUFFER_SIZE) ? TRANSFER_BUFFER_SIZE : (size_t)remaining;

        long long bytes_read = fs_read_file_chunk(filepath, buffer,
                                                  current_offset, to_read);

        if (bytes_read < 0)
        {
            LOG_ERROR("Failed to read file chunk at offset %lld", current_offset);
            status = TRANSFER_STATUS_IO_ERROR;
            break;
        }

        if (bytes_read == 0)
        {
            LOG_ERROR("Unexpected EOF while reading %s", filepath);
            status = TRANSFER_STATUS_IO_ERROR;
            break;
        }

        if (net_send_all(session->data_socket, buffer, (size_t)bytes_read) != 0)
        {
            // Check if this error is due to abort
            if (session_should_abort_transfer(session))
            {
                LOG_INFO("File transfer aborted by ABOR command (connection closed): %s", filepath);
                status = TRANSFER_STATUS_ABORTED;
            }
            else
            {
                int err = net_get_last_error();
                LOG_ERROR("Failed to send data to client: %s (code=%d)", net_get_error_string(err), err);
                status = TRANSFER_STATUS_CONN_ERROR;
            }
            break;
        }

        current_offset += bytes_read;
        remaining -= bytes_read;
        total_sent += bytes_read;
    }

    free(buffer);

    if (status == TRANSFER_STATUS_OK)
    {
        LOG_INFO("File transfer completed: %lld bytes sent", total_sent);

        // Update session statistics
        pthread_mutex_lock(&session->lock);
        session->bytes_downloaded += total_sent;
        session->files_downloaded++;
        pthread_mutex_unlock(&session->lock);
    }
    else
    {
        LOG_ERROR("File transfer failed after %lld bytes", total_sent);
    }

    return status;
}

transfer_status_t transfer_receive_file(session_t *session, const char *filepath, long long offset)
{
    if (!session || !filepath)
    {
        LOG_ERROR("Invalid parameters for transfer_receive_file");
        return TRANSFER_STATUS_INTERNAL_ERROR;
    }

    // Verify data socket is valid
    if (session->data_socket == INVALID_SOCKET_T)
    {
        LOG_ERROR("Data socket is not open for RECEIVE transfer (data_mode=%d, client=%s:%u)",
                  session->data_mode, session->client_ip, session->client_port);
        return TRANSFER_STATUS_CONN_ERROR;
    }

    char *buffer = malloc(TRANSFER_BUFFER_SIZE);
    if (!buffer)
    {
        LOG_ERROR("Failed to allocate transfer buffer");
        return TRANSFER_STATUS_INTERNAL_ERROR;
    }

    long long total_received = 0;
    transfer_status_t status = TRANSFER_STATUS_OK;

    LOG_INFO("Starting file reception: %s (offset: %lld)", filepath, offset);

    while (1)
    {
        // Check if transfer has been aborted before attempting I/O
        if (session_should_abort_transfer(session))
        {
            LOG_INFO("File reception aborted: %s", filepath);
            status = TRANSFER_STATUS_ABORTED;
            break;
        }

        int bytes_received = net_receive(session->data_socket, buffer,
                                         TRANSFER_BUFFER_SIZE);

        if (bytes_received < 0)
        {
            // Check if this error is due to abort
            if (session_should_abort_transfer(session))
            {
                LOG_INFO("File reception aborted by ABOR command (connection closed): %s", filepath);
                status = TRANSFER_STATUS_ABORTED;
            }
            else
            {
                int err = net_get_last_error();
                LOG_ERROR("Failed to receive data from client: %s (code=%d)", net_get_error_string(err), err);
                status = TRANSFER_STATUS_CONN_ERROR;
            }
            break;
        }

        if (bytes_received == 0)
        {
            // Client closed connection - check if it was an abort
            if (session_should_abort_transfer(session))
            {
                LOG_INFO("File reception aborted by ABOR command (connection closed gracefully): %s", filepath);
                status = TRANSFER_STATUS_ABORTED;
            }
            // Otherwise it's normal completion (end of transmission)
            break;
        }

        long long bytes_written = fs_write_file_chunk(filepath, buffer,
                                                      offset + total_received,
                                                      bytes_received);

        if (bytes_written != bytes_received)
        {
            LOG_ERROR("Failed to write to file at offset %lld",
                      offset + total_received);
            status = TRANSFER_STATUS_IO_ERROR;
            break;
        }

        total_received += bytes_received;
    }

    free(buffer);

    if (status == TRANSFER_STATUS_OK)
    {
        LOG_INFO("File reception completed: %lld bytes received", total_received);

        // Update session statistics
        pthread_mutex_lock(&session->lock);
        session->bytes_uploaded += total_received;
        session->files_uploaded++;
        pthread_mutex_unlock(&session->lock);
    }
    else
    {
        LOG_ERROR("File reception failed after %lld bytes", total_received);
    }

    return status;
}

transfer_status_t transfer_send_file_ascii(session_t *session, const char *filepath, long long offset)
{
    if (!session || !filepath)
    {
        LOG_ERROR("Invalid parameters for transfer_send_file_ascii");
        return TRANSFER_STATUS_INTERNAL_ERROR;
    }

    // Verify data socket is valid
    if (session->data_socket == INVALID_SOCKET_T)
    {
        LOG_ERROR("Data socket is not open for transfer");
        return TRANSFER_STATUS_CONN_ERROR;
    }

    long long file_size = fs_get_file_size(filepath);
    if (file_size < 0)
    {
        LOG_ERROR("Cannot get file size: %s", filepath);
        return TRANSFER_STATUS_IO_ERROR;
    }

    if (offset > file_size)
    {
        LOG_ERROR("Offset %lld exceeds file size %lld", offset, file_size);
        return TRANSFER_STATUS_IO_ERROR;
    }

    char *read_buffer = malloc(TRANSFER_BUFFER_SIZE);
    char *write_buffer = malloc(TRANSFER_BUFFER_SIZE * 2); // Max 2x for CRLF conversion
    if (!read_buffer || !write_buffer)
    {
        LOG_ERROR("Failed to allocate transfer buffers");
        free(read_buffer);
        free(write_buffer);
        return TRANSFER_STATUS_INTERNAL_ERROR;
    }

    long long remaining = file_size - offset;
    long long current_offset = offset;
    long long total_sent = 0;
    transfer_status_t status = TRANSFER_STATUS_OK;

    LOG_INFO("Starting ASCII file transfer: %s (size: %lld, offset: %lld)",
             filepath, file_size, offset);

    while (remaining > 0)
    {
        // Check if transfer has been aborted before attempting I/O
        if (session_should_abort_transfer(session))
        {
            LOG_INFO("ASCII file transfer aborted: %s", filepath);
            status = TRANSFER_STATUS_ABORTED;
            break;
        }

        size_t to_read = (remaining > TRANSFER_BUFFER_SIZE) ? TRANSFER_BUFFER_SIZE : (size_t)remaining;

        long long bytes_read = fs_read_file_chunk(filepath, read_buffer,
                                                  current_offset, to_read);

        if (bytes_read < 0)
        {
            LOG_ERROR("Failed to read file chunk at offset %lld", current_offset);
            status = TRANSFER_STATUS_IO_ERROR;
            break;
        }

        if (bytes_read == 0)
        {
            LOG_ERROR("Unexpected EOF while reading %s in ASCII mode", filepath);
            status = TRANSFER_STATUS_IO_ERROR;
            break;
        }

        long long converted_bytes = lf_to_crlf(read_buffer, bytes_read, write_buffer, TRANSFER_BUFFER_SIZE * 2);
        if (converted_bytes < 0)
        {
            LOG_ERROR("Failed to convert LF to CRLF for sending");
            status = TRANSFER_STATUS_INTERNAL_ERROR;
            break;
        }

        if (net_send_all(session->data_socket, write_buffer, (size_t)converted_bytes) != 0)
        {
            // Check if this error is due to abort
            if (session_should_abort_transfer(session))
            {
                LOG_INFO("ASCII file transfer aborted by ABOR command (connection closed): %s", filepath);
                status = TRANSFER_STATUS_ABORTED;
            }
            else
            {
                int err = net_get_last_error();
                LOG_ERROR("Failed to send data to client in ASCII mode: %s (code=%d)", net_get_error_string(err), err);
                status = TRANSFER_STATUS_CONN_ERROR;
            }
            break;
        }

        current_offset += bytes_read;
        remaining -= bytes_read;
        total_sent += converted_bytes; // total_sent counts bytes sent over network
    }

    free(read_buffer);
    free(write_buffer);

    if (status == TRANSFER_STATUS_OK)
    {
        LOG_INFO("ASCII file transfer completed: %lld bytes sent", total_sent);

        // Update session statistics
        pthread_mutex_lock(&session->lock);
        session->bytes_downloaded += total_sent;
        session->files_downloaded++;
        pthread_mutex_unlock(&session->lock);
    }
    else
    {
        LOG_ERROR("ASCII file transfer failed after %lld bytes", total_sent);
    }

    return status;
}

transfer_status_t transfer_receive_file_ascii(session_t *session, const char *filepath, long long offset)
{
    if (!session || !filepath)
    {
        LOG_ERROR("Invalid parameters for transfer_receive_file_ascii");
        return TRANSFER_STATUS_INTERNAL_ERROR;
    }

    // Verify data socket is valid
    if (session->data_socket == INVALID_SOCKET_T)
    {
        LOG_ERROR("Data socket is not open for transfer");
        return TRANSFER_STATUS_CONN_ERROR;
    }

    char *read_buffer = malloc(TRANSFER_BUFFER_SIZE);
    char *write_buffer = malloc(TRANSFER_BUFFER_SIZE); // Output buffer for converted data
    if (!read_buffer || !write_buffer)
    {
        LOG_ERROR("Failed to allocate transfer buffers");
        free(read_buffer);
        free(write_buffer);
        return TRANSFER_STATUS_INTERNAL_ERROR;
    }

    long long total_received = 0;
    long long total_written = 0;
    transfer_status_t status = TRANSFER_STATUS_OK;

    LOG_INFO("Starting ASCII file reception: %s (offset: %lld)", filepath, offset);

    while (1)
    {
        // Check if transfer has been aborted before attempting I/O
        if (session_should_abort_transfer(session))
        {
            LOG_INFO("ASCII file reception aborted: %s", filepath);
            status = TRANSFER_STATUS_ABORTED;
            break;
        }

        int bytes_received = net_receive(session->data_socket, read_buffer,
                                         TRANSFER_BUFFER_SIZE);

        if (bytes_received < 0)
        {
            // Check if this error is due to abort
            if (session_should_abort_transfer(session))
            {
                LOG_INFO("ASCII file reception aborted by ABOR command (connection closed): %s", filepath);
                status = TRANSFER_STATUS_ABORTED;
            }
            else
            {
                int err = net_get_last_error();
                LOG_ERROR("Failed to receive data from client in ASCII mode: %s (code=%d)", net_get_error_string(err), err);
                status = TRANSFER_STATUS_CONN_ERROR;
            }
            break;
        }

        if (bytes_received == 0)
        {
            // Client closed connection - check if it was an abort
            if (session_should_abort_transfer(session))
            {
                LOG_INFO("ASCII file reception aborted by ABOR command (connection closed gracefully): %s", filepath);
                status = TRANSFER_STATUS_ABORTED;
            }
            // Otherwise it's normal completion (end of transmission)
            break;
        }

        total_received += bytes_received;

        long long bytes_to_write;
        const char *data_to_write;

#ifdef _WIN32
        // On Windows, No conversion needed for received ASCII.
        bytes_to_write = bytes_received;
        data_to_write = read_buffer;
#else
        // On Unix, convert CRLF to LF.
        long long converted_bytes = crlf_to_lf(read_buffer, bytes_received, write_buffer, TRANSFER_BUFFER_SIZE);
        if (converted_bytes < 0)
        {
            LOG_ERROR("Failed to convert CRLF to LF for receiving");
            status = TRANSFER_STATUS_INTERNAL_ERROR;
            break;
        }
        bytes_to_write = converted_bytes;
        data_to_write = write_buffer;
#endif

        long long bytes_written = fs_write_file_chunk(filepath, data_to_write,
                                                      offset + total_written,
                                                      bytes_to_write);

        if (bytes_written != bytes_to_write)
        {
            LOG_ERROR("Failed to write to file at offset %lld in ASCII mode",
                      offset + total_written);
            status = TRANSFER_STATUS_IO_ERROR;
            break;
        }

        total_written += bytes_written;
    }

    free(read_buffer);
    free(write_buffer);

    if (status == TRANSFER_STATUS_OK)
    {
        LOG_INFO("ASCII file reception completed: %lld bytes written", total_written);

        // Update session statistics (use total_received as it reflects actual network bytes)
        pthread_mutex_lock(&session->lock);
        session->bytes_uploaded += total_received;
        session->files_uploaded++;
        pthread_mutex_unlock(&session->lock);
    }
    else
    {
        LOG_ERROR("ASCII file reception failed after %lld bytes", total_written);
    }

    return status;
}

/**
 * @brief Format a single line of file listing (detailed view).
 * @param info Pointer to file information structure.
 * @param buffer Output buffer for the formatted line.
 * @param buffer_size Size of the output buffer.
 * @return 0 on success, -1 on error.
 */
static int format_list_line(const fs_file_info_t *info,
                            char *buffer,
                            size_t buffer_size)
{
    if (!info || !buffer || buffer_size == 0)
    {
        return -1;
    }

    char type_char = '-';
    mode_t mode = info->mode;

    if (info->type == FS_TYPE_DIR)
        type_char = 'd';
    else if (info->type == FS_TYPE_SYMLINK)
        type_char = 'l';
    else
    {
#ifndef _WIN32
        if (S_ISCHR(mode))
            type_char = 'c';
        else if (S_ISBLK(mode))
            type_char = 'b';
        else if (S_ISFIFO(mode))
            type_char = 'p';
        else if (S_ISSOCK(mode))
            type_char = 's';
#endif
    }

    char perms[10];
    perms[0] = (mode & S_IRUSR) ? 'r' : '-';
    perms[1] = (mode & S_IWUSR) ? 'w' : '-';
    perms[2] = (mode & S_IXUSR) ? 'x' : '-';
    perms[3] = (mode & S_IRGRP) ? 'r' : '-';
    perms[4] = (mode & S_IWGRP) ? 'w' : '-';
    perms[5] = (mode & S_IXGRP) ? 'x' : '-';
    perms[6] = (mode & S_IROTH) ? 'r' : '-';
    perms[7] = (mode & S_IWOTH) ? 'w' : '-';
    perms[8] = (mode & S_IXOTH) ? 'x' : '-';
    perms[9] = '\0';

    char user_name[32] = "ftp";
    char group_name[32] = "ftp";

#ifndef _WIN32
    struct passwd *pw = getpwuid(info->uid);
    if (pw)
        snprintf(user_name, sizeof(user_name), "%s", pw->pw_name);
    else
        snprintf(user_name, sizeof(user_name), "%u", info->uid);

    struct group *gr = getgrgid(info->gid);
    if (gr)
        snprintf(group_name, sizeof(group_name), "%s", gr->gr_name);
    else
        snprintf(group_name, sizeof(group_name), "%u", info->gid);
#endif

    struct tm *tm_info = localtime(&info->last_modified);
    if (!tm_info)
    {
        return -1;
    }

    char date_str[32];
    strftime(date_str, sizeof(date_str), "%b %d %H:%M", tm_info);

    int written;
    if (info->type == FS_TYPE_SYMLINK && info->link_target[0] != '\0')
    {
        written = snprintf(buffer, buffer_size,
                           "%c%s %3u %-8s %-8s %12lld %s %s -> %s\r\n",
                           type_char, perms, (unsigned int)info->nlink,
                           user_name, group_name, info->size,
                           date_str, info->name, info->link_target);
    }
    else
    {
        written = snprintf(buffer, buffer_size,
                           "%c%s %3u %-8s %-8s %12lld %s %s\r\n",
                           type_char, perms, (unsigned int)info->nlink,
                           user_name, group_name, info->size,
                           date_str, info->name);
    }

    // Success if written fits in buffer
    return (written >= 0 && written < (int)buffer_size) ? 0 : -1;
}

/**
 * @brief Send a directory listing to the client.
 * @param session Pointer to the session structure.
 * @param dirpath Path to the directory to list.
 * @param filter_name Optional filter for a specific file name.
 * @return Transfer status code.
 */
static transfer_status_t send_listing(session_t *session,
                                      const char *dirpath,
                                      const char *filter_name)
{
    fs_file_info_t file_list[1024];
    int count = fs_list_directory(dirpath, file_list, 1024);

    if (count < 0)
    {
        LOG_ERROR("Failed to list directory: %s", dirpath);
        return TRANSFER_STATUS_IO_ERROR;
    }

    int entries_sent = 0;
    char line_buffer[1024];

    for (int i = 0; i < count; i++)
    {
        // Check if transfer has been aborted
        if (session_should_abort_transfer(session))
        {
            LOG_INFO("Directory listing aborted: %s", dirpath);
            return TRANSFER_STATUS_ABORTED;
        }

        // If filtering by name, skip non-matching entries
        if (filter_name && strcmp(file_list[i].name, filter_name) != 0)
        {
            continue;
        }

        if (format_list_line(&file_list[i], line_buffer, sizeof(line_buffer)) != 0)
        {
            LOG_ERROR("Failed to format listing line for %s", file_list[i].name);
            return TRANSFER_STATUS_INTERNAL_ERROR;
        }

        if (net_send_all(session->data_socket, line_buffer, strlen(line_buffer)) != 0)
        {
            if (session_should_abort_transfer(session))
            {
                LOG_INFO("Directory listing aborted by ABOR command (connection closed): %s", dirpath);
                return TRANSFER_STATUS_ABORTED;
            }
            else
            {
                int err = net_get_last_error();
                LOG_ERROR("Failed to send listing line: %s (code=%d)", net_get_error_string(err), err);
                return TRANSFER_STATUS_CONN_ERROR;
            }
        }

        entries_sent++;

        // If filtering for a specific name, we can stop after sending it
        if (filter_name)
        {
            break;
        }
    }

    if (filter_name && entries_sent == 0)
    {
        LOG_DEBUG("Entry '%s' not found in %s", filter_name, dirpath);
        return TRANSFER_STATUS_IO_ERROR;
    }

    LOG_INFO("Sent directory listing: %d entries", entries_sent);
    return TRANSFER_STATUS_OK;
}

transfer_status_t transfer_send_list(session_t *session, const char *path)
{
    if (!session || !path)
    {
        LOG_ERROR("Invalid parameters for transfer_send_list");
        return TRANSFER_STATUS_INTERNAL_ERROR;
    }

    // Verify data socket is valid
    if (session->data_socket == INVALID_SOCKET_T)
    {
        LOG_ERROR("Data socket is not open for transfer");
        return TRANSFER_STATUS_CONN_ERROR;
    }

    if (fs_is_directory(path))
    {
        return send_listing(session, path, NULL);
    }

    if (!fs_path_exists(path))
    {
        LOG_ERROR("LIST path does not exist: %s", path);
        return TRANSFER_STATUS_IO_ERROR;
    }

    // It's a file, extract parent directory and ls that file only
    char filename[SESSION_MAX_PATH];
    const char *name_part = fs_extract_filename(path);
    if (!name_part || name_part[0] == '\0')
    {
        LOG_ERROR("Failed to extract filename for LIST: %s", path);
        return TRANSFER_STATUS_INTERNAL_ERROR;
    }

    strncpy(filename, name_part, sizeof(filename) - 1);
    filename[sizeof(filename) - 1] = '\0';

    char parent_dir[SESSION_MAX_PATH];
    if (fs_get_parent_directory(path, parent_dir, sizeof(parent_dir)) != 0)
    {
        LOG_ERROR("Failed to determine parent directory for LIST: %s", path);
        return TRANSFER_STATUS_INTERNAL_ERROR;
    }

    return send_listing(session, parent_dir, filename);
}

transfer_status_t transfer_send_nlst(session_t *session, const char *dirpath)
{
    if (!session || !dirpath)
    {
        LOG_ERROR("Invalid parameters for transfer_send_nlst");
        return TRANSFER_STATUS_INTERNAL_ERROR;
    }

    fs_file_info_t file_list[1024];
    int count = fs_list_directory(dirpath, file_list, 1024);

    if (count < 0)
    {
        LOG_ERROR("Failed to list directory: %s", dirpath);
        return TRANSFER_STATUS_IO_ERROR;
    }

    char line_buffer[512];

    for (int i = 0; i < count; i++)
    {
        // Check if transfer should be aborted
        if (session_should_abort_transfer(session))
        {
            LOG_INFO("Name list transfer aborted: %s", dirpath);
            return TRANSFER_STATUS_ABORTED;
        }

        // NLST format: just filename with CRLF
        snprintf(line_buffer, sizeof(line_buffer), "%s\r\n", file_list[i].name);

        if (net_send_all(session->data_socket, line_buffer, strlen(line_buffer)) != 0)
        {
            if (session_should_abort_transfer(session))
            {
                LOG_INFO("Name list transfer aborted by ABOR command (connection closed): %s", dirpath);
                return TRANSFER_STATUS_ABORTED;
            }
            else
            {
                int err = net_get_last_error();
                LOG_ERROR("Failed to send name list line: %s (code=%d)", net_get_error_string(err), err);
                return TRANSFER_STATUS_CONN_ERROR;
            }
        }
    }

    LOG_INFO("Sent name list: %d entries", count);
    return TRANSFER_STATUS_OK;
}

/**
 * @brief Transfer thread function for async file transfers
 *
 * This function runs in a separate thread to perform file transfers
 * without blocking the main command processing thread.
 *
 * @param arg Pointer to session_t
 * @return NULL
 */
void *transfer_thread_func(void *arg)
{
    session_t *session = (session_t *)arg;
    if (!session)
    {
        return NULL;
    }

    // Set thread state to running
    session_set_transfer_thread_state(session, TRANSFER_THREAD_RUNNING);

    // Set transfer in progress flag
    session_set_transfer_in_progress(session);

    transfer_status_t result;
    transfer_params_t *params = (transfer_params_t *)session->transfer_params;

    LOG_INFO("Session from %s, transfer thread started: operation=%d, path=%s, offset=%lld",
             session->client_ip, params->operation, params->filepath, params->offset);

    // Execute transfer based on operation type
    switch (params->operation)
    {
    case TRANSFER_OP_SEND_FILE:
        // Download (RETR)
        if (params->type == PROTO_TYPE_ASCII)
        {
            result = transfer_send_file_ascii(session, params->filepath, params->offset);
        }
        else
        {
            result = transfer_send_file(session, params->filepath, params->offset);
        }
        break;

    case TRANSFER_OP_RECV_FILE:
        // Upload (STOR / APPE)
        if (params->type == PROTO_TYPE_ASCII)
        {
            result = transfer_receive_file_ascii(session, params->filepath, params->offset);
        }
        else
        {
            result = transfer_receive_file(session, params->filepath, params->offset);
        }
        break;

    case TRANSFER_OP_SEND_LIST:
        // Directory listing (LIST)
        result = transfer_send_list(session, params->filepath);
        break;

    case TRANSFER_OP_SEND_NLST:
        // Name listing (NLST)
        result = transfer_send_nlst(session, params->filepath);
        break;

    default:
        LOG_ERROR("Unknown transfer operation: %d", params->operation);
        result = TRANSFER_STATUS_INTERNAL_ERROR;
        break;
    }

    // Close data connection
    session_close_data_connection(session);

    // Send completion response based on result
    switch (result)
    {
    case TRANSFER_STATUS_OK:
        session_send_response(session, PROTO_RESP_CLOSING_DATA, "Transfer complete");
        break;

    case TRANSFER_STATUS_ABORTED:
        // Send the 226 response for ABOR command sequence
        session_send_response(session, PROTO_RESP_CLOSING_DATA, "ABOR command successful");
        // Clear abort flag after sending response
        session_clear_transfer_should_abort(session);
        break;

    case TRANSFER_STATUS_CONN_ERROR:
        if (!session->should_quit)
        {
            session_send_response(session, PROTO_RESP_CONN_CLOSED, "Data connection closed; transfer aborted");
        }
        break;

    case TRANSFER_STATUS_IO_ERROR:
        session_send_response(session, PROTO_RESP_LOCAL_ERROR, "Failed to read/write file");
        break;

    case TRANSFER_STATUS_INTERNAL_ERROR:
        session_send_response(session, PROTO_RESP_LOCAL_ERROR, "Internal server error during transfer");
        break;

    default:
        break;
    }

    // Store result
    session->transfer_result = result;

    // Clear transfer flags
    session_clear_transfer_in_progress(session);

    // Release file lock if it was acquired
    if (params->lock_acquired)
    {
        // Determine lock type based on operation
        if (params->operation == TRANSFER_OP_RECV_FILE)
        {
            file_lock_release_exclusive(params->filepath);
        }
        else if (params->operation == TRANSFER_OP_SEND_FILE)
        {
            file_lock_release_shared(params->filepath);
        }
        // LIST/NLST operations don't acquire locks
        params->lock_acquired = 0;
        LOG_DEBUG("Released file lock for %s", params->filepath);
    }

    // Free transfer parameters
    if (session->transfer_params)
    {
        free(session->transfer_params);
        session->transfer_params = NULL;
    }

    // Set final thread state
    if (result == TRANSFER_STATUS_ABORTED)
    {
        session_set_transfer_thread_state(session, TRANSFER_THREAD_ABORTED);
    }
    else
    {
        session_set_transfer_thread_state(session, TRANSFER_THREAD_COMPLETING);
    }

    // Reset to idle after a short delay to allow any pending operations to complete
    session_set_transfer_thread_state(session, TRANSFER_THREAD_IDLE);

    LOG_DEBUG("Session from %s, transfer thread exiting", session->client_ip);

    return NULL;
}
