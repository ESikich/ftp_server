/* ftp_session.c - FTP session state machine and command handlers. */

#include "ftp_server.h"

#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* I/O helpers                                                          */
/* ------------------------------------------------------------------ */

static int
wait_readable(int fd, int timeout_ms)
{
    struct pollfd pfd;
    int rc;

    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;

    for (;;) {
        rc = poll(&pfd, 1, timeout_ms);
        if (rc < 0 && errno == EINTR)
            continue;
        return rc;
    }
}

/*
 * Read one complete command line, using sess->ctrl as a persistent
 * buffer so unconsumed bytes from multi-command reads are retained.
 * Returns 0 on success (cmd populated), -1 on error or timeout.
 */
static int
read_command(ftp_session_t *sess, ftp_cmd_t *cmd, int timeout_ms)
{
    ftp_cmd_parser_t *p;
    ftp_ctrl_buf_t *ctrl;
    ssize_t nr;
    size_t consumed;
    int rc;

    p    = &sess->parser;
    ctrl = &sess->ctrl;

    for (;;) {
        /* try to extract a command from already-buffered bytes */
        if (ctrl->len > 0) {
            consumed = 0;
            rc = ftp_cmd_parser_feed(p, ctrl->data, ctrl->len,
                &consumed, cmd);
            if (consumed > 0) {
                size_t remain = ctrl->len - consumed;
                if (remain > 0)
                    memmove(ctrl->data, ctrl->data + consumed,
                        remain);
                ctrl->len = remain;
            }
            if (rc < 0)
                return -1;
            if (rc > 0)
                return 0;
        }

        /* buffer is drained; read more from the socket */
        if (ctrl->len >= sizeof(ctrl->data)) {
            errno = EOVERFLOW;
            return -1;
        }

        rc = wait_readable(sess->ctrl_fd, timeout_ms);
        if (rc <= 0) {
            if (rc == 0)
                errno = ETIMEDOUT;
            return -1;
        }

        nr = read(sess->ctrl_fd,
            ctrl->data + ctrl->len,
            sizeof(ctrl->data) - ctrl->len);
        if (nr < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (nr == 0) {
            errno = ECONNRESET;
            return -1;
        }

        ctrl->len += (size_t)nr;
    }
}

/* ------------------------------------------------------------------ */
/* PASV helpers                                                         */
/* ------------------------------------------------------------------ */

static void
pasv_close(ftp_session_t *sess)
{
    if (sess->pasv_armed) {
        close(sess->pasv.listen_fd);
        sess->pasv.listen_fd = -1;
        sess->pasv_armed = false;
    }
}

/* ------------------------------------------------------------------ */
/* Command handlers                                                     */
/* ------------------------------------------------------------------ */

static int
cmd_user(ftp_session_t *sess, const ftp_cmd_t *cmd,
    const ftp_config_t *config)
{
    (void)config;

    if (cmd->arg_len == 0)
        return ftp_reply_send(sess->ctrl_fd, 501,
            "Syntax error in parameters");

    if (ftp_strlcpy(sess->pending_user, cmd->arg,
            sizeof(sess->pending_user)) >= sizeof(sess->pending_user)) {
        return ftp_reply_send(sess->ctrl_fd, 501,
            "Username too long");
    }

    sess->auth = SESSION_NEED_PASS;
    return ftp_reply_send(sess->ctrl_fd, 331,
        "Password required");
}

static int
cmd_pass(ftp_session_t *sess, const ftp_cmd_t *cmd,
    const ftp_config_t *config)
{
    if (sess->auth != SESSION_NEED_PASS)
        return ftp_reply_send(sess->ctrl_fd, 503,
            "Login with USER first");

    if (strcmp(sess->pending_user, config->username) != 0 ||
        strcmp(cmd->arg, config->password) != 0) {
        ftp_log(LOG_WARN, "auth failure for user '%s'",
            sess->pending_user);
        sess->auth = SESSION_CONNECTED;
        sess->pending_user[0] = '\0';
        return ftp_reply_send(sess->ctrl_fd, 530,
            "Login incorrect");
    }

    sess->auth = SESSION_AUTHED;
    sess->pending_user[0] = '\0';
    ftp_log(LOG_INFO, "user '%s' logged in", config->username);
    return ftp_reply_send(sess->ctrl_fd, 230, "User logged in");
}

static int
cmd_quit(ftp_session_t *sess, const ftp_cmd_t *cmd,
    const ftp_config_t *config)
{
    (void)cmd;
    (void)config;
    sess->auth = SESSION_CLOSING;
    return ftp_reply_send(sess->ctrl_fd, 221,
        "Service closing control connection");
}

static int
cmd_noop(ftp_session_t *sess, const ftp_cmd_t *cmd,
    const ftp_config_t *config)
{
    (void)cmd;
    (void)config;
    return ftp_reply_send(sess->ctrl_fd, 200, "OK");
}

static int
cmd_syst(ftp_session_t *sess, const ftp_cmd_t *cmd,
    const ftp_config_t *config)
{
    (void)cmd;
    (void)config;
    return ftp_reply_send(sess->ctrl_fd, 215, "UNIX Type: L8");
}

static int
cmd_type(ftp_session_t *sess, const ftp_cmd_t *cmd,
    const ftp_config_t *config)
{
    (void)config;

    if (cmd->arg_len == 0)
        return ftp_reply_send(sess->ctrl_fd, 501,
            "Syntax error in parameters");

    if (strcmp(cmd->arg, "I") == 0) {
        sess->xfer_type = TYPE_IMAGE;
        return ftp_reply_send(sess->ctrl_fd, 200, "Type set to I");
    }
    if (strcmp(cmd->arg, "A") == 0 || strcmp(cmd->arg, "A N") == 0) {
        /* TYPE A: passthrough only; local newlines are LF, not CRLF */
        sess->xfer_type = TYPE_ASCII;
        return ftp_reply_send(sess->ctrl_fd, 200, "Type set to A");
    }

    return ftp_reply_send(sess->ctrl_fd, 504,
        "Command not implemented for that parameter");
}

static int
cmd_pwd(ftp_session_t *sess, const ftp_cmd_t *cmd,
    const ftp_config_t *config)
{
    (void)cmd;
    (void)config;
    return ftp_reply_sendf(sess->ctrl_fd, 257, "\"%s\"", sess->cwd);
}

static int
cmd_cwd(ftp_session_t *sess, const ftp_cmd_t *cmd,
    const ftp_config_t *config)
{
    char resolved[PATH_BUF_SIZE];
    struct stat st;
    char new_logical[PATH_BUF_SIZE];
    int n;

    if (cmd->arg_len == 0)
        return ftp_reply_send(sess->ctrl_fd, 501,
            "Syntax error in parameters");

    if (ftp_path_resolve(config->root, sess->cwd, cmd->arg,
            resolved) < 0) {
        return ftp_reply_send(sess->ctrl_fd, 550,
            "Requested action not taken");
    }

    if (stat(resolved, &st) < 0)
        return ftp_reply_send(sess->ctrl_fd, 550,
            "No such file or directory");

    if (!S_ISDIR(st.st_mode))
        return ftp_reply_send(sess->ctrl_fd, 550, "Not a directory");

    /* Update logical cwd */
    if (cmd->arg[0] == '/') {
        n = snprintf(new_logical, sizeof(new_logical), "%s", cmd->arg);
    } else if (strcmp(sess->cwd, "/") == 0) {
        n = snprintf(new_logical, sizeof(new_logical),
            "/%s", cmd->arg);
    } else {
        n = snprintf(new_logical, sizeof(new_logical),
            "%s/%s", sess->cwd, cmd->arg);
    }

    if (n < 0 || (size_t)n >= sizeof(new_logical))
        return ftp_reply_send(sess->ctrl_fd, 550,
            "Path too long");

    ftp_path_normalize(new_logical);
    ftp_strlcpy(sess->cwd, new_logical, sizeof(sess->cwd));

    return ftp_reply_send(sess->ctrl_fd, 250,
        "Directory successfully changed");
}

static int
cmd_mkd(ftp_session_t *sess, const ftp_cmd_t *cmd,
    const ftp_config_t *config)
{
    char resolved[PATH_BUF_SIZE];

    if (cmd->arg_len == 0)
        return ftp_reply_send(sess->ctrl_fd, 501,
            "Syntax error in parameters");

    if (ftp_path_resolve(config->root, sess->cwd, cmd->arg,
            resolved) < 0) {
        return ftp_reply_send(sess->ctrl_fd, 550,
            "Requested action not taken");
    }

    if (mkdir(resolved, 0755) < 0) {
        if (errno == EEXIST)
            return ftp_reply_send(sess->ctrl_fd, 550,
                "Directory already exists");
        return ftp_reply_send(sess->ctrl_fd, 550,
            "Failed to create directory");
    }

    ftp_log(LOG_INFO, "MKD %s ok", resolved);
    return ftp_reply_sendf(sess->ctrl_fd, 257, "\"%s\" directory created",
        cmd->arg);
}

static int
cmd_cdup(ftp_session_t *sess, const ftp_cmd_t *cmd,
    const ftp_config_t *config)
{
    ftp_cmd_t up;

    (void)cmd;
    memset(&up, 0, sizeof(up));
    ftp_strlcpy(up.verb, "CWD", sizeof(up.verb));
    ftp_strlcpy(up.arg, "..", sizeof(up.arg));
    up.arg_len = 2;
    return cmd_cwd(sess, &up, config);
}

static int
cmd_pasv(ftp_session_t *sess, const ftp_cmd_t *cmd,
    const ftp_config_t *config)
{
    struct sockaddr_in ctrl_addr;
    socklen_t addrlen;
    uint8_t h1, h2, h3, h4;
    uint8_t p1, p2;
    uint16_t port;
    int listen_fd;

    (void)cmd;

    /* replace any previously armed passive listener */
    pasv_close(sess);

    listen_fd = ftp_pasv_listen(config->pasv_port_min,
        config->pasv_port_max, &port);
    if (listen_fd < 0) {
        ftp_log(LOG_ERROR, "pasv_listen failed: %s", strerror(errno));
        return ftp_reply_send(sess->ctrl_fd, 425,
            "Can't open data connection");
    }

    /*
     * Report the control connection's local IP address.
     * IPv4 only (design assumption).
     */
    addrlen = sizeof(ctrl_addr);
    if (getsockname(sess->ctrl_fd,
            (struct sockaddr *)&ctrl_addr, &addrlen) < 0) {
        close(listen_fd);
        return ftp_reply_send(sess->ctrl_fd, 425,
            "Can't open data connection");
    }

    h1 = (uint8_t)((ntohl(ctrl_addr.sin_addr.s_addr) >> 24) & 0xff);
    h2 = (uint8_t)((ntohl(ctrl_addr.sin_addr.s_addr) >> 16) & 0xff);
    h3 = (uint8_t)((ntohl(ctrl_addr.sin_addr.s_addr) >>  8) & 0xff);
    h4 = (uint8_t)((ntohl(ctrl_addr.sin_addr.s_addr)      ) & 0xff);
    p1 = (uint8_t)(port / 256);
    p2 = (uint8_t)(port % 256);

    sess->pasv.listen_fd = listen_fd;
    sess->pasv.port = port;
    snprintf(sess->pasv.pasv_arg, sizeof(sess->pasv.pasv_arg),
        "%u,%u,%u,%u,%u,%u", h1, h2, h3, h4, p1, p2);
    sess->pasv_armed = true;

    return ftp_reply_sendf(sess->ctrl_fd, 227,
        "Entering Passive Mode (%s)", sess->pasv.pasv_arg);
}

/* ------------------------------------------------------------------ */
/* Directory listing helpers                                            */
/* ------------------------------------------------------------------ */

static const char *
mode_str(mode_t m, char *buf)
{
    buf[0] = S_ISDIR(m)  ? 'd' :
             S_ISLNK(m)  ? 'l' : '-';
    buf[1] = (m & S_IRUSR) ? 'r' : '-';
    buf[2] = (m & S_IWUSR) ? 'w' : '-';
    buf[3] = (m & S_IXUSR) ? 'x' : '-';
    buf[4] = (m & S_IRGRP) ? 'r' : '-';
    buf[5] = (m & S_IWGRP) ? 'w' : '-';
    buf[6] = (m & S_IXGRP) ? 'x' : '-';
    buf[7] = (m & S_IROTH) ? 'r' : '-';
    buf[8] = (m & S_IWOTH) ? 'w' : '-';
    buf[9] = (m & S_IXOTH) ? 'x' : '-';
    buf[10] = '\0';
    return buf;
}

static int
write_all_data(int fd, const char *buf, size_t len)
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

static int
send_list(int data_fd, const char *dir_path)
{
    DIR *dir;
    struct dirent *ent;
    struct stat st;
    char entry_path[PATH_BUF_SIZE];
    char mtime[32];
    char modebuf[12];
    char line[LIST_LINE_MAX];
    struct tm tm;
    int n;

    dir = opendir(dir_path);
    if (dir == NULL)
        return -1;

    while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 ||
            strcmp(ent->d_name, "..") == 0)
            continue;

        n = snprintf(entry_path, sizeof(entry_path),
            "%s/%s", dir_path, ent->d_name);
        if (n < 0 || (size_t)n >= sizeof(entry_path))
            continue;

        if (lstat(entry_path, &st) < 0)
            continue;

        gmtime_r(&st.st_mtime, &tm);
        strftime(mtime, sizeof(mtime), "%b %d %H:%M", &tm);

        n = snprintf(line, sizeof(line),
            "%s %3u %8u %8u %10llu %s %s\r\n",
            mode_str(st.st_mode, modebuf),
            (unsigned)st.st_nlink,
            (unsigned)st.st_uid,
            (unsigned)st.st_gid,
            (unsigned long long)st.st_size,
            mtime,
            ent->d_name);
        if (n < 0 || (size_t)n >= sizeof(line))
            continue;

        if (write_all_data(data_fd, line, (size_t)n) < 0) {
            closedir(dir);
            return -1;
        }
    }

    closedir(dir);
    return 0;
}

static int
send_nlst(int data_fd, const char *dir_path)
{
    DIR *dir;
    struct dirent *ent;
    char line[NAME_MAX + 3];
    int n;

    dir = opendir(dir_path);
    if (dir == NULL)
        return -1;

    while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 ||
            strcmp(ent->d_name, "..") == 0)
            continue;

        n = snprintf(line, sizeof(line), "%s\r\n", ent->d_name);
        if (n < 0 || (size_t)n >= sizeof(line))
            continue;

        if (write_all_data(data_fd, line, (size_t)n) < 0) {
            closedir(dir);
            return -1;
        }
    }

    closedir(dir);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Transfer commands (LIST, NLST, RETR, STOR)                          */
/* ------------------------------------------------------------------ */

static int
cmd_list_like(ftp_session_t *sess, const ftp_cmd_t *cmd,
    const ftp_config_t *config, bool nlst)
{
    char resolved[PATH_BUF_SIZE];
    const char *target;
    struct stat st;
    int data_fd;
    int rc;

    if (!sess->pasv_armed)
        return ftp_reply_send(sess->ctrl_fd, 425,
            "Use PASV first");

    target = (cmd->arg_len > 0) ? cmd->arg : sess->cwd;

    if (ftp_path_resolve(config->root, sess->cwd, target,
            resolved) < 0) {
        pasv_close(sess);
        return ftp_reply_send(sess->ctrl_fd, 550,
            "Requested action not taken");
    }

    if (stat(resolved, &st) < 0) {
        pasv_close(sess);
        return ftp_reply_send(sess->ctrl_fd, 550,
            "No such file or directory");
    }

    if (!S_ISDIR(st.st_mode)) {
        pasv_close(sess);
        return ftp_reply_send(sess->ctrl_fd, 550, "Not a directory");
    }

    if (ftp_reply_send(sess->ctrl_fd, 150,
            "Opening data connection for directory listing") < 0) {
        pasv_close(sess);
        return -1;
    }

    data_fd = ftp_pasv_accept(sess->pasv.listen_fd, PASV_ACCEPT_MS);
    pasv_close(sess);

    if (data_fd < 0) {
        ftp_log(LOG_ERROR, "pasv accept failed: %s", strerror(errno));
        return ftp_reply_send(sess->ctrl_fd, 425,
            "Can't open data connection");
    }

    rc = nlst ? send_nlst(data_fd, resolved)
              : send_list(data_fd, resolved);
    close(data_fd);

    if (rc < 0) {
        ftp_log(LOG_ERROR, "listing failed: %s", strerror(errno));
        return ftp_reply_send(sess->ctrl_fd, 451,
            "Requested action aborted");
    }

    ftp_log(LOG_INFO, "%s %s", nlst ? "NLST" : "LIST", resolved);
    return ftp_reply_send(sess->ctrl_fd, 226, "Transfer complete");
}

static int
cmd_retr(ftp_session_t *sess, const ftp_cmd_t *cmd,
    const ftp_config_t *config)
{
    char resolved[PATH_BUF_SIZE];
    struct stat st;
    int src_fd;
    int data_fd;
    int rc;

    if (!sess->pasv_armed)
        return ftp_reply_send(sess->ctrl_fd, 425,
            "Use PASV first");

    if (cmd->arg_len == 0) {
        pasv_close(sess);
        return ftp_reply_send(sess->ctrl_fd, 501,
            "Syntax error in parameters");
    }

    if (ftp_path_resolve(config->root, sess->cwd, cmd->arg,
            resolved) < 0) {
        pasv_close(sess);
        return ftp_reply_send(sess->ctrl_fd, 550,
            "Requested action not taken");
    }

    if (stat(resolved, &st) < 0) {
        pasv_close(sess);
        return ftp_reply_send(sess->ctrl_fd, 550,
            "No such file or directory");
    }

    if (!S_ISREG(st.st_mode)) {
        pasv_close(sess);
        return ftp_reply_send(sess->ctrl_fd, 550, "Not a regular file");
    }

    src_fd = open(resolved, O_RDONLY);
    if (src_fd < 0) {
        pasv_close(sess);
        return ftp_reply_send(sess->ctrl_fd, 550,
            "Failed to open file");
    }

    if (ftp_reply_send(sess->ctrl_fd, 150,
            "Opening data connection") < 0) {
        close(src_fd);
        pasv_close(sess);
        return -1;
    }

    data_fd = ftp_pasv_accept(sess->pasv.listen_fd, PASV_ACCEPT_MS);
    pasv_close(sess);

    if (data_fd < 0) {
        close(src_fd);
        ftp_log(LOG_ERROR, "pasv accept failed: %s", strerror(errno));
        return ftp_reply_send(sess->ctrl_fd, 425,
            "Can't open data connection");
    }

    rc = ftp_data_copy(data_fd, src_fd, DATA_XFER_MS);
    close(data_fd);
    close(src_fd);

    if (rc < 0) {
        ftp_log(LOG_ERROR, "RETR %s failed: %s", resolved,
            strerror(errno));
        return ftp_reply_send(sess->ctrl_fd, 451,
            "Requested action aborted");
    }

    ftp_log(LOG_INFO, "RETR %s ok", resolved);
    return ftp_reply_send(sess->ctrl_fd, 226, "Transfer complete");
}

static int
cmd_stor(ftp_session_t *sess, const ftp_cmd_t *cmd,
    const ftp_config_t *config)
{
    char resolved[PATH_BUF_SIZE];
    int dst_fd;
    int data_fd;
    int rc;

    if (!sess->pasv_armed)
        return ftp_reply_send(sess->ctrl_fd, 425,
            "Use PASV first");

    if (cmd->arg_len == 0) {
        pasv_close(sess);
        return ftp_reply_send(sess->ctrl_fd, 501,
            "Syntax error in parameters");
    }

    if (ftp_path_resolve(config->root, sess->cwd, cmd->arg,
            resolved) < 0) {
        pasv_close(sess);
        return ftp_reply_send(sess->ctrl_fd, 550,
            "Requested action not taken");
    }

    /*
     * STOR overwrites existing files.  This is the documented policy.
     * Anonymous uploads are not enabled by design (auth required).
     */
    dst_fd = open(resolved, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (dst_fd < 0) {
        pasv_close(sess);
        return ftp_reply_send(sess->ctrl_fd, 550,
            "Failed to open file for writing");
    }

    if (ftp_reply_send(sess->ctrl_fd, 150,
            "Opening data connection") < 0) {
        close(dst_fd);
        pasv_close(sess);
        return -1;
    }

    data_fd = ftp_pasv_accept(sess->pasv.listen_fd, PASV_ACCEPT_MS);
    pasv_close(sess);

    if (data_fd < 0) {
        close(dst_fd);
        ftp_log(LOG_ERROR, "pasv accept failed: %s", strerror(errno));
        return ftp_reply_send(sess->ctrl_fd, 425,
            "Can't open data connection");
    }

    rc = ftp_data_copy(dst_fd, data_fd, DATA_XFER_MS);
    close(data_fd);
    close(dst_fd);

    if (rc < 0) {
        ftp_log(LOG_ERROR, "STOR %s failed: %s", resolved,
            strerror(errno));
        /*
         * Partial upload left in place; consistent with documented
         * conservative policy (don't silently delete).
         */
        return ftp_reply_send(sess->ctrl_fd, 451,
            "Requested action aborted");
    }

    ftp_log(LOG_INFO, "STOR %s ok", resolved);
    return ftp_reply_send(sess->ctrl_fd, 226, "Transfer complete");
}

/* ------------------------------------------------------------------ */
/* Command dispatch                                                     */
/* ------------------------------------------------------------------ */

static int
dispatch(ftp_session_t *sess, const ftp_cmd_t *cmd,
    const ftp_config_t *config)
{
    const char *v;
    bool authed;

    v = cmd->verb;
    authed = (sess->auth == SESSION_AUTHED);

    /* pre-auth allowed set */
    if (strcmp(v, "USER") == 0)
        return cmd_user(sess, cmd, config);
    if (strcmp(v, "PASS") == 0)
        return cmd_pass(sess, cmd, config);
    if (strcmp(v, "QUIT") == 0)
        return cmd_quit(sess, cmd, config);
    if (strcmp(v, "NOOP") == 0)
        return cmd_noop(sess, cmd, config);

    /* everything else requires auth */
    if (!authed)
        return ftp_reply_send(sess->ctrl_fd, 530, "Not logged in");

    if (strcmp(v, "SYST") == 0)
        return cmd_syst(sess, cmd, config);
    if (strcmp(v, "TYPE") == 0)
        return cmd_type(sess, cmd, config);
    if (strcmp(v, "PWD") == 0 || strcmp(v, "XPWD") == 0)
        return cmd_pwd(sess, cmd, config);
    if (strcmp(v, "CWD") == 0 || strcmp(v, "XCWD") == 0)
        return cmd_cwd(sess, cmd, config);
    if (strcmp(v, "CDUP") == 0 || strcmp(v, "XCUP") == 0)
        return cmd_cdup(sess, cmd, config);
    if (strcmp(v, "MKD") == 0 || strcmp(v, "XMKD") == 0)
        return cmd_mkd(sess, cmd, config);
    if (strcmp(v, "PASV") == 0)
        return cmd_pasv(sess, cmd, config);
    if (strcmp(v, "LIST") == 0)
        return cmd_list_like(sess, cmd, config, false);
    if (strcmp(v, "NLST") == 0)
        return cmd_list_like(sess, cmd, config, true);
    if (strcmp(v, "RETR") == 0)
        return cmd_retr(sess, cmd, config);
    if (strcmp(v, "STOR") == 0)
        return cmd_stor(sess, cmd, config);

    return ftp_reply_send(sess->ctrl_fd, 502,
        "Command not implemented");
}

/* ------------------------------------------------------------------ */
/* Session runner                                                       */
/* ------------------------------------------------------------------ */

void
ftp_session_run(int ctrl_fd, const ftp_config_t *config)
{
    ftp_session_t sess;
    ftp_cmd_t cmd;
    int rc;

    memset(&sess, 0, sizeof(sess));
    sess.ctrl_fd    = ctrl_fd;
    sess.auth       = SESSION_CONNECTED;
    sess.xfer_type  = TYPE_ASCII; /* RFC 959 default */
    sess.pasv_armed = false;
    sess.pasv.listen_fd = -1;
    ftp_strlcpy(sess.cwd, "/", sizeof(sess.cwd));
    ftp_cmd_parser_init(&sess.parser);

    ftp_log(LOG_INFO, "session start");

    if (ftp_reply_send(ctrl_fd, 220, "FTP server ready") < 0)
        goto done;

    for (;;) {
        rc = read_command(&sess, &cmd, config->ctrl_timeout_ms);
        if (rc < 0) {
            if (errno == ETIMEDOUT)
                ftp_reply_send(ctrl_fd, 421,
                    "Service not available, closing control connection");
            break;
        }

        rc = dispatch(&sess, &cmd, config);
        if (rc < 0)
            break;

        if (sess.auth == SESSION_CLOSING)
            break;
    }

done:
    pasv_close(&sess);
    close(ctrl_fd);
    ftp_log(LOG_INFO, "session end");
    exit(0);
}
