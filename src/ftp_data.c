/* ftp_data.c - passive data channel management and data transfer. */

#include "ftp_server.h"

#include <errno.h>
#include <netinet/in.h>
#include <string.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

static int
wait_fd(int fd, short events, int timeout_ms)
{
    struct pollfd pfd;
    int rc;

    pfd.fd = fd;
    pfd.events = events;
    pfd.revents = 0;

    for (;;) {
        rc = poll(&pfd, 1, timeout_ms);
        if (rc < 0 && errno == EINTR)
            continue;
        return rc;
    }
}

int
ftp_pasv_listen(uint16_t min_port, uint16_t max_port, uint16_t *port_out)
{
    int fd;
    struct sockaddr_in addr;
    socklen_t len;
    uint16_t port;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;

    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
            &(int){1}, sizeof(int)) < 0) {
        close(fd);
        return -1;
    }

    for (port = min_port; port <= max_port; port++) {
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(port);

        if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0)
            break;

        if (errno != EADDRINUSE) {
            close(fd);
            return -1;
        }
    }

    if (port > max_port) {
        close(fd);
        errno = EADDRINUSE;
        return -1;
    }

    if (listen(fd, 1) < 0) {
        close(fd);
        return -1;
    }

    len = sizeof(addr);
    if (getsockname(fd, (struct sockaddr *)&addr, &len) < 0) {
        close(fd);
        return -1;
    }

    *port_out = ntohs(addr.sin_port);
    return fd;
}

int
ftp_pasv_accept(int pasv_listen_fd, int timeout_ms)
{
    int rc;

    rc = wait_fd(pasv_listen_fd, POLLIN, timeout_ms);
    if (rc <= 0) {
        if (rc == 0)
            errno = ETIMEDOUT;
        return -1;
    }

    return accept(pasv_listen_fd, NULL, NULL);
}

int
ftp_data_copy(int dst_fd, int src_fd, int timeout_ms)
{
    char buf[65536];
    ssize_t nr;
    ssize_t nw;
    size_t off;

    for (;;) {
        int rc;

        rc = wait_fd(src_fd, POLLIN, timeout_ms);
        if (rc < 0)
            return -1;
        if (rc == 0) {
            errno = ETIMEDOUT;
            return -1;
        }

        nr = read(src_fd, buf, sizeof(buf));
        if (nr < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (nr == 0)
            break;

        off = 0;
        while (off < (size_t)nr) {
            rc = wait_fd(dst_fd, POLLOUT, timeout_ms);
            if (rc <= 0) {
                if (rc == 0)
                    errno = ETIMEDOUT;
                return -1;
            }

            nw = write(dst_fd, buf + off, (size_t)nr - off);
            if (nw < 0) {
                if (errno == EINTR)
                    continue;
                return -1;
            }
            off += (size_t)nw;
        }
    }

    return 0;
}
