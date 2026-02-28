/**
 * @file protocol.h
 * @brief FTP protocol parsing and response formatting (RFC 959)
 * @version 0.1
 * @date 2025-10-30
 *
 */
#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Maximum length of an FTP command name (e.g., "USER", "RETR").
 */
#define PROTO_MAX_CMD_NAME 8

/**
 * @brief Maximum length of an FTP command argument.
 */
#define PROTO_MAX_CMD_ARG 512

/**
 * @brief Maximum length of an FTP response line.
 */
#define PROTO_MAX_RESPONSE_LINE 1024

/**
 * @brief Structure representing a parsed FTP command.
 */
typedef struct
{
    char command[PROTO_MAX_CMD_NAME]; // Command name (e.g., "USER", "PASS")
    char argument[PROTO_MAX_CMD_ARG]; // Command argument (may be empty)
    int has_argument;                 // 1 if argument exists, 0 otherwise
} proto_command_t;

/**
 * @brief FTP transfer type for TYPE command.
 */
typedef enum
{
    PROTO_TYPE_ASCII,  // ASCII mode
    PROTO_TYPE_BINARY, // Binary/Image mode
    PROTO_TYPE_EBCDIC  // EBCDIC mode
} proto_transfer_type_t;

/**
 * @brief FTP transfer mode for MODE command.
 */
typedef enum
{
    PROTO_MODE_STREAM,    // Stream mode (default)
    PROTO_MODE_BLOCK,     // Block mode
    PROTO_MODE_COMPRESSED // Compressed mode
} proto_transfer_mode_t;

/**
 * @brief FTP data structure for STRU command.
 */
typedef enum
{
    PROTO_STRU_FILE,   // File structure (default)
    PROTO_STRU_RECORD, // Record structure
    PROTO_STRU_PAGE    // Page structure
} proto_data_structure_t;

/**
 * @brief Structure representing PORT command parameters (active mode).
 */
typedef struct
{
    uint8_t h1, h2, h3, h4; // Client IP address octets
    uint8_t p1, p2;         // Client port (port = p1 * 256 + p2)
} proto_port_params_t;

/**
 * @brief Structure representing PASV response parameters (passive mode).
 */
typedef struct
{
    uint8_t h1, h2, h3, h4; // Server IP address octets
    uint8_t p1, p2;         // Server port (port = p1 * 256 + p2)
} proto_pasv_params_t;

/**
 * @brief Parses an FTP command line into a command structure.
 *
 * FTP commands are in the format: "COMMAND [argument]\r\n"
 * This function extracts the command name and optional argument.
 *
 * @param line The raw command line received from the client (must include CRLF).
 * @param cmd Pointer to a command structure to fill with parsed data.
 * @return 0 on success, -1 if the line is malformed.
 */
int proto_parse_command(const char *line, proto_command_t *cmd);

/**
 * @brief Formats an FTP response message.
 *
 * FTP responses are in the format: "CODE message\r\n"
 * Multi-line responses use: "CODE-message\r\n" ... "CODE message\r\n"
 *
 * @param buffer The buffer to store the formatted response.
 * @param buffer_size The size of the buffer.
 * @param code The FTP response code (e.g., 220, 331, 230).
 * @param message The response message.
 * @return The number of bytes written to the buffer, or -1 on error.
 */
int proto_format_response(char *buffer, size_t buffer_size, int code, const char *message);

/**
 * @brief Formats a multi-line FTP response (first line).
 *
 * Used to start a multi-line response, or middle lines.
 * Use proto_format_response() for the final line.
 * Only generate one line.
 *
 * @param buffer The buffer to store the formatted response.
 * @param buffer_size The size of the buffer.
 * @param code The FTP response code.
 * @param message The response message.
 * @return The number of bytes written to the buffer, or -1 on error.
 */
int proto_format_response_multiline(char *buffer, size_t buffer_size, int code, const char *message);

/**
 * @brief Parses a PORT command argument.
 *
 * PORT command format: "h1,h2,h3,h4,p1,p2"
 * where h1-h4 are IP octets and p1,p2 form the port number.
 *
 * @param argument The PORT command argument string.
 * @param params Pointer to structure to fill with parsed parameters.
 * @return 0 on success, -1 if the argument is malformed.
 */
int proto_parse_port(const char *argument, proto_port_params_t *params);

/**
 * @brief Formats a PASV response argument.
 *
 * PASV response format: "227 Entering Passive Mode (h1,h2,h3,h4,p1,p2)\r\n"
 *
 * @param buffer The buffer to store the formatted response.
 * @param buffer_size The size of the buffer.
 * @param params The PASV parameters (server IP and port).
 * @return The number of bytes written to the buffer, or -1 on error.
 */
int proto_format_pasv_response(char *buffer, size_t buffer_size, const proto_pasv_params_t *params);

/**
 * @brief Parses a TYPE command argument.
 *
 * TYPE command format: "TYPE A" (ASCII) or "TYPE I" (Binary/Image)
 *
 * @param argument The TYPE command argument string.
 * @param type Pointer to store the parsed transfer type.
 * @return 0 on success, -1 if the argument is invalid.
 */
int proto_parse_type(const char *argument, proto_transfer_type_t *type);

/**
 * @brief Parses a MODE command argument.
 *
 * MODE command format: "MODE S" (Stream), "MODE B" (Block), or "MODE C" (Compressed)
 *
 * @param argument The MODE command argument string.
 * @param mode Pointer to store the parsed transfer mode.
 * @return 0 on success, -1 if the argument is invalid.
 */
int proto_parse_mode(const char *argument, proto_transfer_mode_t *mode);

/**
 * @brief Parses a STRU command argument.
 *
 * STRU command format: "STRU F" (File), "STRU R" (Record), or "STRU P" (Page)
 *
 * @param argument The STRU command argument string.
 * @param structure Pointer to store the parsed data structure.
 * @return 0 on success, -1 if the argument is invalid.
 */
int proto_parse_stru(const char *argument, proto_data_structure_t *structure);

/**
 * @brief Converts PORT parameters to IP address string and port number.
 *
 * @param params The PORT parameters.
 * @param ip_buffer Buffer to store the IP address string (min 16 bytes for IPv4).
 * @param buffer_size Size of the ip_buffer.
 * @param port Pointer to store the port number.
 * @return 0 on success, -1 on error.
 */
int proto_port_to_address(const proto_port_params_t *params, char *ip_buffer, size_t buffer_size, uint16_t *port);

/**
 * @brief Converts IP address string and port to PASV parameters.
 *
 * @param ip_address The IP address string (e.g., "192.168.1.1").
 * @param port The port number.
 * @param params Pointer to structure to fill with PASV parameters.
 * @return 0 on success, -1 on error.
 */
int proto_address_to_pasv(const char *ip_address, uint16_t port, proto_pasv_params_t *params);

/**
 * @brief Validates a path argument for security.
 *
 * Checks for path traversal attacks (e.g., "..", absolute paths).
 * This is just a basic check.
 *
 * @param path The path string to validate.
 * @return 1 if the path is safe, 0 if it's potentially dangerous.
 */
int proto_validate_path(const char *path);

/**
 * @brief Normalizes an FTP path.
 *
 * Converts Windows-style backslashes to forward slashes,
 * removes redundant slashes, etc.
 *
 * @param path The path to normalize (modified in place).
 * @param max_length Maximum length of the path buffer.
 * @return 0 on success, -1 on error.
 */
int proto_normalize_path(char *path, size_t max_length);

/**
 * @brief Common FTP response codes (RFC 959 4.2.2 Numeric Order List of Reply Codes).
 */
#define PROTO_RESP_RESTART_MARKER       110
#define PROTO_RESP_SERVICE_READY_MIN    120
#define PROTO_RESP_DATA_CONN_OPEN       125
#define PROTO_RESP_FILE_STATUS_OK       150

#define PROTO_RESP_OK                   200
#define PROTO_RESP_COMMAND_SUPERFLUOUS  202
#define PROTO_RESP_SYSTEM_STATUS        211
#define PROTO_RESP_DIR_STATUS           212
#define PROTO_RESP_FILE_STATUS          213
#define PROTO_RESP_HELP_MESSAGE         214
#define PROTO_RESP_SYSTEM_TYPE          215
#define PROTO_RESP_SERVICE_READY        220
#define PROTO_RESP_CLOSING_CONTROL      221
#define PROTO_RESP_DATA_CONN_OPEN_NO_TRANSFER 225
#define PROTO_RESP_CLOSING_DATA         226
#define PROTO_RESP_ENTERING_PASV        227
#define PROTO_RESP_USER_LOGGED_IN       230
#define PROTO_RESP_FILE_ACTION_OK       250
#define PROTO_RESP_PATH_CREATED         257

#define PROTO_RESP_NEED_PASSWORD        331
#define PROTO_RESP_NEED_ACCOUNT         332
#define PROTO_RESP_FILE_ACTION_PENDING  350

#define PROTO_RESP_SERVICE_NOT_AVAIL    421
#define PROTO_RESP_CANT_OPEN_DATA       425
#define PROTO_RESP_CONN_CLOSED          426
#define PROTO_RESP_FILE_ACTION_ABORTED  450
#define PROTO_RESP_LOCAL_ERROR          451
#define PROTO_RESP_INSUFFICIENT_STORAGE 452

#define PROTO_RESP_SYNTAX_ERROR         500
#define PROTO_RESP_SYNTAX_ERROR_PARAM   501
#define PROTO_RESP_COMMAND_NOT_IMPL     502
#define PROTO_RESP_BAD_COMMAND_SEQUENCE 503
#define PROTO_RESP_COMMAND_NOT_IMPL_PARAM 504
#define PROTO_RESP_NOT_LOGGED_IN        530
#define PROTO_RESP_NEED_ACCOUNT_STORE   532
#define PROTO_RESP_FILE_UNAVAILABLE     550
#define PROTO_RESP_PAGE_TYPE_UNKNOWN    551
#define PROTO_RESP_EXCEEDED_STORAGE     552
#define PROTO_RESP_BAD_FILENAME         553

#endif