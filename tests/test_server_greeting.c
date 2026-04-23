/* test_server_greeting.c - server sends 220 on connect. */

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

    srv = server_start(root, 54001, 54100, 54150);
    if (srv < 0)
        return 1;

    if (server_wait_ready(54001, 2000) < 0) {
        fprintf(stderr, "server not ready\n");
        server_stop(srv);
        return 1;
    }

    ftp_session_init(&session);
    rc = ftp_session_open(&session, "127.0.0.1", "54001", &reply, 5000);
    if (rc < 0) {
        fprintf(stderr, "open failed: %s\n", strerror(errno));
        server_stop(srv);
        return 1;
    }

    if (reply.code != 220) {
        fprintf(stderr, "expected 220, got %d\n", reply.code);
        ftp_session_close(&session);
        server_stop(srv);
        return 1;
    }

    ftp_session_close(&session);
    server_stop(srv);
    return 0;
}
