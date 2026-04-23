/* ftp_cmd.c - incremental FTP command line parser. */

#include "ftp_server.h"

#include <ctype.h>
#include <errno.h>
#include <string.h>

void
ftp_cmd_parser_init(ftp_cmd_parser_t *p)
{
    p->len = 0;
    p->saw_cr = false;
}

/*
 * Extract verb and argument from a complete (CRLF-stripped) line.
 * Verb is up-cased; arg is the remainder with one leading space removed.
 */
static int
parse_line(const char *line, size_t len, ftp_cmd_t *cmd)
{
    size_t i;
    size_t vlen;

    /* skip leading spaces conservatively */
    i = 0;
    while (i < len && line[i] == ' ')
        i++;

    vlen = 0;
    while (i < len && line[i] != ' ' && line[i] != '\0') {
        if (vlen >= sizeof(cmd->verb) - 1) {
            errno = EPROTO;
            return -1;
        }
        cmd->verb[vlen++] = (char)toupper((unsigned char)line[i]);
        i++;
    }
    cmd->verb[vlen] = '\0';

    if (vlen == 0) {
        errno = EPROTO;
        return -1;
    }

    /* skip single space separator if present */
    if (i < len && line[i] == ' ')
        i++;

    cmd->arg_len = (int)(len - i);
    if ((size_t)cmd->arg_len >= sizeof(cmd->arg)) {
        errno = EMSGSIZE;
        return -1;
    }
    memcpy(cmd->arg, line + i, (size_t)cmd->arg_len);
    cmd->arg[cmd->arg_len] = '\0';

    /* strip trailing whitespace from arg */
    while (cmd->arg_len > 0 &&
        (cmd->arg[cmd->arg_len - 1] == ' ' ||
         cmd->arg[cmd->arg_len - 1] == '\t')) {
        cmd->arg[--cmd->arg_len] = '\0';
    }

    return 0;
}

int
ftp_cmd_parser_feed(ftp_cmd_parser_t *p, const char *buf, size_t len,
    size_t *consumed, ftp_cmd_t *cmd)
{
    size_t i;

    for (i = 0; i < len; i++) {
        unsigned char ch;

        ch = (unsigned char)buf[i];

        if (p->saw_cr) {
            p->saw_cr = false;
            if (ch == '\n') {
                /* CRLF: line is complete in p->buf[0..p->len-1]
                 * (CR was already stored; strip it) */
                if (p->len > 0 && p->buf[p->len - 1] == '\r')
                    p->len--;

                *consumed = i + 1;
                if (parse_line(p->buf, p->len, cmd) < 0)
                    return -1;
                p->len = 0;
                return 1;
            }
            /* bare CR followed by non-LF: treat CR as data */
        }

        if (ch == '\r') {
            if (p->len >= sizeof(p->buf) - 1) {
                errno = EMSGSIZE;
                return -1;
            }
            p->buf[p->len++] = (char)ch;
            p->saw_cr = true;
            continue;
        }

        if (ch == '\n') {
            /* bare LF: accept as line terminator */
            *consumed = i + 1;
            if (parse_line(p->buf, p->len, cmd) < 0)
                return -1;
            p->len = 0;
            p->saw_cr = false;
            return 1;
        }

        if (p->len >= sizeof(p->buf) - 1) {
            errno = EMSGSIZE;
            return -1;
        }
        p->buf[p->len++] = (char)ch;
    }

    *consumed = len;
    return 0;
}
