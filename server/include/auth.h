/**
 * @file auth.h
 * @brief User authentication and authorization module
 * @version 0.1
 * @date 2025-11-03
 */

#ifndef AUTH_H
#define AUTH_H

#include <stdint.h>
#include <time.h>

/**
 * @brief Maximum length of username
 */
#define AUTH_MAX_USERNAME 256

/**
 * @brief Maximum length of password
 */
#define AUTH_MAX_PASSWORD 256

/**
 * @brief Maximum length of home directory path
 */
#define AUTH_MAX_HOME_DIR 1024

/**
 * @brief Maximum number of users
 */
#define AUTH_MAX_USERS 1024

/**
 * @brief User permission flags
 */
typedef enum
{
    AUTH_PERM_NONE = 0x00,   // No permissions
    AUTH_PERM_READ = 0x01,   // Read files and list directories
    AUTH_PERM_WRITE = 0x02,  // Write, upload, delete files
    AUTH_PERM_DELETE = 0x04, // Delete files and directories
    AUTH_PERM_RENAME = 0x08, // Rename files and directories
    AUTH_PERM_MKDIR = 0x10,  // Create directories
    AUTH_PERM_RMDIR = 0x20,  // Remove directories
    AUTH_PERM_ADMIN = 0x40,  // Administrative operations
    AUTH_PERM_ALL = 0xFF     // All permissions
} auth_permission_t;

/**
 * @brief User account information
 */
typedef struct
{
    char username[AUTH_MAX_USERNAME]; // Username
    char password_hash[65];           // SHA-256 hash (64 hex chars + null)
    char home_dir[AUTH_MAX_HOME_DIR]; // Home directory path
    auth_permission_t permissions;    // Permission flags
    int in_use;                       // 1 if slot is occupied, 0 if free
} auth_user_t;

/**
 * @brief Initializes the authentication module.
 *
 * Must be called once before using any other command functions.
 *
 * @return 0 on success, -1 on failure.
 */
int auth_init(void);

/**
 * @brief Cleans up the authentication module.
 *
 * This function releases any resources allocated by the authentication
 * module and clears the user database.
 * It should be called when the application is shutting down.
 *
 */
void auth_cleanup(void);

/**
 * @brief Enables or disables anonymous user login.
 *
 * When enabled, users with the username "anonymous" can log in
 * with any password.
 * Anonymous login is enabled by default.
 *
 * If an "anonymous" user exists in the user database, their
 * configuration (home directory and permissions) will be used.
 * Otherwise, default settings will be applied (read-only access,
 * default anonymous home directory).
 *
 * @param enable 1 to enable anonymous login, 0 to disable.
 */
void auth_set_anonymous_enabled(int enable);

/**
 * @brief Checks if anonymous login is currently enabled.
 *
 * @return 1 if anonymous login is enabled, 0 otherwise.
 */
int auth_is_anonymous_enabled(void);

/**
 * @brief Sets the default anonymous user configuration.
 *
 * This configuration is used when anonymous login is enabled
 * but no "anonymous" user exists in the user database.
 *
 * @param home_dir The default home directory for anonymous users.
 * @param permissions The default permissions for anonymous users.
 * @return 0 on success, -1 on failure.
 */
int auth_set_anonymous_defaults(const char *home_dir, auth_permission_t permissions);

/**
 * @brief Authenticates a user with username and password.
 *
 * This function verifies a user's credentials. For regular users,
 * the password is hashed and compared with the stored hash.
 *
 * If anonymous login is enabled and the username is "anonymous":
 * - Any password will be accepted
 * - If an "anonymous" user exists in the database, their config is used
 * - Otherwise, default anonymous settings are used
 *
 * @param username The username to authenticate.
 * @param password The plaintext password to verify.
 * @return 1 if authentication is successful, 0 otherwise.
 */
int auth_authenticate(const char *username, const char *password);

/**
 * @brief Checks if a user exists in the database.
 *
 * @param username The username to check.
 * @return 1 if the user exists, 0 otherwise.
 */
int auth_user_exists(const char *username);

/**
 * @brief Adds a new user to the database.
 *
 * This creates a new user with the specified username,
 * password, home directory, and permissions.
 * The password will be hashed before storage.
 *
 * @param username The username for the new user.
 * @param password The plaintext password for the new user.
 * @param home_dir The home directory path for the new user.
 * @param permissions The permission flags for the new user.
 * @return 0 on success, -1 on failure.
 */
int auth_add_user(const char *username,
                  const char *password,
                  const char *home_dir,
                  auth_permission_t permissions);

/**
 * @brief Retrieves user information.
 *
 * @param username The username to search for.
 * @return A pointer to the user structure if found in user table
 * (include virtual anonymous user), or NULL otherwise.
 */
const auth_user_t *auth_get_user(const char *username);

/**
 * @brief Checks if a user has specific permissions.
 *
 * @param username The username to check.
 * @param permission The permission flags to verify.
 * @return 1 if the user has the specified permissions, 0 otherwise.
 */
int auth_has_permission(const char *username, auth_permission_t permission);

/**
 * @brief Loads users from a file.
 *
 * This reads user data from a file and populates the user
 * database. The file must follow the expected format.
 * This should manually be called after auth_init() to load users.
 *
 * @param filename The path to the file containing user data.
 * @return 0 on success, -1 on failure.
 */
int auth_load_users(const char *filename);

/**
 * @brief Saves users to a file.
 *
 * This writes the current user database to a file in the
 * expected format.
 * This should manually be called after auth_init() to save users.
 * It will overwrite any existing file.
 *
 * @param filename The path to the file where user data will be saved.
 * @return 0 on success, -1 on failure.
 */
int auth_save_users(const char *filename);

#endif // AUTH_H