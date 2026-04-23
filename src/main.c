/* main.c - FTP server listener, accept loop, and startup. */

#include "ftp_server.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* Server init and run                                                  */
/* ------------------------------------------------------------------ */

int
ftp_server_init(ftp_server_t *srv, const ftp_config_t *config)
{
    struct sockaddr_in addr;
    int fd;

    srv->config = *config;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;

    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
            &(int){1}, sizeof(int)) < 0) {
        close(fd);
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(config->port);

    if (inet_pton(AF_INET, config->bind_addr,
            &addr.sin_addr) <= 0) {
        close(fd);
        errno = EINVAL;
        return -1;
    }

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    if (listen(fd, config->max_sessions) < 0) {
        close(fd);
        return -1;
    }

    srv->listen_fd = fd;
    return 0;
}

static void
reap_children(int sig)
{
    int status;

    (void)sig;
    while (waitpid(-1, &status, WNOHANG) > 0)
        ;
}

void
ftp_server_run(ftp_server_t *srv)
{
    struct sigaction sa;
    int client_fd;
    pid_t pid;

    /* reap children automatically */
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = reap_children;
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &sa, NULL) < 0)
        ftp_fatal("sigaction: %s", strerror(errno));

    ftp_log(LOG_INFO, "listening on %s:%u",
        srv->config.bind_addr, srv->config.port);

    for (;;) {
        client_fd = accept(srv->listen_fd, NULL, NULL);
        if (client_fd < 0) {
            if (errno == EINTR)
                continue;
            ftp_log(LOG_ERROR, "accept failed: %s", strerror(errno));
            continue;
        }

        pid = fork();
        if (pid < 0) {
            ftp_log(LOG_ERROR, "fork failed: %s", strerror(errno));
            close(client_fd);
            continue;
        }

        if (pid == 0) {
            /* child: run session */
            close(srv->listen_fd);
            ftp_session_run(client_fd, &srv->config);
            /* never reached */
        }

        /* parent */
        close(client_fd);
        ftp_log(LOG_INFO, "spawned session pid %d", pid);
    }
}

/* ------------------------------------------------------------------ */
/* Configuration helpers                                               */
/* ------------------------------------------------------------------ */

static void
config_defaults(ftp_config_t *c)
{
    memset(c, 0, sizeof(*c));
    ftp_strlcpy(c->bind_addr, "0.0.0.0", sizeof(c->bind_addr));
    c->port           = 2121;
    c->ctrl_timeout_ms = CTRL_IDLE_MS;
    c->pasv_port_min  = 50000;
    c->pasv_port_max  = 50100;
    c->max_sessions   = 16;
}

static void
usage(const char *prog)
{
    fprintf(stderr,
        "usage: %s -r ROOT -u USER -p PASS"
        " [-b ADDR] [-P PORT]"
        " [-m PASV_MIN] [-M PASV_MAX]\n",
        prog);
}

/* ------------------------------------------------------------------ */
/* main                                                                 */
/* ------------------------------------------------------------------ */

int
main(int argc, char **argv)
{
    ftp_config_t config;
    ftp_server_t srv;
    int opt;
    bool have_root;
    bool have_user;
    bool have_pass;

    config_defaults(&config);
    have_root = false;
    have_user = false;
    have_pass = false;

    while ((opt = getopt(argc, argv, "r:u:p:b:P:m:M:")) != -1) {
        switch (opt) {
        case 'r':
            if (ftp_strlcpy(config.root, optarg,
                    sizeof(config.root)) >= sizeof(config.root))
                ftp_fatal("root path too long");
            have_root = true;
            break;
        case 'u':
            if (ftp_strlcpy(config.username, optarg,
                    sizeof(config.username)) >= sizeof(config.username))
                ftp_fatal("username too long");
            have_user = true;
            break;
        case 'p':
            if (ftp_strlcpy(config.password, optarg,
                    sizeof(config.password)) >= sizeof(config.password))
                ftp_fatal("password too long");
            have_pass = true;
            break;
        case 'b':
            if (ftp_strlcpy(config.bind_addr, optarg,
                    sizeof(config.bind_addr)) >= sizeof(config.bind_addr))
                ftp_fatal("bind address too long");
            break;
        case 'P': {
            long v;
            char *end;

            v = strtol(optarg, &end, 10);
            if (*end != '\0' || v < 1 || v > 65535)
                ftp_fatal("invalid port: %s", optarg);
            config.port = (uint16_t)v;
            break;
        }
        case 'm': {
            long v;
            char *end;

            v = strtol(optarg, &end, 10);
            if (*end != '\0' || v < 1 || v > 65535)
                ftp_fatal("invalid pasv_min: %s", optarg);
            config.pasv_port_min = (uint16_t)v;
            break;
        }
        case 'M': {
            long v;
            char *end;

            v = strtol(optarg, &end, 10);
            if (*end != '\0' || v < 1 || v > 65535)
                ftp_fatal("invalid pasv_max: %s", optarg);
            config.pasv_port_max = (uint16_t)v;
            break;
        }
        default:
            usage(argv[0]);
            return 2;
        }
    }

    if (!have_root || !have_user || !have_pass) {
        usage(argv[0]);
        return 2;
    }

    if (config.pasv_port_min > config.pasv_port_max)
        ftp_fatal("pasv_min > pasv_max");

    if (ftp_server_init(&srv, &config) < 0)
        ftp_fatal("server init failed: %s", strerror(errno));

    ftp_server_run(&srv);
}
