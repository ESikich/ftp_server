/* test_helper.h - utilities for server integration tests. */

#ifndef TEST_HELPER_H
#define TEST_HELPER_H

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#define SERVER_BIN  "build/ftp-server"
#define SERVER_USER "testuser"
#define SERVER_PASS "testpass"
#define SERVER_PASS_HASH "$6$ftpserver$W.gEv68KkenSkpTDtZ/mGL.nun.GJuqzZsXFx5/.XiOhG/gdcWXTAQgexO8jDkHC96G6cN58tliCMrXZtq3iw."

/*
 * Fork and exec the server binary.
 * Server stderr is redirected to /dev/null to keep test output clean.
 */
__attribute__((unused)) static pid_t
server_start(const char *root, uint16_t port,
    uint16_t pasv_min, uint16_t pasv_max)
{
    pid_t pid;
    char cfg_path[] = "/tmp/ftp-server-conf-XXXXXX";
    char port_s[8];
    char min_s[8];
    char max_s[8];
    int cfg_fd;
    int devnull;

    snprintf(port_s, sizeof(port_s), "%u", (unsigned)port);
    snprintf(min_s,  sizeof(min_s),  "%u", (unsigned)pasv_min);
    snprintf(max_s,  sizeof(max_s),  "%u", (unsigned)pasv_max);

    cfg_fd = mkstemp(cfg_path);
    if (cfg_fd < 0) {
        perror("mkstemp");
        return -1;
    }
    dprintf(cfg_fd,
        "root = \"%s\"\n"
        "\n"
        "[[users]]\n"
        "name = \"%s\"\n"
        "hash = \"%s\"\n"
        "home = \".\"\n"
        "perms = [\"read\", \"write\", \"delete\", \"mkdir\"]\n",
        root, SERVER_USER, SERVER_PASS_HASH);
    close(cfg_fd);

    pid = fork();
    if (pid < 0) {
        perror("fork");
        return -1;
    }
    if (pid == 0) {
        devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        execl(SERVER_BIN, "ftp-server",
            "-c", cfg_path,
            "-P", port_s, "-m", min_s, "-M", max_s,
            (char *)NULL);
        _exit(1);
    }
    return pid;
}

/*
 * Poll until the port is connectable or timeout_ms elapses.
 * Returns 0 on success, -1 on timeout.
 */
__attribute__((unused)) static int
server_wait_ready(uint16_t port, int timeout_ms)
{
    struct sockaddr_in addr;
    int fd;
    int elapsed;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);

    for (elapsed = 0; elapsed < timeout_ms; elapsed += 10) {
        fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0)
            return -1;
        if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
            close(fd);
            return 0;
        }
        close(fd);
        usleep(10000);
    }
    return -1;
}

__attribute__((unused)) static void
server_stop(pid_t pid)
{
    int status;

    kill(pid, SIGTERM);
    waitpid(pid, &status, 0);
}

/* Create a temp directory and return its path via out (size bytes). */
__attribute__((unused)) static int
make_tmpdir(char *out, size_t size)
{
    const char *tmpl = "/tmp/ftp-test-XXXXXX";

    if (size < strlen(tmpl) + 1) {
        errno = ENAMETOOLONG;
        return -1;
    }
    memcpy(out, tmpl, strlen(tmpl) + 1);
    if (mkdtemp(out) == NULL)
        return -1;
    return 0;
}

/* Write content to path (creating or truncating). */
__attribute__((unused)) static int
write_file(const char *path, const char *content)
{
    int fd;
    size_t len;
    ssize_t n;

    len = strlen(content);
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        return -1;
    n = write(fd, content, len);
    close(fd);
    return (n == (ssize_t)len) ? 0 : -1;
}

#endif /* TEST_HELPER_H */
