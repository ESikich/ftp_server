/* test_server_dele.c - DELE removes a file; missing file gets 550. */

#include "test_helper.h"
#include "ftp_client.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

int
main(void)
{
    char root[64];
    char filepath[128];
    pid_t srv;
    ftp_session_t session;
    ftp_reply_t reply;
    struct stat st;
    int rc;

    if (make_tmpdir(root, sizeof(root)) < 0) {
        perror("mkdtemp");
        return 1;
    }

    snprintf(filepath, sizeof(filepath), "%s/todelete.txt", root);
    if (write_file(filepath, "bye\n") < 0) {
        perror("write_file");
        return 1;
    }

    srv = server_start(root, 54007, 54700, 54750);
    if (srv < 0)
        return 1;

    if (server_wait_ready(54007, 2000) < 0) {
        fprintf(stderr, "server not ready\n");
        server_stop(srv);
        return 1;
    }

    ftp_session_init(&session);
    rc = ftp_session_open(&session, "127.0.0.1", "54007", &reply, 5000);
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

    /* delete existing file */
    rc = ftp_session_dele(&session, (slice_t){ "todelete.txt", 12 },
        &reply, 5000);
    if (rc < 0 || reply.code != 250) {
        fprintf(stderr, "dele failed: %d\n", reply.code);
        ftp_session_close(&session);
        server_stop(srv);
        return 1;
    }

    if (stat(filepath, &st) == 0) {
        fprintf(stderr, "file still exists after dele\n");
        ftp_session_close(&session);
        server_stop(srv);
        return 1;
    }

    /* delete non-existent file must fail with 550 */
    rc = ftp_session_dele(&session, (slice_t){ "todelete.txt", 12 },
        &reply, 5000);
    if (rc == 0) {
        fprintf(stderr, "dele missing file should have failed\n");
        ftp_session_close(&session);
        server_stop(srv);
        return 1;
    }
    if (reply.code != 550) {
        fprintf(stderr, "expected 550, got %d\n", reply.code);
        ftp_session_close(&session);
        server_stop(srv);
        return 1;
    }

    ftp_session_close(&session);
    server_stop(srv);
    return 0;
}
