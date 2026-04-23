/* test_server_list.c - LIST and NLST return directory contents. */

#include "test_helper.h"
#include "ftp_client.h"

#include <stdio.h>
#include <string.h>

int
main(void)
{
    char root[64];
    char filepath[128];
    pid_t srv;
    ftp_session_t session;
    ftp_reply_t reply;
    char buf[4096];
    int pipe_fds[2];
    ssize_t n;
    size_t total;
    int rc;

    if (make_tmpdir(root, sizeof(root)) < 0) {
        perror("mkdtemp");
        return 1;
    }

    snprintf(filepath, sizeof(filepath), "%s/hello.txt", root);
    if (write_file(filepath, "hello\n") < 0) {
        perror("write_file");
        return 1;
    }

    srv = server_start(root, 54004, 54400, 54450);
    if (srv < 0)
        return 1;

    if (server_wait_ready(54004, 2000) < 0) {
        fprintf(stderr, "server not ready\n");
        server_stop(srv);
        return 1;
    }

    ftp_session_init(&session);
    rc = ftp_session_open(&session, "127.0.0.1", "54004", &reply, 5000);
    if (rc < 0) {
        fprintf(stderr, "open failed: %s\n", strerror(errno));
        server_stop(srv);
        return 1;
    }

    rc = ftp_session_login(&session,
        (slice_t){ SERVER_USER, strlen(SERVER_USER) },
        (slice_t){ SERVER_PASS, strlen(SERVER_PASS) },
        &reply, 5000);
    if (rc < 0) {
        fprintf(stderr, "login failed\n");
        ftp_session_close(&session);
        server_stop(srv);
        return 1;
    }

    /* LIST */
    if (pipe(pipe_fds) < 0) {
        perror("pipe");
        ftp_session_close(&session);
        server_stop(srv);
        return 1;
    }

    rc = ftp_session_list(&session, (slice_t){ "", 0 },
        pipe_fds[1], &reply, 5000);
    close(pipe_fds[1]);
    if (rc < 0 || reply.code != 226) {
        fprintf(stderr, "list failed: %d\n", reply.code);
        close(pipe_fds[0]);
        ftp_session_close(&session);
        server_stop(srv);
        return 1;
    }

    total = 0;
    while ((n = read(pipe_fds[0], buf + total,
            sizeof(buf) - 1 - total)) > 0)
        total += (size_t)n;
    buf[total] = '\0';
    close(pipe_fds[0]);

    if (strstr(buf, "hello.txt") == NULL) {
        fprintf(stderr, "hello.txt not in LIST output: %s\n", buf);
        ftp_session_close(&session);
        server_stop(srv);
        return 1;
    }

    /* NLST */
    if (pipe(pipe_fds) < 0) {
        perror("pipe");
        ftp_session_close(&session);
        server_stop(srv);
        return 1;
    }

    rc = ftp_session_nlst(&session, (slice_t){ "", 0 },
        pipe_fds[1], &reply, 5000);
    close(pipe_fds[1]);
    if (rc < 0 || reply.code != 226) {
        fprintf(stderr, "nlst failed: %d\n", reply.code);
        close(pipe_fds[0]);
        ftp_session_close(&session);
        server_stop(srv);
        return 1;
    }

    total = 0;
    while ((n = read(pipe_fds[0], buf + total,
            sizeof(buf) - 1 - total)) > 0)
        total += (size_t)n;
    buf[total] = '\0';
    close(pipe_fds[0]);

    if (strstr(buf, "hello.txt") == NULL) {
        fprintf(stderr, "hello.txt not in NLST output: %s\n", buf);
        ftp_session_close(&session);
        server_stop(srv);
        return 1;
    }

    ftp_session_close(&session);
    server_stop(srv);
    return 0;
}
