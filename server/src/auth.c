/**
 * @file auth.c
 * @brief User authentication and authorization implementation
 * @version 0.1
 * @date 2025-11-03
 */

#include "auth.h"
#include "logger.h"
#include "filesys.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <ctype.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

static int g_initialized = 0;
static int g_anonymous_enabled = 1; // Anonymous login enabled by default
static auth_user_t g_users[AUTH_MAX_USERS];
static pthread_mutex_t g_auth_lock = PTHREAD_MUTEX_INITIALIZER;

// Virtual anonymous user with default configuration
static auth_user_t g_virtual_anonymous = {
    .username = "anonymous",
    .password_hash = "",
    .home_dir = "/pub",
    .permissions = AUTH_PERM_READ,
    .in_use = 1};

// Forward declarations
static void hash_password(const char *password, char *hash_output);
static int verify_password(const char *password, const char *hash);
static auth_user_t *find_user(const char *username);

int auth_init(void)
{
    if (g_initialized)
        return 0;

    pthread_mutex_lock(&g_auth_lock);
    memset(g_users, 0, sizeof(g_users));
    g_initialized = 1;
    pthread_mutex_unlock(&g_auth_lock);
    LOG_INFO("Authentication module initialized");
    return 0;
}

void auth_cleanup(void)
{
    if (!g_initialized)
        return;

    pthread_mutex_lock(&g_auth_lock);
    memset(g_users, 0, sizeof(g_users));
    g_initialized = 0;
    pthread_mutex_unlock(&g_auth_lock);
    LOG_INFO("Authentication module cleaned up");
}

void auth_set_anonymous_enabled(int enable)
{
    pthread_mutex_lock(&g_auth_lock);
    g_anonymous_enabled = enable ? 1 : 0;
    pthread_mutex_unlock(&g_auth_lock);
    LOG_INFO("Anonymous login %s", g_anonymous_enabled ? "enabled" : "disabled");
}

int auth_is_anonymous_enabled(void)
{
    int enabled;
    pthread_mutex_lock(&g_auth_lock);
    enabled = g_anonymous_enabled;
    pthread_mutex_unlock(&g_auth_lock);
    return enabled;
}

int auth_set_anonymous_defaults(const char *home_dir, auth_permission_t permissions)
{
    if (!home_dir)
        return -1;

    pthread_mutex_lock(&g_auth_lock);
    strncpy(g_virtual_anonymous.home_dir, home_dir, AUTH_MAX_HOME_DIR - 1);
    g_virtual_anonymous.home_dir[AUTH_MAX_HOME_DIR - 1] = '\0';
    g_virtual_anonymous.permissions = permissions;
    pthread_mutex_unlock(&g_auth_lock);

    LOG_INFO("Anonymous defaults set: home='%s', permissions=0x%02X", home_dir, permissions);
    return 0;
}

int auth_load_users(const char *filename)
{
    if (!g_initialized)
        return -1;

    if (!filename)
        return -1;

    pthread_mutex_lock(&g_auth_lock);

    FILE *fp = fopen(filename, "r");
    if (!fp)
    {
        pthread_mutex_unlock(&g_auth_lock);
        LOG_WARN("Could not open user database file: %s", filename);
        return -1;
    }

    char line[2048];
    int count = 0;
    int line_num = 0;

    while (fgets(line, sizeof(line), fp) && count < AUTH_MAX_USERS)
    {
        line_num++;

        // Skip comments and empty lines
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r')
            continue;

        // Parse line: username:password_hash:home_dir:permissions
        char username[AUTH_MAX_USERNAME];
        char password_hash[65];
        char home_dir[AUTH_MAX_HOME_DIR];
        unsigned int permissions;

        int parsed = sscanf(line, "%255[^:]:%64[^:]:%1023[^:]:%u",
                            username, password_hash, home_dir, &permissions);

        if (parsed < 4)
        {
            LOG_WARN("Invalid line %d in user database", line_num);
            continue;
        }

        // Find free slot
        int slot = -1;
        for (int i = 0; i < AUTH_MAX_USERS; i++)
        {
            if (!g_users[i].in_use)
            {
                slot = i;
                break;
            }
        }

        if (slot == -1)
        {
            LOG_WARN("User database full, skipping remaining entries");
            break;
        }

        // Populate user entry
        strncpy(g_users[slot].username, username, AUTH_MAX_USERNAME - 1);
        g_users[slot].username[AUTH_MAX_USERNAME - 1] = '\0';

        strncpy(g_users[slot].password_hash, password_hash, 64);
        g_users[slot].password_hash[64] = '\0';

        strncpy(g_users[slot].home_dir, home_dir, AUTH_MAX_HOME_DIR - 1);
        g_users[slot].home_dir[AUTH_MAX_HOME_DIR - 1] = '\0';

        g_users[slot].permissions = (auth_permission_t)permissions;
        g_users[slot].in_use = 1;

        count++;
    }

    fclose(fp);
    pthread_mutex_unlock(&g_auth_lock);

    LOG_INFO("Loaded %d users from %s", count, filename);
    return 0;
}

int auth_save_users(const char *filename)
{
    if (!g_initialized)
        return -1;

    if (!filename)
        return -1;

    pthread_mutex_lock(&g_auth_lock);

    FILE *fp = fopen(filename, "w");
    if (!fp)
    {
        pthread_mutex_unlock(&g_auth_lock);
        LOG_ERROR("Failed to open user database file for writing: %s", filename);
        return -1;
    }

    fprintf(fp, "# FTP User Database\n");
    fprintf(fp, "# Format: username:password_hash:home_dir:permissions\n");
    fprintf(fp, "# home_dir should be relative to root_dir and start with /\n");
    fprintf(fp, "# permissions are hex values (bitwise OR of permission flags):\n");
    fprintf(fp, "#   0x01 = READ      - Read files and list directories\n");
    fprintf(fp, "#   0x02 = WRITE     - Write and upload files\n");
    fprintf(fp, "#   0x04 = DELETE    - Delete files\n");
    fprintf(fp, "#   0x08 = RENAME    - Rename files and directories\n");
    fprintf(fp, "#   0x10 = MKDIR     - Create directories\n");
    fprintf(fp, "#   0x20 = RMDIR     - Remove directories\n");
    fprintf(fp, "#   0x40 = ADMIN     - Administrative operations\n");
    fprintf(fp, "#   0xFF = ALL       - All permissions\n");
    fprintf(fp, "#\n");
    fprintf(fp, "# Example entries:\n");
    fprintf(fp, "#   admin:0000000000000000000000000000000000000000000000000000000000001234:/admin:255\n");
    fprintf(fp, "#   user1:0000000000000000000000000000000000000000000000000000000000005678:/users/user1:3\n");
    fprintf(fp, "#   readonly:0000000000000000000000000000000000000000000000000000000000004321:/pub:1\n");
    fprintf(fp, "#\n");
    fprintf(fp, "# Anonymous user can be defined here or will use default settings (/pub, READ only)\n");
    fprintf(fp, "# anonymous::/pub:1\n\n");

    int count = 0;
    for (int i = 0; i < AUTH_MAX_USERS; i++)
    {
        if (!g_users[i].in_use)
            continue;

        fprintf(fp, "%s:%s:%s:%u\n",
                g_users[i].username,
                g_users[i].password_hash,
                g_users[i].home_dir,
                (unsigned int)g_users[i].permissions);

        count++;
    }

    fclose(fp);
    pthread_mutex_unlock(&g_auth_lock);

    LOG_INFO("Saved %d users to %s", count, filename);
    return 0;
}

int auth_authenticate(const char *username, const char *password)
{
    if (!g_initialized)
        return 0;

    if (!username || !password)
        return 0;

    pthread_mutex_lock(&g_auth_lock);

    // Check for anonymous user with anonymous login enabled
    if (g_anonymous_enabled && strcmp(username, "anonymous") == 0)
    {
        // Anonymous user can log in with any password
        // Check if anonymous user exists in database (optional)
        auth_user_t *user = find_user(username);
        if (user)
        {
            pthread_mutex_unlock(&g_auth_lock);
            LOG_INFO("Anonymous user authenticated using database configuration");
            return 1;
        }
        else
        {
            // Use virtual anonymous user with default settings
            pthread_mutex_unlock(&g_auth_lock);
            LOG_INFO("Anonymous user authenticated using default virtual configuration");
            return 1;
        }
    }

    // Regular user authentication
    auth_user_t *user = find_user(username);
    if (!user)
    {
        pthread_mutex_unlock(&g_auth_lock);
        LOG_WARN("Authentication failed: user '%s' not found", username);
        return 0;
    }

    // Verify password
    if (!verify_password(password, user->password_hash))
    {
        pthread_mutex_unlock(&g_auth_lock);
        LOG_WARN("Authentication failed: invalid password for user '%s'", username);
        return 0;
    }

    pthread_mutex_unlock(&g_auth_lock);
    LOG_INFO("User '%s' authenticated successfully", username);
    return 1;
}

int auth_user_exists(const char *username)
{
    if (!g_initialized)
        return 0;

    if (!username)
        return 0;

    pthread_mutex_lock(&g_auth_lock);
    auth_user_t *user = find_user(username);
    int exists = (user != NULL);
    pthread_mutex_unlock(&g_auth_lock);

    return exists;
}

int auth_add_user(const char *username,
                  const char *password,
                  const char *home_dir,
                  auth_permission_t permissions)
{
    if (!g_initialized)
        return -1;

    if (!username || !password || !home_dir)
        return -1;

    pthread_mutex_lock(&g_auth_lock);

    // Check if user already exists
    if (find_user(username) != NULL)
    {
        pthread_mutex_unlock(&g_auth_lock);
        LOG_WARN("User '%s' already exists", username);
        return -1;
    }

    // Find free slot
    int slot = -1;
    for (int i = 0; i < AUTH_MAX_USERS; i++)
    {
        if (!g_users[i].in_use)
        {
            slot = i;
            break;
        }
    }

    if (slot == -1)
    {
        pthread_mutex_unlock(&g_auth_lock);
        LOG_ERROR("User database is full");
        return -1;
    }

    // Create user
    strncpy(g_users[slot].username, username, AUTH_MAX_USERNAME - 1);
    g_users[slot].username[AUTH_MAX_USERNAME - 1] = '\0';

    hash_password(password, g_users[slot].password_hash);

    strncpy(g_users[slot].home_dir, home_dir, AUTH_MAX_HOME_DIR - 1);
    g_users[slot].home_dir[AUTH_MAX_HOME_DIR - 1] = '\0';

    g_users[slot].permissions = permissions;
    g_users[slot].in_use = 1;

    pthread_mutex_unlock(&g_auth_lock);

    LOG_INFO("User '%s' added successfully", username);
    return 0;
}

const auth_user_t *auth_get_user(const char *username)
{
    if (!g_initialized)
        return NULL;

    if (!username)
        return NULL;

    pthread_mutex_lock(&g_auth_lock);

    // Check database first
    auth_user_t *user = find_user(username);

    // If not found and it's anonymous with anonymous login enabled,
    // return virtual anonymous user
    if (!user && g_anonymous_enabled && strcmp(username, "anonymous") == 0)
    {
        pthread_mutex_unlock(&g_auth_lock);
        return &g_virtual_anonymous;
    }

    pthread_mutex_unlock(&g_auth_lock);
    return user;
}

int auth_has_permission(const char *username, auth_permission_t permission)
{
    if (!g_initialized)
        return 0;

    if (!username)
        return 0;

    pthread_mutex_lock(&g_auth_lock);
    auth_user_t *user = find_user(username);

    // Check for virtual anonymous user
    if (!user && g_anonymous_enabled && strcmp(username, "anonymous") == 0)
        user = &g_virtual_anonymous;

    if (!user)
    {
        pthread_mutex_unlock(&g_auth_lock);
        return 0;
    }

    int has_perm = (user->permissions & permission) == permission;
    pthread_mutex_unlock(&g_auth_lock);

    return has_perm;
}

// Helper Functions

static void hash_password(const char *password, char *hash_output)
{
    if (!hash_output)
        return;

    if (!password)
    {
        hash_output[0] = '\0';
        return;
    }

    // Simple hash for demonstration (Really simple, should be modified later)
    unsigned int simple_hash = 0;
    for (const char *p = password; *p; p++)
    {
        simple_hash = simple_hash * 31 + (unsigned char)*p;
    }
    snprintf(hash_output, 65, "%064x", simple_hash);
}

static int verify_password(const char *password, const char *hash)
{
    char computed_hash[65];
    hash_password(password, computed_hash);
    return (strcmp(computed_hash, hash) == 0);
}

/// @brief  Finds a user by username in g_users
/// @param username The username to search for.
/// @return A pointer to the user structure if found, or NULL otherwise.
static auth_user_t *find_user(const char *username)
{
    for (int i = 0; i < AUTH_MAX_USERS; i++)
    {
        if (g_users[i].in_use && strcmp(g_users[i].username, username) == 0)
        {
            return &g_users[i];
        }
    }
    return NULL;
}