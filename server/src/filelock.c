/**
 * @file filelock.c
 * @brief Cooperative per-path locking to guard FTP transfers.
 */
#include "filelock.h"

#include "logger.h"
#include "session.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

typedef struct file_lock_entry
{
    char path[SESSION_MAX_PATH];
    unsigned int readers;
    unsigned int writers;
    unsigned int waiting_writers;
    pthread_cond_t cond;
    struct file_lock_entry *next;
} file_lock_entry_t;

static pthread_mutex_t g_file_lock_mutex = PTHREAD_MUTEX_INITIALIZER;
static file_lock_entry_t *g_file_lock_entries = NULL;

static int file_lock_path_valid(const char *path)
{
    if (!path)
    {
        return 0;
    }

    size_t len = strlen(path);
    if (len == 0 || len >= SESSION_MAX_PATH)
    {
        LOG_ERROR("Invalid path for file locking (len=%zu)", len);
        return 0;
    }

    return 1;
}

static file_lock_entry_t *file_lock_find(const char *path, file_lock_entry_t **out_prev)
{
    file_lock_entry_t *prev = NULL;
    file_lock_entry_t *curr = g_file_lock_entries;

    while (curr)
    {
        if (strcmp(curr->path, path) == 0)
        {
            if (out_prev)
            {
                *out_prev = prev;
            }
            return curr;
        }
        prev = curr;
        curr = curr->next;
    }

    if (out_prev)
    {
        *out_prev = NULL;
    }

    return NULL;
}

static file_lock_entry_t *file_lock_get_or_create(const char *path)
{
    file_lock_entry_t *entry = file_lock_find(path, NULL);
    if (entry)
    {
        return entry;
    }

    entry = (file_lock_entry_t *)calloc(1, sizeof(file_lock_entry_t));
    if (!entry)
    {
        LOG_ERROR("Failed to allocate memory for file lock entry");
        return NULL;
    }

    strncpy(entry->path, path, sizeof(entry->path) - 1);
    entry->path[sizeof(entry->path) - 1] = '\0';

    if (pthread_cond_init(&entry->cond, NULL) != 0)
    {
        LOG_ERROR("Failed to initialize condition variable for file lock");
        free(entry);
        return NULL;
    }

    entry->next = g_file_lock_entries;
    g_file_lock_entries = entry;
    return entry;
}

static void file_lock_remove_entry(file_lock_entry_t *entry, file_lock_entry_t *prev)
{
    if (!entry)
    {
        return;
    }

    if (prev)
    {
        prev->next = entry->next;
    }
    else
    {
        g_file_lock_entries = entry->next;
    }

    pthread_cond_destroy(&entry->cond);
    free(entry);
}

int file_lock_acquire_shared(const char *path)
{
    if (!file_lock_path_valid(path))
    {
        return -1;
    }

    pthread_mutex_lock(&g_file_lock_mutex);

    file_lock_entry_t *entry = file_lock_get_or_create(path);
    if (!entry)
    {
        pthread_mutex_unlock(&g_file_lock_mutex);
        return -1;
    }

    while (entry->writers > 0 || entry->waiting_writers > 0)
    {
        pthread_cond_wait(&entry->cond, &g_file_lock_mutex);
    }

    entry->readers++;

    pthread_mutex_unlock(&g_file_lock_mutex);
    return 0;
}

int file_lock_try_acquire_shared(const char *path)
{
    if (!file_lock_path_valid(path))
    {
        return -1;
    }

    pthread_mutex_lock(&g_file_lock_mutex);

    file_lock_entry_t *entry = file_lock_get_or_create(path);
    if (!entry)
    {
        pthread_mutex_unlock(&g_file_lock_mutex);
        return -1;
    }

    // Check if lock is available (no writers and no waiting writers)
    if (entry->writers > 0 || entry->waiting_writers > 0)
    {
        // Lock is busy, return immediately
        pthread_mutex_unlock(&g_file_lock_mutex);
        return -1;
    }

    // Lock is available, acquire it
    entry->readers++;

    pthread_mutex_unlock(&g_file_lock_mutex);
    return 0;
}

int file_lock_acquire_exclusive(const char *path)
{
    if (!file_lock_path_valid(path))
    {
        return -1;
    }

    pthread_mutex_lock(&g_file_lock_mutex);

    file_lock_entry_t *entry = file_lock_get_or_create(path);
    if (!entry)
    {
        pthread_mutex_unlock(&g_file_lock_mutex);
        return -1;
    }

    entry->waiting_writers++;

    while (entry->writers > 0 || entry->readers > 0)
    {
        pthread_cond_wait(&entry->cond, &g_file_lock_mutex);
    }

    entry->waiting_writers--;
    entry->writers = 1;

    pthread_mutex_unlock(&g_file_lock_mutex);
    return 0;
}

int file_lock_try_acquire_exclusive(const char *path)
{
    if (!file_lock_path_valid(path))
    {
        return -1;
    }

    pthread_mutex_lock(&g_file_lock_mutex);

    file_lock_entry_t *entry = file_lock_get_or_create(path);
    if (!entry)
    {
        pthread_mutex_unlock(&g_file_lock_mutex);
        return -1;
    }

    // Check if lock is available (no readers and no writers)
    if (entry->writers > 0 || entry->readers > 0)
    {
        // Lock is busy, return immediately
        pthread_mutex_unlock(&g_file_lock_mutex);
        return -1;
    }

    // Lock is available, acquire it
    entry->writers = 1;

    pthread_mutex_unlock(&g_file_lock_mutex);
    return 0;
}

static void file_lock_release_common(const char *path, int exclusive)
{
    if (!file_lock_path_valid(path))
    {
        return;
    }

    pthread_mutex_lock(&g_file_lock_mutex);

    file_lock_entry_t *prev = NULL;
    file_lock_entry_t *entry = file_lock_find(path, &prev);
    if (!entry)
    {
        pthread_mutex_unlock(&g_file_lock_mutex);
        LOG_WARN("Attempted to release non-existent file lock for '%s'", path);
        return;
    }

    if (exclusive)
    {
        if (entry->writers == 0)
        {
            LOG_WARN("Release exclusive lock called without writer for '%s'", path);
        }
        entry->writers = 0;
    }
    else
    {
        if (entry->readers == 0)
        {
            LOG_WARN("Release shared lock called with no readers for '%s'", path);
        }
        else
        {
            entry->readers--;
        }
    }

    pthread_cond_broadcast(&entry->cond);

    if (entry->readers == 0 && entry->writers == 0 && entry->waiting_writers == 0)
    {
        file_lock_remove_entry(entry, prev);
    }

    pthread_mutex_unlock(&g_file_lock_mutex);
}

void file_lock_release_shared(const char *path)
{
    file_lock_release_common(path, 0);
}

void file_lock_release_exclusive(const char *path)
{
    file_lock_release_common(path, 1);
}

int file_lock_is_exclusive_locked(const char *path)
{
    if (!file_lock_path_valid(path))
    {
        return -1;
    }

    pthread_mutex_lock(&g_file_lock_mutex);

    file_lock_entry_t *entry = file_lock_find(path, NULL);
    if (!entry)
    {
        pthread_mutex_unlock(&g_file_lock_mutex);
        return 0; // No lock entry
    }

    int result = (entry->writers > 0) ? 1 : 0;

    pthread_mutex_unlock(&g_file_lock_mutex);
    return result;
}

int file_lock_get_shared_lock_count(const char *path)
{
    if (!file_lock_path_valid(path))
    {
        return -1;
    }

    pthread_mutex_lock(&g_file_lock_mutex);

    file_lock_entry_t *entry = file_lock_find(path, NULL);
    if (!entry)
    {
        pthread_mutex_unlock(&g_file_lock_mutex);
        return 0; // No lock entry
    }

    int result = entry->readers;

    pthread_mutex_unlock(&g_file_lock_mutex);
    return result;
}
