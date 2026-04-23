/* ftp_reply.c - FTP reply generation helpers. */

#include "ftp_server.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int
write_all(int fd, const char *buf, size_t len)
{
    size_t done;
    ssize_t rc;

    done = 0;
    while (done < len) {
        rc = write(fd, buf + done, len - done);
        if (rc < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (rc == 0) {
            errno = EPIPE;
            return -1;
        }
        done += (size_t)rc;
    }
    return 0;
}

int
ftp_reply_send(int fd, int code, const char *text)
{
    char line[512];
    int n;

    n = snprintf(line, sizeof(line), "%d %s\r\n", code, text);
    if (n < 0 || (size_t)n >= sizeof(line)) {
        errno = EMSGSIZE;
        return -1;
    }
    return write_all(fd, line, (size_t)n);
}

int
ftp_reply_sendf(int fd, int code, const char *fmt, ...)
{
    char text[256];
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);

    if (n < 0 || (size_t)n >= sizeof(text)) {
        errno = EMSGSIZE;
        return -1;
    }
    return ftp_reply_send(fd, code, text);
}
