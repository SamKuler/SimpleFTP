/**
 * @file utils.c
 * @brief Utility functions
 * @version 0.1
 * @date 2025-11-07
 */

#define _POSIX_C_SOURCE 200809L
#include "utils.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

void get_timestamp(char *buffer, size_t size)
{
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(buffer, size, "%Y-%m-%d %H:%M:%S", tm_info);
}

void trim_whitespace(char *str)
{
    if (!str || !*str)
        return;

    // Trim leading whitespace
    char *start = str;
    while (*start && isspace((unsigned char)*start))
        start++;

    // Trim trailing whitespace
    char *end = start + strlen(start) - 1;
    while (end > start && isspace((unsigned char)*end))
        end--;

    // Move trimmed string to beginning and null-terminate
    size_t len = end - start + 1;
    memmove(str, start, len);
    str[len] = '\0';
}

void to_uppercase(char *str)
{
    if (!str)
        return;

    while (*str)
    {
        *str = (char)toupper((unsigned char)*str);
        str++;
    }
}

long long crlf_to_lf(const char *input_buffer, long long input_len, char *output_buffer, long long output_size)
{
    if (!input_buffer || !output_buffer || input_len < 0 || output_size <= 0)
        return -1;

    long long write_idx = 0;
    for (long long i = 0; i < input_len; i++)
    {
        if (input_buffer[i] == '\r')
        {
            // Check for CRLF sequence
            if (i + 1 < input_len && input_buffer[i + 1] == '\n')
            {
                if (write_idx + 1 > output_size)
                    return -1; // Output buffer too small
                output_buffer[write_idx++] = '\n';
                i++; // Skip next character (LF)
            }
            else
            {
                // Lone CR, treat as regular character
                if (write_idx + 1 > output_size)
                    return -1; // Output buffer too small
                output_buffer[write_idx++] = input_buffer[i];
            }
        }
        else
        {
            if (write_idx + 1 > output_size)
                return -1; // Output buffer too small
            output_buffer[write_idx++] = input_buffer[i];
        }
    }
    return write_idx;
}

long long lf_to_crlf(const char *input_buffer, long long input_len, char *output_buffer, long long output_size)
{
    if (!input_buffer || !output_buffer || input_len < 0 || output_size <= 0)
        return -1;

    long long write_idx = 0;
    for (long long i = 0; i < input_len; i++)
    {
        if (input_buffer[i] == '\n')
        {
            if (write_idx + 2 > output_size)
                return -1; // Output buffer too small
            output_buffer[write_idx++] = '\r';
            output_buffer[write_idx++] = '\n';
        }
        else
        {
            if (write_idx + 1 > output_size)
                return -1; // Output buffer too small
            output_buffer[write_idx++] = input_buffer[i];
        }
    }
    return write_idx;
}

int string_to_hex(const char *input, char *output_buffer, size_t buffer_size)
{
    if (!input || !output_buffer || buffer_size == 0)
        return -1;

    size_t input_len = strlen(input);
    size_t hex_len = 0;

    for (size_t i = 0; i < input_len && hex_len < buffer_size - 3; i++)
    {
        int written = snprintf(output_buffer + hex_len, buffer_size - hex_len, "%02X ", (unsigned char)input[i]);
        if (written < 0 || (size_t)written >= buffer_size - hex_len)
            return -1;
        hex_len += written;
    }

    if (hex_len > 0 && output_buffer[hex_len - 1] == ' ')
        output_buffer[hex_len - 1] = '\0'; // Remove trailing space
    else
        output_buffer[hex_len] = '\0';

    return 0;
}

void sleep_ms(unsigned int milliseconds)
{
#ifdef _WIN32
    Sleep(milliseconds);
#else
    struct timespec ts;
    ts.tv_sec = milliseconds / 1000;
    ts.tv_nsec = (milliseconds % 1000) * 1000000L;
    nanosleep(&ts, NULL);
#endif
}