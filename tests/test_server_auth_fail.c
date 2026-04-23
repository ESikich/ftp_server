/* test_server_auth_fail.c - bad password gets 530. */

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
    int rc;

    if (make_tmpdir(root, sizeof(root)) < 0) {
        perror("mkdtemp");
        return 1;
    }

    srv = server_start(root, 54003, 54300, 54350);
    if (srv < 0)
        return 1;

    if (server_wait_ready(54003, 2000) < 0) {
        fprintf(stderr, "server not ready\n");
        server_stop(srv);
        return 1;
    }

    ftp_session_init(&session);
    rc = ftp_session_open(&session, "127.0.0.1", "54003", &reply, 5000);
    if (rc < 0) {
        fprintf(stderr, "open failed: %s\n", strerror(errno));
        server_stop(srv);
        return 1;
    }

    rc = ftp_session_login(&session,
        (slice_t){ SERVER_USER, strlen(SERVER_USER) },
        (slice_t){ "wrongpass", 9 },
        &reply, 5000);
    if (rc == 0) {
        fprintf(stderr, "login should have failed\n");
        ftp_session_close(&session);
        server_stop(srv);
        return 1;
    }
    if (reply.code != 530) {
        fprintf(stderr, "expected 530, got %d\n", reply.code);
        ftp_session_close(&session);
        server_stop(srv);
        return 1;
    }

    ftp_session_close(&session);
    server_stop(srv);
    return 0;
}
