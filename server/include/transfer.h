/**
 * @file transfer.h
 * @brief FTP data transfer operations
 * @version 0.1
 * @date 2025-11-02
 */

#ifndef TRANSFER_H
#define TRANSFER_H

#include "protocol.h"

// Forward declaration to avoid circular include
struct session_t;
typedef struct session_t session_t;

/**
 * @brief Transfer buffer size for file operations
 */
#define TRANSFER_BUFFER_SIZE 65536 // 64KB

/**
 * @brief Result codes for data transfer operations.
 */
typedef enum
{
	TRANSFER_STATUS_OK = 0,
	TRANSFER_STATUS_IO_ERROR = -1,
	TRANSFER_STATUS_CONN_ERROR = -2,
	TRANSFER_STATUS_INTERNAL_ERROR = -3,
	TRANSFER_STATUS_ABORTED = -4
} transfer_status_t;

/**
 * @brief Transfer thread states
 */
typedef enum
{
	TRANSFER_THREAD_IDLE,		// No transfer thread running
	TRANSFER_THREAD_STARTING,	// Transfer thread is starting
	TRANSFER_THREAD_RUNNING,	// Transfer thread is running
	TRANSFER_THREAD_COMPLETING, // Transfer thread is completing
	TRANSFER_THREAD_ABORTED		// Transfer thread was aborted
} transfer_thread_state_t;

/**
 * @brief Transfer operation types
 */
typedef enum
{
	TRANSFER_OP_SEND_FILE,   // Send file (RETR)
	TRANSFER_OP_RECV_FILE,   // Receive file (STOR/APPE)
	TRANSFER_OP_SEND_LIST,   // Send directory listing (LIST)
	TRANSFER_OP_SEND_NLST    // Send name list (NLST)
} transfer_operation_t;

/**
 * @brief Transfer parameters for async transfer thread
 */
typedef struct
{
	transfer_operation_t operation; // Operation type
	char filepath[1024];		    // File/directory path for transfer
	long long offset;			    // Transfer offset (for file operations)
	proto_transfer_type_t type;     // Transfer type (ASCII/BINARY)
	int lock_acquired;			    // 1 if file lock was acquired, 0 otherwise
} transfer_params_t;

/**
 * @brief Sends a file to the client through the data connection.
 *
 * Binary mode transmission.
 *
 * @param session The FTP session
 * @param filepath Absolute filesystem path to the file
 * @param offset Starting byte offset (for REST command support)
 * @return transfer_status_t value indicating success or the failure reason
 */
transfer_status_t transfer_send_file(session_t *session, const char *filepath, long long offset);

/**
 * @brief Receives a file from the client through the data connection
 *
 * @param session The FTP session
 * @param filepath Absolute filesystem path to save the file
 * @param offset Starting byte offset (for APPE/REST command support)
 * @return transfer_status_t value indicating success or the failure reason
 */
transfer_status_t transfer_receive_file(session_t *session, const char *filepath, long long offset);

/**
 * @brief Sends a file to the client through the data connection
 *
 * With ASCII Mode. Converts LF to CRLF for transmission
 *
 * @param session The FTP session
 * @param filepath Absolute filesystem path to the file
 * @param offset Starting byte offset
 * @return transfer_status_t value indicating success or the failure reason
 */
transfer_status_t transfer_send_file_ascii(session_t *session, const char *filepath, long long offset);

/**
 * @brief Receives a file from the client through the data connection
 *
 * With ASCII Mode. Converts CRLF to local type.
 *
 * @param session The FTP session
 * @param filepath Absolute filesystem path to save the file
 * @param offset Starting byte offset
 * @return transfer_status_t value indicating success or the failure reason
 */
transfer_status_t transfer_receive_file_ascii(session_t *session, const char *filepath, long long offset);

/**
 * @brief Sends directory listing to the client (LIST command)
 *
 * Binary mode transmission.
 *
 * @param session The FTP session
 * @param dirpath Absolute filesystem path to the directory
 * @return transfer_status_t indicating success or failure reason
 */
transfer_status_t transfer_send_list(session_t *session, const char *dirpath);

/**
 * @brief Sends name list to the client (NLST command)
 *
 * @param session The FTP session
 * @param dirpath Absolute filesystem path to the directory
 * @return transfer_status_t indicating success or failure reason
 */
transfer_status_t transfer_send_nlst(session_t *session, const char *dirpath);

/**
 * @brief Transfer thread function for async file transfers
 *
 * This function runs in a separate thread to perform file transfers
 * without blocking the main command processing thread.
 *
 * @param arg Pointer to session_t
 * @return NULL
 */
void *transfer_thread_func(void *arg);

#endif // TRANSFER_H