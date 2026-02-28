/**
 * @file utils.h
 * @brief Utility functions
 * @version 0.1
 * @date 2025-11-07
 */
#ifndef UTILS_H
#define UTILS_H

#include <stddef.h>

/**
 * @brief Get timestamp string with format %Y-%m-%d %H:%M:%S
 *
 * @param buffer Output buffer
 * @param size Buffer size
 */
void get_timestamp(char *buffer, size_t size);

/**
 * @brief Trims leading and trailing whitespace from a string.
 *
 * @param str The string to trim (modified in place).
 */
void trim_whitespace(char *str);

/**
 * @brief Converts a string to uppercase.
 *
 * @param str The string to convert (modified in place).
 */
void to_uppercase(char *str);

/**
 * @brief Converts CRLF line endings to LF line endings in a buffer.
 *
 * @param input_buffer The input buffer containing data with CRLF.
 * @param input_len The number of bytes in the input buffer.
 * @param output_buffer The output buffer to store data with LF.
 * @param output_size The maximum size of the output buffer.
 * @return The number of bytes written to the output buffer, or -1 on error.
 */
long long crlf_to_lf(const char *input_buffer, long long input_len, char *output_buffer, long long output_size);

/**
 * @brief Converts LF line endings to CRLF line endings in a buffer.
 *
 * @param input_buffer The input buffer containing data with LF.
 * @param input_len The number of bytes in the input buffer.
 * @param output_buffer The output buffer to store data with CRLF.
 * @param output_size The maximum size of the output buffer.
 * @return The number of bytes written to the output buffer, or -1 on error.
 */
long long lf_to_crlf(const char *input_buffer, long long input_len, char *output_buffer, long long output_size);

/**
 * @brief Converts a string to its hexadecimal representation.
 *
 * Each byte is represented as two uppercase hex digits followed by a space.
 * The trailing space is removed from the output.
 *
 * @param input The input string to convert.
 * @param output_buffer The output buffer to store the hex string.
 * @param buffer_size The size of the output buffer.
 * @return 0 on success, -1 if buffer is too small.
 */
int string_to_hex(const char *input, char *output_buffer, size_t buffer_size);

/**
 * @brief Cross-platform millisecond sleep function.
 *
 * On Windows, uses Sleep().
 * On POSIX systems, uses usleep().
 *
 * @param milliseconds The number of milliseconds to sleep.
 */
void sleep_ms(unsigned int milliseconds);

#endif