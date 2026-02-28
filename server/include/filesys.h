/**
 * @file filesys.h
 * @brief Provide intermediate operations between different file systems
 *
 * All operations will return -1 if an error occurs.
 * ‘/’ and '\'  will be processed automatically
 *
 * TODO: Error codes are intended to be used afterward.
 *
 * @version 0.3
 * @date 2025-11-3
 *
 */
#ifndef FILESYS_H
#define FILESYS_H

#define MAX_FILENAME_LEN 256
#include <time.h>

#ifdef _WIN32
#include <stdint.h>
#ifdef __MINGW32__
// MinGW defines mode_t in sys/types.h
#include <sys/types.h>
#else
// For other Windows compilers
typedef uint32_t mode_t;
#endif

typedef uint32_t nlink_t;
typedef uint32_t uid_t;
typedef uint32_t gid_t;

// POSIX file type and permission constants for Windows
#define S_IFMT 0170000   // File type mask
#define S_IFDIR 0040000  // Directory
#define S_IFCHR 0020000  // Character device
#define S_IFBLK 0060000  // Block device
#define S_IFREG 0100000  // Regular file
#define S_IFIFO 0010000  // FIFO
#define S_IFLNK 0120000  // Symbolic link
#define S_IFSOCK 0140000 // Socket

// Permission bits
#define S_IRWXU 00700 // Read, write, execute by owner
#define S_IRUSR 00400 // Read by owner
#define S_IWUSR 00200 // Write by owner
#define S_IXUSR 00100 // Execute by owner
#define S_IRWXG 00070 // Read, write, execute by group
#define S_IRGRP 00040 // Read by group
#define S_IWGRP 00020 // Write by group
#define S_IXGRP 00010 // Execute by group
#define S_IRWXO 00007 // Read, write, execute by others
#define S_IROTH 00004 // Read by others
#define S_IWOTH 00002 // Write by others
#define S_IXOTH 00001 // Execute by others

#else
#include <sys/types.h>
#endif

// Define file types: File, Directory, Unknown
typedef enum
{
    FS_TYPE_UNKNOWN,
    FS_TYPE_FILE,
    FS_TYPE_DIR,
    FS_TYPE_SYMLINK
} fs_file_type_t;

// File information struct. Include filename, filesize and filetype.
typedef struct
{
    char name[MAX_FILENAME_LEN];        // File or directory name
    fs_file_type_t type;                // Type: file, directory, or unknown
    long long size;                     // File size in bytes (0 for directories)
    time_t last_modified;               // Last modification timestamp
    mode_t mode;                        // File permissions and type
    nlink_t nlink;                      // Number of hard links
    uid_t uid;                          // User ID of owner
    gid_t gid;                          // Group ID of owner
    char link_target[MAX_FILENAME_LEN]; // Target path for symbolic links
} fs_file_info_t;

/**
 * @brief Join directory and name into a single path stored in dest
 * @param dest Output buffer where the joined path will be written.
 * @param dest_size Size of the output buffer in bytes (including the NUL).
 * @param dir Directory path component (must not be NULL).
 * @param name File name or subpath component (must not be NULL).
 * @return int 0 on success, -1 on error (invalid arguments or insufficient buffer).
 */
int fs_join_path(char *dest, long long dest_size, const char *dir, const char *name);

/**
 * @brief Check whether file/dir exits.
 * @param path Path to the file/dir
 * @return int
 * @retval 1 - Exist
 * @retval 0 - Do not exist or error
 */
int fs_path_exists(const char *path);

/**
 * @brief Check whether the given path is a directory.
 * @param path Path
 * @return int
 * @retval 1 - A directory
 * @retval 0 - Not a directory
 */
int fs_is_directory(const char *path);

/**
 * @brief Get the file size.
 * @param path Path to the file
 * @return File size in byte, or -1 for non-existence or error.
 */
long long fs_get_file_size(const char *path);

/**
 * @brief Get the file modification time.
 * @param path Path to the file
 * @return Modification time as time_t, or -1 for non-existence or error.
 */
time_t fs_get_file_mtime(const char *path);

/**
 * @brief Get the directory size.
 * @param path Path to the directory
 * @return Directory size in byte, or -1 for non-existence or error.
 */
long long fs_get_directory_size(const char *path);

/**
 * @brief List the contents of the specified directory.
 *
 * Note: Special entries "." and ".." are NOT included in the result.
 *
 * @param path Path to directory
 * @param file_list Array pointer used to store file information.
 * @param max_files The maximum capacity of the file_list array.
 * @return The actual number of files/directories listed, or -1 if an error occurs.
 */
int fs_list_directory(const char *path, fs_file_info_t *file_list, int max_files);

/**
 * @brief Extract filename from path
 *
 * @param path Absolute path
 * @return filename
 */
const char *fs_extract_filename(const char *path);

/**
 * @brief Read the entire contents of a file into the provided buffer.
 * @param path File path
 * @param buffer Content buffer
 * @param buffer_size Maximum buffer size
 * @return The actual number of bytes read, or -1 if an error occurs.
 */
long long fs_read_file_all(const char *path, void *buffer, long long buffer_size);

/**
 * @brief Write the entire contents of the buffer into the given file.
 *
 * If the file does not exist, it will be created.
 *
 * @param path File path
 * @param buffer Content buffer
 * @param size The number of bytes to write from the buffer
 * @return The actual number of bytes written, or -1 if an error occurs.
 */
long long fs_write_file_all(const char *path, void *buffer, long long size);

/**
 * @brief Read data from a file at a specified offset and length.
 * @param path File path
 * @param buffer The buffer for storing reading data.
 * @param offset The starting offset for reading in file.
 * @param length The number of bytes to read.
 * @return The actual number of bytes read, or -1 if an error occurs.
 */
long long fs_read_file_chunk(const char *path, void *buffer, long long offset, long long length);

/**
 * @brief Write data of a specified offset and length to the file.
 * @param path File path
 * @param buffer The buffer containing the data to be written.
 * @param offset The starting offset for writing in file.
 * @param length The number of bytes to write.
 * @return The actual number of bytes written, or -1 if an error occurs.
 */
long long fs_write_file_chunk(const char *path, const void *buffer, long long offset, long long length);

/**
 * @brief Create a new directory.
 * @param path Path to new directory.
 * @return int
 * @retval 0 - Success
 * @retval -1 - Failure or error
 */
int fs_create_directory(const char *path);

/**
 * @brief Delete a file.
 * @param path Path to new file.
 * @return int
 * @retval 0 - Success
 * @retval -1 - Failure or error
 */
int fs_delete_file(const char *path);

/**
 * @brief Delete a directory.
 * @param path Path to directory to delete.
 * @param force_delete Default 0.
 * 0 means only empty folders will be deleted,
 * while non-zero means the folder will be forcibly deleted even if it is not empty.
 * @return int
 * @retval 0 - Success
 * @retval -1 - Failure or error
 */
int fs_delete_directory(const char *path, int force_delete);

/**
 * @brief Rename a file or directory.
 * @param old_path Path to the existing file or directory.
 * @param new_path New path for the file or directory.
 * @return int
 * @retval 0 - Success
 * @retval -1 - Failure or error
 */
int fs_rename(const char *old_path, const char *new_path);

/**
 * @brief Get the parent directory of the given path.
 *
 * Copies the parent path into the provided buffer, normalizing trailing
 * separators. Root paths ("/" or "C:\") remain as "/" or "C:\".
 *
 * @param path Input path string
 * @param parent Output buffer for parent directory
 * @param parent_size Size of the output buffer, including the terminator
 * @return 0 on success, -1 on error
 */
int fs_get_parent_directory(const char *path, char *parent, size_t parent_size);

#endif