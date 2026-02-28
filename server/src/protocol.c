/**
 * @file protocol.c
 * @brief FTP protocol parsing and response formatting implementation
 * @version 0.1
 * @date 2025-10-30
 *
 */
#include "protocol.h"
#include "utils.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

int proto_parse_command(const char *line, proto_command_t *cmd)
{
    if (!line || !cmd)
        return -1;

    // Initialize command structure
    memset(cmd, 0, sizeof(proto_command_t));

    // Copy line to temporary buffer for processing
    char buffer[PROTO_MAX_CMD_ARG + PROTO_MAX_CMD_NAME + 10];
    size_t line_len = strlen(line);

    if (line_len >= sizeof(buffer))
        return -1;

    strncpy(buffer, line, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    // Filter Telnet IAC sequences
    char filtered_buffer[PROTO_MAX_CMD_ARG + PROTO_MAX_CMD_NAME + 10];
    size_t write_pos = 0;
    for (size_t i = 0; i < line_len && write_pos < sizeof(filtered_buffer) - 1; i++)
    {
        unsigned char c = (unsigned char)buffer[i];
        if (c == 0xFF) {  // IAC (Interpret As Command)
            if (i + 1 < line_len)
            {
                unsigned char next = (unsigned char)buffer[i + 1];
                if (next == 0xFF)
                {
                    // Escaped IAC: IAC IAC → 0xFF
                    filtered_buffer[write_pos++] = 0xFF;
                    i++;  // Skip the second IAC
                }
                else if (next >= 0xF0)
                {
                    // Telnet command (IP=0xF4, BRK=0xF3, DM=0xF2, NOP=0xF1, etc.)
                    i++;  // Skip the command byte
                }
                else if (next >= 0xFB && next <= 0xFE)
                {
                    // Negotiation command (WILL=0xFB, WONT=0xFC, DO=0xFD, DONT=0xFE)
                    if (i + 2 < line_len)
                    {
                        i += 2;  // Skip command and option bytes
                    }
                    else
                    {
                        i++;  // Skip command byte if incomplete
                    }
                }
                else
                {
                    // Unknown IAC sequence, skip IAC and next byte
                    if (i + 1 < line_len)
                    {
                        i += 2;
                    }
                    else
                    {
                        i++;
                    }
                }
            }
            // If no next byte, skip this IAC
        }
        else if (c == 0xF4) {  // IP (Interrupt Process) - may appear without IAC in urgent data
            // Skip IP byte
        }
        else
        {
            filtered_buffer[write_pos++] = c;
        }
    }
    filtered_buffer[write_pos] = '\0';

    // Use filtered buffer for further processing
    strcpy(buffer, filtered_buffer);

    // Remove CRLF if present
    char *crlf = strstr(buffer, "\r\n");
    if (crlf)
        *crlf = '\0';

    // Trim whitespace
    trim_whitespace(buffer);

    if (strlen(buffer) == 0)
        return -1;

    // Find space separator
    char *space = strchr(buffer, ' ');

    if (space)
    {
        // Command has an argument
        *space = '\0';
        char *arg = space + 1;

        // Copy command name
        strncpy(cmd->command, buffer, PROTO_MAX_CMD_NAME - 1);
        cmd->command[PROTO_MAX_CMD_NAME - 1] = '\0';

        // Trim and copy argument
        trim_whitespace(arg);
        strncpy(cmd->argument, arg, PROTO_MAX_CMD_ARG - 1);
        cmd->argument[PROTO_MAX_CMD_ARG - 1] = '\0';

        cmd->has_argument = (strlen(cmd->argument) > 0) ? 1 : 0;
    }
    else
    {
        // Command has no argument
        strncpy(cmd->command, buffer, PROTO_MAX_CMD_NAME - 1);
        cmd->command[PROTO_MAX_CMD_NAME - 1] = '\0';
        cmd->has_argument = 0;
    }

    // Convert command to uppercase
    to_uppercase(cmd->command);

    return 0;
}

int proto_format_response(char *buffer, size_t buffer_size, int code, const char *message)
{
    if (!buffer || buffer_size == 0 || !message)
        return -1;

    if (code < 100 || code > 599)
        return -1;

    int written = snprintf(buffer, buffer_size, "%d %s\r\n", code, message);

    if (written < 0 || (size_t)written >= buffer_size)
        return -1;

    return written;
}

int proto_format_response_multiline(char *buffer, size_t buffer_size, int code, const char *message)
{
    if (!buffer || buffer_size == 0 || !message)
        return -1;

    if (code < 100 || code > 599)
        return -1;

    int written = snprintf(buffer, buffer_size, "%d-%s\r\n", code, message);

    if (written < 0 || (size_t)written >= buffer_size)
        return -1;

    return written;
}

int proto_parse_port(const char *argument, proto_port_params_t *params)
{
    if (!argument || !params)
        return -1;

    unsigned int h1, h2, h3, h4, p1, p2;

    // Parse PORT format: h1,h2,h3,h4,p1,p2
    int parsed = sscanf(argument, "%u,%u,%u,%u,%u,%u", &h1, &h2, &h3, &h4, &p1, &p2);

    if (parsed != 6)
        return -1;

    // Validate values are in byte range
    if (h1 > 255 || h2 > 255 || h3 > 255 || h4 > 255 || p1 > 255 || p2 > 255)
        return -1;

    params->h1 = (uint8_t)h1;
    params->h2 = (uint8_t)h2;
    params->h3 = (uint8_t)h3;
    params->h4 = (uint8_t)h4;
    params->p1 = (uint8_t)p1;
    params->p2 = (uint8_t)p2;

    return 0;
}

int proto_format_pasv_response(char *buffer, size_t buffer_size, const proto_pasv_params_t *params)
{
    if (!buffer || buffer_size == 0 || !params)
        return -1;

    int written = snprintf(buffer, buffer_size,
                           "227 Entering Passive Mode (%u,%u,%u,%u,%u,%u)\r\n",
                           params->h1, params->h2, params->h3, params->h4,
                           params->p1, params->p2);

    if (written < 0 || (size_t)written >= buffer_size)
        return -1;

    return written;
}

int proto_parse_type(const char *argument, proto_transfer_type_t *type)
{
    if (!argument || !type)
        return -1;

    char arg_upper[16];
    strncpy(arg_upper, argument, sizeof(arg_upper) - 1);
    arg_upper[sizeof(arg_upper) - 1] = '\0';
    to_uppercase(arg_upper);
    trim_whitespace(arg_upper);

    if (strcmp(arg_upper, "A") == 0 || strcmp(arg_upper, "A N") == 0)
    {
        *type = PROTO_TYPE_ASCII;
        return 0;
    }
    else if (strcmp(arg_upper, "I") == 0)
    {
        *type = PROTO_TYPE_BINARY;
        return 0;
    }
    else if (strcmp(arg_upper, "E") == 0 || strcmp(arg_upper, "E N") == 0)
    {
        *type = PROTO_TYPE_EBCDIC;
        return 0;
    }

    return -1;
}

int proto_parse_mode(const char *argument, proto_transfer_mode_t *mode)
{
    if (!argument || !mode)
        return -1;

    char arg_upper[16];
    strncpy(arg_upper, argument, sizeof(arg_upper) - 1);
    arg_upper[sizeof(arg_upper) - 1] = '\0';
    to_uppercase(arg_upper);
    trim_whitespace(arg_upper);

    if (strcmp(arg_upper, "S") == 0)
    {
        *mode = PROTO_MODE_STREAM;
        return 0;
    }
    else if (strcmp(arg_upper, "B") == 0)
    {
        *mode = PROTO_MODE_BLOCK;
        return 0;
    }
    else if (strcmp(arg_upper, "C") == 0)
    {
        *mode = PROTO_MODE_COMPRESSED;
        return 0;
    }

    return -1;
}

int proto_parse_stru(const char *argument, proto_data_structure_t *structure)
{
    if (!argument || !structure)
        return -1;

    char arg_upper[16];
    strncpy(arg_upper, argument, sizeof(arg_upper) - 1);
    arg_upper[sizeof(arg_upper) - 1] = '\0';
    to_uppercase(arg_upper);
    trim_whitespace(arg_upper);

    if (strcmp(arg_upper, "F") == 0)
    {
        *structure = PROTO_STRU_FILE;
        return 0;
    }
    else if (strcmp(arg_upper, "R") == 0)
    {
        *structure = PROTO_STRU_RECORD;
        return 0;
    }
    else if (strcmp(arg_upper, "P") == 0)
    {
        *structure = PROTO_STRU_PAGE;
        return 0;
    }

    return -1;
}

int proto_port_to_address(const proto_port_params_t *params, char *ip_buffer, size_t buffer_size, uint16_t *port)
{
    if (!params || !ip_buffer || !port || buffer_size < 16)
        return -1;

    // Format IP address
    int written = snprintf(ip_buffer, buffer_size, "%u.%u.%u.%u",
                           params->h1, params->h2, params->h3, params->h4);

    if (written < 0 || (size_t)written >= buffer_size)
        return -1;

    // Calculate port number
    *port = (uint16_t)(params->p1 * 256 + params->p2);

    return 0;
}

int proto_address_to_pasv(const char *ip_address, uint16_t port, proto_pasv_params_t *params)
{
    if (!ip_address || !params)
        return -1;

    unsigned int h1, h2, h3, h4;

    // Parse IP address
    int parsed = sscanf(ip_address, "%u.%u.%u.%u", &h1, &h2, &h3, &h4);

    if (parsed != 4)
        return -1;

    // Validate values are in byte range
    if (h1 > 255 || h2 > 255 || h3 > 255 || h4 > 255)
        return -1;

    params->h1 = (uint8_t)h1;
    params->h2 = (uint8_t)h2;
    params->h3 = (uint8_t)h3;
    params->h4 = (uint8_t)h4;

    // Calculate port bytes
    params->p1 = (uint8_t)(port / 256);
    params->p2 = (uint8_t)(port % 256);

    return 0;
}

int proto_validate_path(const char *path)
{
    if (!path)
        return 0;

    // Check for empty path
    if (strlen(path) == 0)
        return 1;

    // Check for parent directory references
    if (strstr(path, "..") != NULL)
        return 0;

    size_t len = strlen(path);
    for (size_t i = 0; i < len; i++)
    {
        // Check for null bytes
        if (path[i] == '\0' && i < len - 1)
            return 0;
        // Check for control characters
        if (path[i] < 32 && path[i] != '\t') // Allow tab
            return 0;
    }

    return 1;
}

int proto_normalize_path(char *path, size_t max_length)
{
    if (!path || max_length == 0)
        return -1;

    size_t len = strlen(path);
    if (len >= max_length)
        return -1;

    // Convert backslashes to forward slashes
    for (size_t i = 0; i < len; i++)
    {
        if (path[i] == '\\')
            path[i] = '/';
    }

    // Remove duplicate slashes
    char *src = path;
    char *dst = path;
    int last_was_slash = 0;

    while (*src)
    {
        if (*src == '/')
        {
            if (!last_was_slash)
            {
                *dst++ = *src;
                last_was_slash = 1;
            }
        }
        else
        {
            *dst++ = *src;
            last_was_slash = 0;
        }
        src++;
    }
    *dst = '\0';

    // Remove trailing slash (unless it's the root)
    len = strlen(path);
    if (len > 1 && path[len - 1] == '/')
        path[len - 1] = '\0';

    return 0;
}