/* ftp_log.c - process-wide logging, fatal error, and compat helpers. */

#include "ftp_server.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

size_t
ftp_strlcpy(char *dst, const char *src, size_t size)
{
    size_t src_len;

    src_len = strlen(src);
    if (size > 0) {
        size_t copy = (src_len < size - 1) ? src_len : size - 1;
        memcpy(dst, src, copy);
        dst[copy] = '\0';
    }
    return src_len;
}

static const char *
level_str(log_level_t level)
{
    switch (level) {
    case LOG_INFO:  return "INFO";
    case LOG_WARN:  return "WARN";
    case LOG_ERROR: return "ERROR";
    default:        return "?";
    }
}

void
ftp_log(log_level_t level, const char *fmt, ...)
{
    struct timespec ts;
    struct tm tm;
    char tbuf[32];
    va_list ap;

    clock_gettime(CLOCK_REALTIME, &ts);
    gmtime_r(&ts.tv_sec, &tm);
    strftime(tbuf, sizeof(tbuf), "%Y-%m-%dT%H:%M:%SZ", &tm);

    fprintf(stderr, "[%s] %s ", tbuf, level_str(level));
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

void
ftp_fatal(const char *fmt, ...)
{
    va_list ap;

    fprintf(stderr, "[FATAL] ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}
