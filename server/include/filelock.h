/**
 * @file filelock.h
 * @brief Cooperative file locking helpers for FTP transfers.
 */
#ifndef FILELOCK_H
#define FILELOCK_H

/**
 * @brief Acquire a shared (read) lock for the specified absolute path.
 *
 * Multiple readers may hold the lock concurrently provided no writers are
 * waiting. Blocks until the lock can be acquired.
 *
 * @param path Absolute filesystem path to lock.
 * @return 0 on success, -1 on error.
 */
int file_lock_acquire_shared(const char *path);

/**
 * Try to acquire a shared (read) lock without blocking.
 *
 * If the lock cannot be acquired immediately, it returns immediately with an error.
 *
 * @param path Absolute filesystem path to lock.
 * @return 0 on success, -1 on error.
 */
int file_lock_try_acquire_shared(const char *path);

/**
 * @brief Acquire an exclusive (write) lock for the specified absolute path.
 *
 * Only a single writer may hold the lock, and writers have priority over new
 * readers to avoid starvation.
 *
 * @param path Absolute filesystem path to lock.
 * @return 0 on success, -1 on error.
 */
int file_lock_acquire_exclusive(const char *path);

/**
 * @brief Try to acquire an exclusive (write) lock without blocking.
 *
 * This is a non-blocking version of file_lock_acquire_exclusive(). If the
 * lock cannot be acquired immediately, it returns immediately with an error.
 *
 * @param path Absolute filesystem path to lock.
 * @return 0 on success (lock acquired), -1 if lock is busy or on error.
 */
int file_lock_try_acquire_exclusive(const char *path);

/**
 * @brief Release a previously acquired shared (read) lock.
 *
 * @param path Absolute filesystem path associated with the lock.
 */
void file_lock_release_shared(const char *path);

/**
 * @brief Release a previously acquired exclusive (write) lock.
 *
 * @param path Absolute filesystem path associated with the lock.
 */
void file_lock_release_exclusive(const char *path);

/**
 * @brief Check if the specified path has an exclusive (write) lock.
 *
 * This function performs a non-blocking check to determine if a file is
 * currently locked exclusively.
 *
 * @param path Absolute filesystem path to check.
 * @return 1 if locked exclusively, 0 if not locked or locked shared, -1 on error.
 */
int file_lock_is_exclusive_locked(const char *path);

/**
 * @brief Check if the specified path has any shared (read) locks.
 *
 * This function performs a non-blocking check to determine if a file is
 * currently locked for reading.
 *
 * @param path Absolute filesystem path to check.
 * @return Number of shared locks (>0 if locked shared), 0 if not locked, -1 on error.
 */
int file_lock_get_shared_lock_count(const char *path);

#endif
