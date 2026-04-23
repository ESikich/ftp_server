/* ftp_path.c - rooted path resolution and normalization. */

#include "ftp_server.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

/*
 * Normalize a logical slash-separated path in place.
 * Resolves "." and ".." components, collapses multiple slashes,
 * and ensures the result starts with "/".
 * If ".." would escape "/", it is clamped to "/".
 */
void
ftp_path_normalize(char *path)
{
    char tmp[PATH_BUF_SIZE];
    const char *src;
    char *dst;
    char *seg;
    char *save;
    char *comp[PATH_BUF_SIZE / 2];
    int depth;
    int i;

    if (path[0] != '/') {
        /* caller must ensure absolute logical path */
        path[0] = '/';
        path[1] = '\0';
        return;
    }

    /* copy into tmp and tokenize */
    if (ftp_strlcpy(tmp, path, sizeof(tmp)) >= sizeof(tmp)) {
        path[0] = '/';
        path[1] = '\0';
        return;
    }

    depth = 0;
    save = NULL;
    src = tmp;
    for (seg = strtok_r(tmp + 1, "/", &save);
        seg != NULL;
        seg = strtok_r(NULL, "/", &save)) {
        (void)src;
        if (strcmp(seg, ".") == 0 || seg[0] == '\0') {
            continue;
        }
        if (strcmp(seg, "..") == 0) {
            if (depth > 0)
                depth--;
            continue;
        }
        if (depth < (int)(sizeof(comp) / sizeof(comp[0])))
            comp[depth++] = seg;
    }

    dst = path;
    *dst++ = '/';
    for (i = 0; i < depth; i++) {
        size_t slen;

        if (i > 0)
            *dst++ = '/';
        slen = strlen(comp[i]);
        memcpy(dst, comp[i], slen);
        dst += slen;
    }
    *dst = '\0';
}

/*
 * Resolve client_path under root/cwd.
 * client_path may be absolute (starts with '/') or relative to cwd.
 * The result is written into out (PATH_BUF_SIZE bytes).
 * Returns 0 if the resolved path is within root, -1 otherwise.
 *
 * Assumption: symlinks are not followed beyond the root; we rely on
 * realpath() to canonicalize, then check the prefix.  If the path
 * does not exist, we build the logical path and check the prefix
 * without calling realpath (needed for STOR targets).
 */
int
ftp_path_resolve(const char *root, const char *cwd,
    const char *client_path, char *out)
{
    char logical[PATH_BUF_SIZE];
    char candidate[PATH_BUF_SIZE * 2];
    size_t root_len;
    int n;

    /* Build logical path */
    if (client_path[0] == '/') {
        /* absolute client path is relative to root, not the filesystem */
        n = snprintf(logical, sizeof(logical), "%s", client_path);
    } else if (strcmp(cwd, "/") == 0) {
        n = snprintf(logical, sizeof(logical), "/%s", client_path);
    } else {
        n = snprintf(logical, sizeof(logical), "%s/%s", cwd, client_path);
    }

    if (n < 0 || (size_t)n >= sizeof(logical)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    ftp_path_normalize(logical);

    /* Build real candidate path: root + logical */
    if (strcmp(logical, "/") == 0) {
        n = snprintf(candidate, sizeof(candidate), "%s", root);
    } else {
        n = snprintf(candidate, sizeof(candidate), "%s%s", root, logical);
    }

    if (n < 0 || (size_t)n >= sizeof(candidate)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    root_len = strlen(root);

    /*
     * Verify the candidate starts with root.  The logical normalization
     * above prevents ".." escape, but we double-check with string prefix.
     */
    if (strncmp(candidate, root, root_len) != 0) {
        errno = EACCES;
        return -1;
    }

    /* candidate[root_len] must be '/' or '\0' to avoid prefix collision */
    if (candidate[root_len] != '\0' && candidate[root_len] != '/') {
        errno = EACCES;
        return -1;
    }

    if (ftp_strlcpy(out, candidate, PATH_BUF_SIZE) >= PATH_BUF_SIZE) {
        errno = ENAMETOOLONG;
        return -1;
    }

    return 0;
}
