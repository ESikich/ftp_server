/* test_server_stor.c - STOR uploads a file correctly. */

#include "test_helper.h"
#include "ftp_client.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

int
main(void)
{
    char root[64];
    char stored[128];
    char local[64];
    pid_t srv;
    ftp_session_t session;
    ftp_reply_t reply;
    char buf[256];
    int fd;
    ssize_t n;
    size_t total;
    int rc;
    struct stat st;

    if (make_tmpdir(root, sizeof(root)) < 0) {
        perror("mkdtemp");
        return 1;
    }

    snprintf(local, sizeof(local), "/tmp/ftp-stor-src-XXXXXX");
    fd = mkstemp(local);
    if (fd < 0) {
        perror("mkstemp");
        return 1;
    }
    if (write(fd, "upload content\n", 15) != 15) {
        perror("write");
        close(fd);
        return 1;
    }
    close(fd);

    srv = server_start(root, 54006, 54600, 54650);
    if (srv < 0) {
        unlink(local);
        return 1;
    }

    if (server_wait_ready(54006, 2000) < 0) {
        fprintf(stderr, "server not ready\n");
        unlink(local);
        server_stop(srv);
        return 1;
    }

    ftp_session_init(&session);
    rc = ftp_session_open(&session, "127.0.0.1", "54006", &reply, 5000);
    if (rc < 0) {
        fprintf(stderr, "open failed: %s\n", strerror(errno));
        unlink(local);
        server_stop(srv);
        return 1;
    }

    rc = ftp_session_login(&session,
        (slice_t){ SERVER_USER, strlen(SERVER_USER) },
        (slice_t){ SERVER_PASS, strlen(SERVER_PASS) },
        &reply, 5000);
    if (rc < 0) {
        fprintf(stderr, "login failed\n");
        unlink(local);
        ftp_session_close(&session);
        server_stop(srv);
        return 1;
    }

    fd = open(local, O_RDONLY);
    if (fd < 0) {
        perror("open local");
        ftp_session_close(&session);
        unlink(local);
        server_stop(srv);
        return 1;
    }

    rc = ftp_session_stor(&session, (slice_t){ "uploaded.txt", 12 },
        fd, &reply, 5000);
    close(fd);
    if (rc < 0 || reply.code != 226) {
        fprintf(stderr, "stor failed: %d\n", reply.code);
        ftp_session_close(&session);
        unlink(local);
        server_stop(srv);
        return 1;
    }

    /* verify the file landed on the server */
    snprintf(stored, sizeof(stored), "%s/uploaded.txt", root);
    if (stat(stored, &st) < 0) {
        fprintf(stderr, "stored file missing\n");
        ftp_session_close(&session);
        unlink(local);
        server_stop(srv);
        return 1;
    }

    fd = open(stored, O_RDONLY);
    if (fd < 0) {
        perror("open stored");
        ftp_session_close(&session);
        unlink(local);
        server_stop(srv);
        return 1;
    }
    total = 0;
    while ((n = read(fd, buf + total, sizeof(buf) - 1 - total)) > 0)
        total += (size_t)n;
    buf[total] = '\0';
    close(fd);

    if (strcmp(buf, "upload content\n") != 0) {
        fprintf(stderr, "unexpected stored content: [%s]\n", buf);
        ftp_session_close(&session);
        unlink(local);
        server_stop(srv);
        return 1;
    }

    ftp_session_close(&session);
    unlink(local);
    server_stop(srv);
    return 0;
}
