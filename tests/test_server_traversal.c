/* test_server_traversal.c - path traversal outside root is rejected. */

#include "test_helper.h"
#include "ftp_client.h"

#include <stdio.h>
#include <string.h>

int
main(void)
{
    char root[64];
    pid_t srv;
    ftp_session_t session;
    ftp_reply_t reply;
    int pipe_fds[2];
    int rc;

    if (make_tmpdir(root, sizeof(root)) < 0) {
        perror("mkdtemp");
        return 1;
    }

    srv = server_start(root, 54008, 54800, 54850);
    if (srv < 0)
        return 1;

    if (server_wait_ready(54008, 2000) < 0) {
        fprintf(stderr, "server not ready\n");
        server_stop(srv);
        return 1;
    }

    ftp_session_init(&session);
    rc = ftp_session_open(&session, "127.0.0.1", "54008", &reply, 5000);
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

    /* CWD with .. escape must be rejected */
    rc = ftp_session_cwd(&session,
        (slice_t){ "../../etc", 9 }, &reply, 5000);
    if (rc == 0) {
        fprintf(stderr, "CWD traversal should have failed\n");
        ftp_session_close(&session);
        server_stop(srv);
        return 1;
    }

    /* PWD must still report "/" (cwd unchanged) */
    rc = ftp_session_pwd(&session, &reply, 5000);
    if (rc < 0 || reply.code != 257) {
        fprintf(stderr, "pwd after failed cwd: %d\n", reply.code);
        ftp_session_close(&session);
        server_stop(srv);
        return 1;
    }
    if (strstr(reply.text, "\"/\"") == NULL) {
        fprintf(stderr, "cwd changed after traversal attempt: %s\n",
            reply.text);
        ftp_session_close(&session);
        server_stop(srv);
        return 1;
    }

    /* RETR with traversal path must be rejected */
    if (pipe(pipe_fds) < 0) {
        perror("pipe");
        ftp_session_close(&session);
        server_stop(srv);
        return 1;
    }

    rc = ftp_session_retr(&session,
        (slice_t){ "../../etc/passwd", 16 }, pipe_fds[1], &reply, 5000);
    close(pipe_fds[1]);
    close(pipe_fds[0]);
    if (rc == 0) {
        fprintf(stderr, "RETR traversal should have failed\n");
        ftp_session_close(&session);
        server_stop(srv);
        return 1;
    }

    ftp_session_close(&session);
    server_stop(srv);
    return 0;
}
