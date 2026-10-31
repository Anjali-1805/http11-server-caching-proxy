/**
 * @file file_handler.c
 * @brief Implementation of static file serving and path safety checks.
 */

#include "file_handler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <strings.h>

static char g_canonical_doc_root[PATH_MAX] = {0};
static size_t g_canonical_doc_root_len = 0;

bool file_handler_init(const char *doc_root)
{
    if (!doc_root || strlen(doc_root) == 0) {
        doc_root = "./www";
    }

    if (!realpath(doc_root, g_canonical_doc_root)) {
        perror("file_handler_init: realpath failed for doc_root");
        return false;
    }

    struct stat st;
    if (stat(g_canonical_doc_root, &st) != 0 || !S_ISDIR(st.st_mode)) {
        fprintf(stderr, "file_handler_init: doc_root is not a directory: %s\n", g_canonical_doc_root);
        return false;
    }

    g_canonical_doc_root_len = strlen(g_canonical_doc_root);
    return true;
}

const char *file_handler_get_root(void)
{
    return g_canonical_doc_root;
}

const char *file_handler_get_mime_type(const char *path)
{
    if (!path) return "application/octet-stream";

    const char *dot = strrchr(path, '.');
    if (!dot) return "application/octet-stream";

    if (strcasecmp(dot, ".html") == 0 || strcasecmp(dot, ".htm") == 0) {
        return "text/html; charset=utf-8";
    } else if (strcasecmp(dot, ".css") == 0) {
        return "text/css";
    } else if (strcasecmp(dot, ".js") == 0 || strcasecmp(dot, ".mjs") == 0) {
        return "application/javascript";
    } else if (strcasecmp(dot, ".txt") == 0) {
        return "text/plain; charset=utf-8";
    } else if (strcasecmp(dot, ".png") == 0) {
        return "image/png";
    } else if (strcasecmp(dot, ".jpg") == 0 || strcasecmp(dot, ".jpeg") == 0) {
        return "image/jpeg";
    } else if (strcasecmp(dot, ".gif") == 0) {
        return "image/gif";
    } else if (strcasecmp(dot, ".pdf") == 0) {
        return "application/pdf";
    } else if (strcasecmp(dot, ".ico") == 0) {
        return "image/x-icon";
    } else if (strcasecmp(dot, ".svg") == 0) {
        return "image/svg+xml";
    } else if (strcasecmp(dot, ".json") == 0) {
        return "application/json";
    } else if (strcasecmp(dot, ".xml") == 0) {
        return "application/xml";
    }

    return "application/octet-stream";
}

/**
 * @brief Normalize logical path components to protect against traversal.
 *
 * @param path Input URI path.
 * @param normalized Output normalized path buffer.
 * @param max_len Size of output buffer.
 * @return true if valid safe path, false if directory traversal attempted.
 */
static bool normalize_path(const char *path, char *normalized, size_t max_len)
{
    if (!path || !normalized || max_len == 0) return false;

    char copy[4096];
    strncpy(copy, path, sizeof(copy) - 1);
    copy[sizeof(copy) - 1] = '\0';

    char *segments[256];
    int depth = 0;

    char *saveptr = NULL;
    char *token = strtok_r(copy, "/", &saveptr);
    while (token) {
        if (strcmp(token, ".") == 0 || strlen(token) == 0) {
            /* Current directory - no-op */
        } else if (strcmp(token, "..") == 0) {
            /* Parent directory */
            if (depth > 0) {
                depth--;
            } else {
                /* Attempt to escape root! */
                return false;
            }
        } else {
            if (depth < 255) {
                segments[depth++] = token;
            } else {
                return false; /* Path too deep */
            }
        }
        token = strtok_r(NULL, "/", &saveptr);
    }

    normalized[0] = '/';
    normalized[1] = '\0';
    size_t cur_len = 1;

    for (int i = 0; i < depth; i++) {
        size_t seg_len = strlen(segments[i]);
        if (cur_len + seg_len + 2 >= max_len) {
            return false;
        }
        if (cur_len > 1) {
            normalized[cur_len++] = '/';
        }
        memcpy(normalized + cur_len, segments[i], seg_len);
        cur_len += seg_len;
        normalized[cur_len] = '\0';
    }

    return true;
}

file_status_t file_handler_resolve_and_open(const char *req_path, file_info_t *out_info)
{
    if (!req_path || !out_info) {
        return FILE_ERROR;
    }

    memset(out_info, 0, sizeof(*out_info));
    out_info->file_fd = -1;

    if (g_canonical_doc_root_len == 0) {
        if (!file_handler_init("./www")) {
            return FILE_ERROR;
        }
    }

    char normalized_rel_path[4096];
    if (!normalize_path(req_path, normalized_rel_path, sizeof(normalized_rel_path))) {
        /* Traversal attempt detected */
        return FILE_FORBIDDEN;
    }

    /* Build candidate absolute path */
    char candidate_path[PATH_MAX * 2];
    if (strcmp(normalized_rel_path, "/") == 0) {
        snprintf(candidate_path, sizeof(candidate_path), "%s/index.html", g_canonical_doc_root);
    } else {
        snprintf(candidate_path, sizeof(candidate_path), "%s%s", g_canonical_doc_root, normalized_rel_path);
    }

    /* Check if candidate is a directory; if so, map to index.html within it */
    struct stat st;
    if (stat(candidate_path, &st) == 0 && S_ISDIR(st.st_mode)) {
        size_t clen = strlen(candidate_path);
        if (clen + 12 < sizeof(candidate_path)) {
            strcat(candidate_path, "/index.html");
        }
    }

    /* Resolve canonical realpath to ensure no symlink escaping */
    char resolved[PATH_MAX];
    if (!realpath(candidate_path, resolved)) {
        if (errno == ENOENT) {
            return FILE_NOT_FOUND;
        } else if (errno == EACCES) {
            return FILE_FORBIDDEN;
        }
        return FILE_NOT_FOUND;
    }

    /* Security check: verify that resolved path begins with canonical doc root */
    if (strncmp(resolved, g_canonical_doc_root, g_canonical_doc_root_len) != 0) {
        return FILE_FORBIDDEN;
    }

    /* Ensure it doesn't match a sibling prefix like /www2 when root is /www */
    char char_after_root = resolved[g_canonical_doc_root_len];
    if (char_after_root != '/' && char_after_root != '\0') {
        return FILE_FORBIDDEN;
    }

    /* Open the file */
    int fd = open(resolved, O_RDONLY);
    if (fd < 0) {
        if (errno == ENOENT) return FILE_NOT_FOUND;
        if (errno == EACCES) return FILE_FORBIDDEN;
        return FILE_ERROR;
    }

    if (fstat(fd, &st) != 0) {
        close(fd);
        return FILE_ERROR;
    }

    if (!S_ISREG(st.st_mode)) {
        /* Not a regular file (e.g. directory, pipe, device) */
        close(fd);
        return FILE_FORBIDDEN;
    }

    strncpy(out_info->resolved_path, resolved, sizeof(out_info->resolved_path) - 1);
    out_info->file_fd = fd;
    out_info->file_size = st.st_size;
    out_info->mime_type = file_handler_get_mime_type(resolved);

    return FILE_OK;
}

void file_info_close(file_info_t *info)
{
    if (info && info->file_fd >= 0) {
        close(info->file_fd);
        info->file_fd = -1;
    }
}
