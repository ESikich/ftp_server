/* main.c - FTP server listener, accept loop, and startup. */

#include "ftp_server.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netinet/in.h>
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
        "usage: %s [-c CONFIG]"
        " [-r ROOT -u USER -p HASH]"
        " [-b ADDR] [-P PORT]"
        " [-m PASV_MIN] [-M PASV_MAX]\n",
        prog);
}

static int
parse_u16(const char *s, uint16_t *out, const char *what)
{
    long v;
    char *end;

    errno = 0;
    v = strtol(s, &end, 10);
    if (errno != 0 || *end != '\0' || v < 1 || v > 65535) {
        fprintf(stderr, "invalid %s: %s\n", what, s);
        return -1;
    }

    *out = (uint16_t)v;
    return 0;
}

static int
parse_int(const char *s, int min, int max, int *out, const char *what)
{
    long v;
    char *end;

    errno = 0;
    v = strtol(s, &end, 10);
    if (errno != 0 || *end != '\0' || v < min || v > max) {
        fprintf(stderr, "invalid %s: %s\n", what, s);
        return -1;
    }

    *out = (int)v;
    return 0;
}

static int
parse_perm_name(const char *s, unsigned *out)
{
    if (strcmp(s, "read") == 0) {
        *out = FTP_PERM_READ;
        return 0;
    }
    if (strcmp(s, "write") == 0) {
        *out = FTP_PERM_WRITE;
        return 0;
    }
    if (strcmp(s, "delete") == 0) {
        *out = FTP_PERM_DELETE;
        return 0;
    }
    if (strcmp(s, "mkdir") == 0) {
        *out = FTP_PERM_MKDIR;
        return 0;
    }
    return -1;
}

static char *
trim_ws(char *s)
{
    char *end;

    while (*s != '\0' && isspace((unsigned char)*s))
        s++;
    end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1]))
        *--end = '\0';
    return s;
}

static void
strip_comment(char *s)
{
    bool in_quote;

    in_quote = false;
    for (; *s != '\0'; s++) {
        if (*s == '"')
            in_quote = !in_quote;
        else if (*s == '#' && !in_quote) {
            *s = '\0';
            return;
        }
    }
}

static int
parse_toml_string(const char *value, char *out, size_t out_size)
{
    size_t len;

    value = trim_ws((char *)value);
    len = strlen(value);
    if (len < 2 || value[0] != '"' || value[len - 1] != '"')
        return -1;

    value++;
    len -= 2;
    if (len >= out_size)
        return -1;
    memcpy(out, value, len);
    out[len] = '\0';
    return 0;
}

static int
parse_toml_uint16(const char *value, uint16_t *out, const char *what)
{
    int parsed;

    if (parse_int(trim_ws((char *)value), 1, 65535, &parsed, what) < 0)
        return -1;
    *out = (uint16_t)parsed;
    return 0;
}

static int
parse_toml_int(const char *value, int min, int max, int *out,
    const char *what)
{
    return parse_int(trim_ws((char *)value), min, max, out, what);
}

static int
parse_toml_perms(const char *value, unsigned *out)
{
    char buf[256];
    char *s;
    unsigned perms;

    value = trim_ws((char *)value);
    if (ftp_strlcpy(buf, value, sizeof(buf)) >= sizeof(buf))
        return -1;

    s = trim_ws(buf);
    if (*s != '[')
        return -1;
    s++;
    perms = 0;

    for (;;) {
        unsigned bit;
        char *start;
        char *end;
        char tmp[32];

        s = trim_ws(s);
        if (*s == ']') {
            *out = perms;
            return 0;
        }
        if (*s != '"')
            return -1;
        start = ++s;
        while (*s != '\0' && *s != '"')
            s++;
        if (*s != '"')
            return -1;
        end = s;
        *end = '\0';
        if (ftp_strlcpy(tmp, start, sizeof(tmp)) >= sizeof(tmp))
            return -1;
        if (parse_perm_name(tmp, &bit) < 0)
            return -1;
        perms |= bit;
        s = end + 1;
        s = trim_ws(s);
        if (*s == ',') {
            s++;
            continue;
        }
        if (*s == ']') {
            *out = perms;
            return 0;
        }
        return -1;
    }
}

static int
append_user(ftp_config_t *config, const char *name, const char *hash,
    const char *home, unsigned perms)
{
    ftp_user_t *user;

    if (config->user_count >= FTP_MAX_USERS) {
        fprintf(stderr, "too many users in configuration\n");
        return -1;
    }
    if (home[0] == '/') {
        fprintf(stderr, "user home must be relative to the export root: %s\n",
            home);
        return -1;
    }

    user = &config->users[config->user_count];
    memset(user, 0, sizeof(*user));

    if (ftp_strlcpy(user->username, name, sizeof(user->username)) >=
        sizeof(user->username)) {
        fprintf(stderr, "username too long: %s\n", name);
        return -1;
    }
    if (ftp_strlcpy(user->password_hash, hash,
            sizeof(user->password_hash)) >= sizeof(user->password_hash)) {
        fprintf(stderr, "password hash too long for user: %s\n", name);
        return -1;
    }
    if (ftp_strlcpy(user->home, home, sizeof(user->home)) >=
        sizeof(user->home)) {
        fprintf(stderr, "home path too long for user: %s\n", name);
        return -1;
    }
    user->perms = perms;
    config->user_count++;
    return 0;
}

static int
load_config_file(const char *path, ftp_config_t *config)
{
    FILE *fp;
    char line[1024];
    char *key;
    char *value;
    char *eq;
    char user_name[128];
    char user_hash[256];
    char user_home[PATH_BUF_SIZE];
    unsigned user_perms;
    bool in_user;
    bool have_name;
    bool have_hash;
    bool have_home;
    bool have_perms;
    int line_no;

    fp = fopen(path, "r");
    if (fp == NULL)
        return -1;

    in_user = false;
    have_name = false;
    have_hash = false;
    have_home = false;
    have_perms = false;
    user_name[0] = '\0';
    user_hash[0] = '\0';
    user_home[0] = '\0';
    user_perms = 0;

    line_no = 0;
    while (fgets(line, sizeof(line), fp) != NULL) {
        line_no++;
        if (strchr(line, '\n') == NULL && !feof(fp)) {
            fprintf(stderr, "%s:%d: line too long\n", path, line_no);
            fclose(fp);
            errno = ENAMETOOLONG;
            return -1;
        }

        strip_comment(line);
        key = trim_ws(line);
        if (*key == '\0')
            continue;

        if (strcmp(key, "[[users]]") == 0) {
            if (in_user) {
                if (!have_name || !have_hash || !have_home || !have_perms ||
                    user_perms == 0) {
                    fprintf(stderr, "%s:%d: incomplete user block\n",
                        path, line_no);
                    fclose(fp);
                    errno = EINVAL;
                    return -1;
                }
                if (append_user(config, user_name, user_hash, user_home,
                        user_perms) < 0) {
                    fprintf(stderr, "%s:%d: invalid user entry\n",
                        path, line_no);
                    fclose(fp);
                    return -1;
                }
            }

            in_user = true;
            have_name = false;
            have_hash = false;
            have_home = false;
            have_perms = false;
            user_name[0] = '\0';
            user_hash[0] = '\0';
            user_home[0] = '\0';
            user_perms = 0;
            continue;
        }

        eq = strchr(key, '=');
        if (eq == NULL) {
            fprintf(stderr, "%s:%d: expected key = value\n", path, line_no);
            fclose(fp);
            errno = EINVAL;
            return -1;
        }
        *eq = '\0';
        value = trim_ws(eq + 1);
        key = trim_ws(key);

        if (!in_user) {
            if (strcmp(key, "root") == 0) {
                if (parse_toml_string(value, config->root,
                        sizeof(config->root)) < 0) {
                    fprintf(stderr, "%s:%d: invalid root\n", path, line_no);
                    fclose(fp);
                    errno = EINVAL;
                    return -1;
                }
                continue;
            }
            if (strcmp(key, "bind") == 0) {
                if (parse_toml_string(value, config->bind_addr,
                        sizeof(config->bind_addr)) < 0) {
                    fprintf(stderr, "%s:%d: invalid bind address\n",
                        path, line_no);
                    fclose(fp);
                    errno = EINVAL;
                    return -1;
                }
                continue;
            }
            if (strcmp(key, "port") == 0) {
                if (parse_toml_uint16(value, &config->port, "port") < 0) {
                    fprintf(stderr, "%s:%d: invalid port\n", path, line_no);
                    fclose(fp);
                    return -1;
                }
                continue;
            }
            if (strcmp(key, "pasv_min") == 0) {
                if (parse_toml_uint16(value, &config->pasv_port_min,
                        "pasv_min") < 0) {
                    fprintf(stderr, "%s:%d: invalid pasv_min\n",
                        path, line_no);
                    fclose(fp);
                    return -1;
                }
                continue;
            }
            if (strcmp(key, "pasv_max") == 0) {
                if (parse_toml_uint16(value, &config->pasv_port_max,
                        "pasv_max") < 0) {
                    fprintf(stderr, "%s:%d: invalid pasv_max\n",
                        path, line_no);
                    fclose(fp);
                    return -1;
                }
                continue;
            }
            if (strcmp(key, "max_sessions") == 0) {
                if (parse_toml_int(value, 1, 1000000, &config->max_sessions,
                        "max_sessions") < 0) {
                    fprintf(stderr, "%s:%d: invalid max_sessions\n",
                        path, line_no);
                    fclose(fp);
                    return -1;
                }
                continue;
            }
            fprintf(stderr, "%s:%d: unknown top-level key '%s'\n",
                path, line_no, key);
            fclose(fp);
            errno = EINVAL;
            return -1;
        }

        if (strcmp(key, "name") == 0) {
            if (parse_toml_string(value, user_name, sizeof(user_name)) < 0) {
                fprintf(stderr, "%s:%d: invalid user name\n", path, line_no);
                fclose(fp);
                errno = EINVAL;
                return -1;
            }
            have_name = true;
            continue;
        }
        if (strcmp(key, "hash") == 0) {
            if (parse_toml_string(value, user_hash, sizeof(user_hash)) < 0) {
                fprintf(stderr, "%s:%d: invalid password hash\n",
                    path, line_no);
                fclose(fp);
                errno = EINVAL;
                return -1;
            }
            have_hash = true;
            continue;
        }
        if (strcmp(key, "home") == 0) {
            if (parse_toml_string(value, user_home, sizeof(user_home)) < 0) {
                fprintf(stderr, "%s:%d: invalid home path\n", path, line_no);
                fclose(fp);
                errno = EINVAL;
                return -1;
            }
            have_home = true;
            continue;
        }
        if (strcmp(key, "perms") == 0) {
            if (parse_toml_perms(value, &user_perms) < 0) {
                fprintf(stderr, "%s:%d: invalid permissions\n",
                    path, line_no);
                fclose(fp);
                errno = EINVAL;
                return -1;
            }
            have_perms = true;
            continue;
        }

        fprintf(stderr, "%s:%d: unknown user key '%s'\n",
            path, line_no, key);
        fclose(fp);
        errno = EINVAL;
        return -1;
    }

    if (in_user) {
        if (!have_name || !have_hash || !have_home || !have_perms ||
            user_perms == 0) {
            fprintf(stderr, "%s: incomplete user block at EOF\n", path);
            fclose(fp);
            errno = EINVAL;
            return -1;
        }
        if (append_user(config, user_name, user_hash, user_home,
                user_perms) < 0) {
            fprintf(stderr, "%s: invalid user entry at EOF\n", path);
            fclose(fp);
            return -1;
        }
    }

    fclose(fp);
    return 0;
}

static int
finalize_config(ftp_config_t *config)
{
    char *resolved_root;
    char resolved_home[PATH_BUF_SIZE];

    if (config->root[0] == '\0') {
        fprintf(stderr, "root is required\n");
        errno = EINVAL;
        return -1;
    }
    if (config->root[0] != '/') {
        fprintf(stderr, "root must be an absolute path: %s\n", config->root);
        errno = EINVAL;
        return -1;
    }
    resolved_root = realpath(config->root, NULL);
    if (resolved_root == NULL)
        return -1;
    if (ftp_strlcpy(config->root, resolved_root, sizeof(config->root)) >=
        sizeof(config->root)) {
        free(resolved_root);
        errno = ENAMETOOLONG;
        return -1;
    }
    free(resolved_root);

    if (config->user_count == 0) {
        fprintf(stderr, "at least one user is required\n");
        errno = EINVAL;
        return -1;
    }

    for (size_t i = 0; i < config->user_count; i++) {
        ftp_user_t *user = &config->users[i];

        if (user->username[0] == '\0' || user->password_hash[0] == '\0' ||
            user->home[0] == '\0') {
            fprintf(stderr, "user entry %zu is incomplete\n", i + 1);
            errno = EINVAL;
            return -1;
        }

        for (size_t j = i + 1; j < config->user_count; j++) {
            if (strcmp(user->username, config->users[j].username) == 0) {
                fprintf(stderr, "duplicate user: %s\n", user->username);
                errno = EINVAL;
                return -1;
            }
        }

        if (ftp_path_resolve(config->root, "/", user->home,
                resolved_home) < 0) {
            fprintf(stderr, "invalid home for user %s: %s\n",
                user->username, strerror(errno));
            return -1;
        }
        if (ftp_strlcpy(user->home, resolved_home, sizeof(user->home)) >=
            sizeof(user->home)) {
            errno = ENAMETOOLONG;
            return -1;
        }
    }

    return 0;
}

static void
populate_single_user(ftp_config_t *config, const char *root,
    const char *user, const char *hash)
{
    memset(config, 0, sizeof(*config));
    ftp_strlcpy(config->bind_addr, "0.0.0.0", sizeof(config->bind_addr));
    ftp_strlcpy(config->root, root, sizeof(config->root));
    config->port           = 2121;
    config->ctrl_timeout_ms = CTRL_IDLE_MS;
    config->pasv_port_min  = 50000;
    config->pasv_port_max  = 50100;
    config->max_sessions   = 16;
    config->user_count     = 1;

    ftp_strlcpy(config->users[0].username, user,
        sizeof(config->users[0].username));
    ftp_strlcpy(config->users[0].password_hash, hash,
        sizeof(config->users[0].password_hash));
    ftp_strlcpy(config->users[0].home, ".", sizeof(config->users[0].home));
    config->users[0].perms = FTP_PERM_READ | FTP_PERM_WRITE |
        FTP_PERM_DELETE | FTP_PERM_MKDIR;
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
    bool have_config;
    bool have_root;
    bool have_user;
    bool have_pass;
    char config_path[PATH_BUF_SIZE] = "server.conf.toml";
    char root_arg[PATH_BUF_SIZE];
    char user_arg[128];
    char hash_arg[256];
    char bind_arg[64];
    uint16_t port_arg;
    uint16_t pasv_min_arg;
    uint16_t pasv_max_arg;
    bool have_bind;
    bool have_port;
    bool have_pasv_min;
    bool have_pasv_max;

    memset(&config, 0, sizeof(config));
    config_defaults(&config);
    have_config = false;
    have_root = false;
    have_user = false;
    have_pass = false;
    have_bind = false;
    have_port = false;
    have_pasv_min = false;
    have_pasv_max = false;

    while ((opt = getopt(argc, argv, "c:r:u:p:b:P:m:M:")) != -1) {
        switch (opt) {
        case 'c':
            if (ftp_strlcpy(config_path, optarg,
                    sizeof(config_path)) >= sizeof(config_path))
                ftp_fatal("config path too long");
            have_config = true;
            break;
        case 'r':
            if (ftp_strlcpy(root_arg, optarg,
                    sizeof(root_arg)) >= sizeof(root_arg))
                ftp_fatal("root path too long");
            have_root = true;
            break;
        case 'u':
            if (ftp_strlcpy(user_arg, optarg,
                    sizeof(user_arg)) >= sizeof(user_arg))
                ftp_fatal("username too long");
            have_user = true;
            break;
        case 'p':
            if (ftp_strlcpy(hash_arg, optarg,
                    sizeof(hash_arg)) >= sizeof(hash_arg))
                ftp_fatal("password hash too long");
            have_pass = true;
            break;
        case 'b':
            if (ftp_strlcpy(bind_arg, optarg,
                    sizeof(bind_arg)) >= sizeof(bind_arg))
                ftp_fatal("bind address too long");
            have_bind = true;
            break;
        case 'P': {
            if (parse_u16(optarg, &port_arg, "port") < 0)
                return 2;
            have_port = true;
            break;
        }
        case 'm': {
            if (parse_u16(optarg, &pasv_min_arg, "pasv_min") < 0)
                return 2;
            have_pasv_min = true;
            break;
        }
        case 'M': {
            if (parse_u16(optarg, &pasv_max_arg, "pasv_max") < 0)
                return 2;
            have_pasv_max = true;
            break;
        }
        default:
            usage(argv[0]);
            return 2;
        }
    }

    if (have_config || !(have_root || have_user || have_pass)) {
        if (load_config_file(config_path, &config) < 0) {
            ftp_fatal("failed to load config '%s': %s",
                config_path, strerror(errno));
        }
    } else {
        if (!have_root || !have_user || !have_pass) {
            usage(argv[0]);
            return 2;
        }
        populate_single_user(&config, root_arg, user_arg, hash_arg);
    }

    if (have_bind)
        ftp_strlcpy(config.bind_addr, bind_arg, sizeof(config.bind_addr));
    if (have_port)
        config.port = port_arg;
    if (have_pasv_min)
        config.pasv_port_min = pasv_min_arg;
    if (have_pasv_max)
        config.pasv_port_max = pasv_max_arg;

    if (config.pasv_port_min > config.pasv_port_max)
        ftp_fatal("pasv_min > pasv_max");

    if (finalize_config(&config) < 0)
        ftp_fatal("invalid configuration");

    if (ftp_server_init(&srv, &config) < 0)
        ftp_fatal("server init failed: %s", strerror(errno));

    ftp_server_run(&srv);
}
