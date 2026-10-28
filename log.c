/*
 * Git: https://github.com/crypery
 * Author: https://crypery.com
 * License: GNU AGPL v3 (Affero GPL)
 */

/*
 * Logging module.
 *
 * Timestamped log output to console, file, both, or none, with configurable
 * severity levels (DEBUG, INFO, WARN, ERROR). Uses thread-safe Win32
 * GetLocalTime instead of locale-dependent CRT localtime/strftime. Thread-safe:
 * every write and callback invocation is serialized by an internal lock.
 *
 * Example:
 *   log_init(LOG_OUTPUT_FILE, LOG_LEVEL_INFO, "server.log");
 *   LOG_INFO("server started on port %d\n", 443);
 *   LOG_ERROR("request failed: %s\n", reason);
 *   log_close();
 */
#include "log.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
 * Cross-platform compatibility shims.
 *
 * Maps _stricmp() to strcasecmp() on POSIX systems. On Windows the CRT
 * provides both _stricmp() and strdup() natively.
 */

#ifndef COMPAT_P
#define COMPAT_P

#include <string.h>

#ifdef _WIN32
// _stricmp() and strdup() are provided by the Windows CRT
#else
#include <strings.h>
#define _stricmp strcasecmp
#endif

#endif // COMPAT_P

// Log module globals
static FILE *g_log_file = NULL;
static LogOutput g_log_output = LOG_OUTPUT_NONE;
static LogLevel g_log_level = LOG_LEVEL_INFO;
static char g_log_path[512] = DEFAULT_LOG_FILE;
static int g_log_open = 0;
static log_cb_t g_log_cb = NULL;

// Module-wide lock: log lines are produced by several worker threads, and the
// callback (log saving/mirroring) must not run concurrently with itself. Every
// log_vmsg()/log_set_callback()/log_init()/log_close() critical section is
// serialized through this lock.
#ifdef _WIN32
#include <windows.h>
static CRITICAL_SECTION g_log_lock;
static int g_log_lock_init = 0;
static void log_lock(void) {
    if (!g_log_lock_init) { InitializeCriticalSection(&g_log_lock); g_log_lock_init = 1; }
    EnterCriticalSection(&g_log_lock);
}
static void log_unlock(void) {
    LeaveCriticalSection(&g_log_lock);
}
#else
#include <pthread.h>
static pthread_mutex_t g_log_lock = PTHREAD_MUTEX_INITIALIZER;
static void log_lock(void) { pthread_mutex_lock(&g_log_lock); }
static void log_unlock(void) { pthread_mutex_unlock(&g_log_lock); }
#endif

// Install a callback that receives each formatted log line (log saving /
// mirroring). The assignment is protected by the module lock.
void log_set_callback(log_cb_t cb) {
    log_lock();
    g_log_cb = cb;
    log_unlock();
}

// Format the current local date/time into buf (thread-safe)
static void log_timestamp(char *buf, size_t buf_len) {
    time_t now = time(NULL);
    struct tm tmv;
#ifdef _WIN32
    localtime_s(&tmv, &now);
#else
    localtime_r(&now, &tmv);
#endif
    snprintf(buf, buf_len, "%04d-%02d-%02d %02d:%02d:%02d",
             tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
             tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
}

// Return the string name of a log level
static const char *log_level_str(LogLevel level) {
    switch (level) {
        case LOG_LEVEL_DEBUG: return "DEBUG";
        case LOG_LEVEL_INFO:  return "INFO";
        case LOG_LEVEL_WARN:  return "WARN";
        case LOG_LEVEL_ERROR: return "ERROR";
        default:              return "UNKNOWN";
    }
}

// Initialize logging with output mode, level, and file
void log_init(int output, int level, const char *filename) {
    if (g_log_open) log_close(); /* log_close() takes the lock itself */

    log_lock();
    g_log_output = LOG_OUTPUT_NONE;
    if (output == LOG_OUTPUT_BOTH) g_log_output = LOG_OUTPUT_BOTH;
    else if (output == LOG_OUTPUT_CONSOLE) g_log_output = LOG_OUTPUT_CONSOLE;
    else if (output == LOG_OUTPUT_FILE) g_log_output = LOG_OUTPUT_FILE;

    if (level >= LOG_LEVEL_DEBUG && level <= LOG_LEVEL_ERROR) {
        g_log_level = (LogLevel)level;
    } else {
        g_log_level = LOG_LEVEL_INFO;
    }

    if (filename && filename[0]) {
        strncpy(g_log_path, filename, sizeof(g_log_path) - 1);
        g_log_path[sizeof(g_log_path) - 1] = '\0';
    }

    if (g_log_output == LOG_OUTPUT_FILE || g_log_output == LOG_OUTPUT_BOTH) {
        g_log_file = fopen(g_log_path, "a");
        if (g_log_file) {
            char ts[64];
            log_timestamp(ts, sizeof(ts));
            fprintf(g_log_file, "[%s] [INFO] [main] log started: %s\n", ts, g_log_path);
            fflush(g_log_file);
        }
    }
    g_log_open = 1;
    log_unlock();
}

// Close log file and flush
void log_close(void) {
    log_lock();
    if (g_log_file) {
        char ts[64];
        log_timestamp(ts, sizeof(ts));
        fprintf(g_log_file, "[%s] [INFO] [main] log closed\n", ts);
        fflush(g_log_file);
        fclose(g_log_file);
        g_log_file = NULL;
    }
    g_log_open = 0;
    log_unlock();
}

// Write a log message at given level
void log_vmsg(LogLevel level, const char *fmt, va_list args) {
    if ((int)level < (int)g_log_level) return;

    char ts[64];
    log_timestamp(ts, sizeof(ts));

    // size of the formatted message (no fixed-length buffer limit)
    va_list args_copy;
    va_copy(args_copy, args);
    int msg_len = vsnprintf(NULL, 0, fmt, args_copy);
    va_end(args_copy);
    if (msg_len < 0) msg_len = 0;

    char *msg = (char *) malloc((size_t)msg_len + 1);
    if (!msg) return;
    vsnprintf(msg, (size_t)msg_len + 1, fmt, args);

    size_t ml = strlen(msg);
    while (ml > 0 && (msg[ml-1] == '\n' || msg[ml-1] == '\r')) msg[--ml] = '\0';

    // "[ts] [level] msg\n" + NUL
    const char *level_str = log_level_str(level);
    size_t full_len = strlen(ts) + strlen(level_str) + ml + 8;
    char *full_msg = (char *) malloc(full_len);
    if (!full_msg) { free(msg); return; }
    snprintf(full_msg, full_len, "[%s] [%s] %s\n", ts, level_str, msg);
    free(msg);

    log_lock();
    if (g_log_cb) g_log_cb(full_msg);

    if (g_log_output != LOG_OUTPUT_NONE) {
        if (g_log_output == LOG_OUTPUT_FILE || g_log_output == LOG_OUTPUT_BOTH) {
            if (g_log_file) {
                fprintf(g_log_file, "%s", full_msg);
                fflush(g_log_file);
            }
        }
        if (g_log_output == LOG_OUTPUT_CONSOLE || g_log_output == LOG_OUTPUT_BOTH) {
            fprintf(stderr, "%s", full_msg);
            fflush(stderr);
        }
    }
    log_unlock();
    free(full_msg);
}

// Write a formatted log message at given level
void log_msg(LogLevel level, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_vmsg(level, fmt, args);
    va_end(args);
}
