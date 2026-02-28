/**
 * @file logger.c
 * @brief Implementations for logger functions.
 *
 * @version 0.1
 * @date 2025-10-15
 *
 */
#define _XOPEN_SOURCE 500
#include "logger.h"
#include "filesys.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <pthread.h>

#ifdef _WIN32
#include <io.h>
#define isatty _isatty
#define fileno _fileno
#else
#include <unistd.h>
#endif

// String map for LogLevel
static const char *level_strings[] = {
    "DEBUG",
    "INFO",
    "WARN",
    "ERROR",
};

// ANSI Color code
static const char *level_colors[] = {
    "\x1b[36m", // DEBUG - Cyan
    "\x1b[32m", // INFO  - Green
    "\x1b[33m", // WARN  - Yellow
    "\x1b[31m", // ERROR - Red
};
#define COLOR_RESET "\x1b[0m"

// Global logger
static struct
{
    FILE *fp;          // File pointer to log file
    log_level_t level; // Minimum log level
    int initialized;   // To avoid reinit
    pthread_mutex_t lock;
} g_logger = {NULL, LOG_LEVEL_INFO, 0, PTHREAD_MUTEX_INITIALIZER};

int logger_init(const char *log_file, log_level_t level)
{
    if (g_logger.initialized)
    {
        logger_close(); // Close for reopen.
    }

    if (log_file)
    {
        g_logger.fp = fopen(log_file, "a");
        if (!g_logger.fp)
        {
            fprintf(stderr, "Failed to open log file: %s\n", log_file);
            return -1;
        }
        // Set line buffering to ensure timely log writing
        setvbuf(g_logger.fp, NULL, _IOLBF, 0);
    }
    else
    {
        g_logger.fp = stdout;
    }

    g_logger.level = level;
    g_logger.initialized = 1;

    return 0;
}

void logger_log(log_level_t level, const char *filename,
                int line, const char *funcname,
                const char *format, ...)
{
    // Bound check
    if (level < LOG_LEVEL_DEBUG || level > LOG_LEVEL_ERROR)
    {
        return;
    }

    if (!g_logger.initialized || level < g_logger.level)
    {
        return;
    }

    // Get timestamp
    char timestamp[32];
    get_timestamp(timestamp, sizeof(timestamp));

    // Get filename
    const char *ex_filename = fs_extract_filename(filename);

    pthread_mutex_lock(&g_logger.lock);

    // Double-check
    if (!g_logger.initialized || !g_logger.fp)
    {
        pthread_mutex_unlock(&g_logger.lock);
        return;
    }

    int in_tty = (g_logger.fp == stdout || g_logger.fp == stderr) && isatty(fileno(g_logger.fp));

    // Output header：[LEVEL][TIMESTAMP][FILE:LINE:FUNC]
    if (in_tty) // Use color in terminal outputs
    {
        fprintf(g_logger.fp, "%s[%s]%s[%s][%s:%d:%s] ",
                level_colors[level], level_strings[level], COLOR_RESET,
                timestamp, ex_filename, line, funcname);
    }
    else
    {
        fprintf(g_logger.fp, "[%s][%s][%s:%d:%s] ",
                level_strings[level], timestamp, ex_filename, line, funcname);
    }

    // Output message
    va_list args;
    va_start(args, format);
    vfprintf(g_logger.fp, format, args);
    va_end(args);

    fprintf(g_logger.fp, "\n");

    // Immediate output for error
    if (level == LOG_LEVEL_ERROR)
    {
        fflush(g_logger.fp);
    }

    pthread_mutex_unlock(&g_logger.lock);
}

void logger_close(void)
{
    pthread_mutex_lock(&g_logger.lock);
    if (g_logger.initialized && g_logger.fp &&
        g_logger.fp != stdout && g_logger.fp != stderr)
    {
        fflush(g_logger.fp);
        fclose(g_logger.fp);
    }
    g_logger.fp = NULL;
    g_logger.initialized = 0;
    pthread_mutex_unlock(&g_logger.lock);
}

void logger_set_level(log_level_t level)
{
    if (level >= LOG_LEVEL_DEBUG && level <= LOG_LEVEL_ERROR)
    {
        g_logger.level = level;
    }
}