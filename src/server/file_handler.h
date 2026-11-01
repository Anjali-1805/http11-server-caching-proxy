/**
 * @file file_handler.h
 * @brief Static file serving and safe path validation.
 */

#ifndef FILE_HANDLER_H
#define FILE_HANDLER_H

#include "../common/http_types.h"
#include <sys/stat.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FILE_OK = 0,
    FILE_NOT_FOUND = 404,
    FILE_FORBIDDEN = 403,
    FILE_ERROR = 500
} file_status_t;

typedef struct {
    char resolved_path[4096];
    int file_fd;
    off_t file_size;
    const char *mime_type;
} file_info_t;

/**
 * @brief Initialize file handler with the specified document root.
 *
 * @param doc_root Path to document root directory.
 * @return true on success, false if root directory cannot be accessed.
 */
bool file_handler_init(const char *doc_root);

/**
 * @brief Get the configured canonical document root path.
 */
const char *file_handler_get_root(void);

/**
 * @brief Detect MIME content type based on file path extension.
 *
 * @param path File path or filename.
 * @return MIME type string literal.
 */
const char *file_handler_get_mime_type(const char *path);

/**
 * @brief Safely resolve requested URI path and open file if accessible.
 *
 * Enforces strict directory traversal defense, verifies canonical paths,
 * maps directory requests to index.html, and ensures only regular files
 * inside the document root can be opened.
 *
 * @param req_path Requested decoded URI path.
 * @param out_info Pointer to file_info_t populated on success.
 * @return FILE_OK on success, FILE_FORBIDDEN (403), FILE_NOT_FOUND (404), or FILE_ERROR (500).
 */
file_status_t file_handler_resolve_and_open(const char *req_path, file_info_t *out_info);

/**
 * @brief Close an opened file descriptor in file_info_t.
 */
void file_info_close(file_info_t *info);

#ifdef __cplusplus
}
#endif

#endif /* FILE_HANDLER_H */
