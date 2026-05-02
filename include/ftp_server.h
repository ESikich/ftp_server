/* ftp_server.h - public types and interface for the FTP server. */

#ifndef FTP_SERVER_H
#define FTP_SERVER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Constants                                                           */
/* ------------------------------------------------------------------ */

enum {
    CMD_LINE_MAX     = 512,    /* max bytes in one command line */
    CTRL_BUF_SIZE    = 4096,   /* control channel read buffer */
    PATH_BUF_SIZE    = 1024,   /* max resolved path length */
    PASV_REPLY_SIZE  = 64,     /* "227 Entering Passive Mode (...)\r\n" */
    LIST_LINE_MAX    = 256,    /* one ls-style listing line */
    FTP_MAX_USERS    = 32,     /* max configured user accounts */

    CTRL_IDLE_MS     = 300000, /* 5 min idle control timeout */
    DATA_XFER_MS     = 30000,  /* 30 sec data transfer timeout */
    PASV_ACCEPT_MS   = 10000,  /* 10 sec to accept passive connection */
};

/* ------------------------------------------------------------------ */
/* Configuration (immutable after startup)                             */
/* ------------------------------------------------------------------ */

typedef enum {
    FTP_PERM_READ   = 1u << 0,
    FTP_PERM_WRITE  = 1u << 1,
    FTP_PERM_DELETE = 1u << 2,
    FTP_PERM_MKDIR  = 1u << 3,
} ftp_perm_t;

typedef struct ftp_user_t {
    char     username[128];
    char     password_hash[256];
    char     home[PATH_BUF_SIZE];    /* absolute base path after validation */
    unsigned perms;
} ftp_user_t;

typedef struct {
    char     bind_addr[64];
    uint16_t port;
    char     root[PATH_BUF_SIZE];    /* export root, absolute */
    int      ctrl_timeout_ms;
    uint16_t pasv_port_min;
    uint16_t pasv_port_max;
    int      max_sessions;
    size_t   user_count;
    ftp_user_t users[FTP_MAX_USERS];
} ftp_config_t;

/* ------------------------------------------------------------------ */
/* Command parser                                                       */
/* ------------------------------------------------------------------ */

typedef struct {
    char   verb[16];
    char   arg[CMD_LINE_MAX];
    size_t arg_len;
} ftp_cmd_t;

typedef struct {
    char   buf[CTRL_BUF_SIZE];
    size_t len;
    bool   saw_cr;
} ftp_cmd_parser_t;

/* ------------------------------------------------------------------ */
/* Session state                                                        */
/* ------------------------------------------------------------------ */

typedef enum {
    SESSION_CONNECTED,   /* greeting sent, no USER yet */
    SESSION_NEED_PASS,   /* USER accepted, waiting for PASS */
    SESSION_AUTHED,      /* logged in */
    SESSION_CLOSING,     /* QUIT received or fatal error */
} session_auth_t;

typedef enum {
    TYPE_ASCII,
    TYPE_IMAGE,
} xfer_type_t;

typedef struct {
    int      listen_fd;
    uint16_t port;
    char     pasv_arg[32]; /* comma-separated h1,h2,h3,h4,p1,p2 */
} pasv_t;

typedef struct {
    char   data[CTRL_BUF_SIZE];
    size_t len;
} ftp_ctrl_buf_t;

typedef struct {
    int              ctrl_fd;
    session_auth_t   auth;
    char             pending_user[128];
    const ftp_user_t *user;
    char             home_root[PATH_BUF_SIZE];
    char             cwd[PATH_BUF_SIZE]; /* logical path, starts with "/" */
    xfer_type_t      xfer_type;
    pasv_t           pasv;
    bool             pasv_armed;
    ftp_cmd_parser_t parser;
    ftp_ctrl_buf_t   ctrl;    /* unconsumed bytes from ctrl_fd */
} ftp_session_t;

/* ------------------------------------------------------------------ */
/* Server                                                               */
/* ------------------------------------------------------------------ */

typedef struct {
    ftp_config_t config;
    int          listen_fd;
} ftp_server_t;

/* ------------------------------------------------------------------ */
/* Logging                                                              */
/* ------------------------------------------------------------------ */

typedef enum {
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR,
} log_level_t;

void ftp_log(log_level_t level, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

_Noreturn void ftp_fatal(const char *fmt, ...)
    __attribute__((format(printf, 1, 2)));

/* ------------------------------------------------------------------ */
/* Command parser API                                                   */
/* ------------------------------------------------------------------ */

void ftp_cmd_parser_init(ftp_cmd_parser_t *p);

/*
 * Feed bytes into the parser.  Returns 1 if a complete command was
 * extracted into *cmd, 0 if more data is needed, -1 on protocol error.
 * *consumed is set to the number of bytes consumed from buf.
 */
int ftp_cmd_parser_feed(ftp_cmd_parser_t *p, const char *buf, size_t len,
    size_t *consumed, ftp_cmd_t *cmd);

/* ------------------------------------------------------------------ */
/* Reply helpers                                                        */
/* ------------------------------------------------------------------ */

int ftp_reply_send(int fd, int code, const char *text);
int ftp_reply_sendf(int fd, int code, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

/* ------------------------------------------------------------------ */
/* Path resolution                                                      */
/* ------------------------------------------------------------------ */

/*
 * Resolve client_path (absolute or relative to cwd) under root.
 * Writes the absolute real path into out (PATH_BUF_SIZE bytes).
 * Returns 0 on success, -1 if path escapes root or is otherwise invalid.
 */
int ftp_path_resolve(const char *root, const char *cwd,
    const char *client_path, char *out);

/* Normalize a logical slash-separated path in place. */
void ftp_path_normalize(char *path);

/* ------------------------------------------------------------------ */
/* Passive data channel                                                 */
/* ------------------------------------------------------------------ */

/*
 * Open a passive listening socket on a free port in [min_port, max_port].
 * Writes the bound port into *port_out.
 * Returns the listen fd on success, -1 on failure.
 */
int ftp_pasv_listen(uint16_t min_port, uint16_t max_port,
    uint16_t *port_out);

/*
 * Accept one connection on pasv_listen_fd within timeout_ms.
 * Returns the accepted data fd, or -1 on failure/timeout.
 */
int ftp_pasv_accept(int pasv_listen_fd, int timeout_ms);

/*
 * Stream all of src_fd into dst_fd until EOF or error.
 * Returns 0 on success, -1 on error.
 */
int ftp_data_copy(int dst_fd, int src_fd, int timeout_ms);

/* ------------------------------------------------------------------ */
/* Session runner                                                       */
/* ------------------------------------------------------------------ */

/*
 * Run one FTP session on ctrl_fd using config.
 * Never returns on success (the child process exits).
 */
_Noreturn void ftp_session_run(int ctrl_fd, const ftp_config_t *config);

/* ------------------------------------------------------------------ */
/* Server                                                               */
/* ------------------------------------------------------------------ */

int  ftp_server_init(ftp_server_t *srv, const ftp_config_t *config);
_Noreturn void ftp_server_run(ftp_server_t *srv);

/* ------------------------------------------------------------------ */
/* Compat (strlcpy not in glibc < 2.38)                                */
/* ------------------------------------------------------------------ */

size_t ftp_strlcpy(char *dst, const char *src, size_t size);

#endif /* FTP_SERVER_H */
