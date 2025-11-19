#include "common.h"
#include <ctype.h>

/**
 * visual_strlen
 * @brief Calculate the visual length of a string, excluding ANSI escape sequences.
 * 
 * ANSI codes follow the pattern: ESC [ ... m
 * This function skips those sequences when counting characters.
 * 
 * @param str Input string (may contain ANSI codes)
 * @return Visual character count
 */
int visual_strlen(const char* str) {
    if (str == NULL) {
        return 0;
    }
    
    int len = 0;
    const char* p = str;
    
    while (*p) {
        if (*p == '\033') {  // ESC character
            // Skip until we find 'm' (end of ANSI sequence)
            while (*p && *p != 'm') {
                p++;
            }
            if (*p == 'm') {
                p++;  // Skip the 'm' itself
            }
        } else {
            len++;
            p++;
        }
    }
    
    return len;
}

/**
 * read_file_content
 * @brief Read the entire contents of a file into a malloc'd buffer.
 *
 * The returned buffer must be freed by the caller. On error NULL is
 * returned.
 *
 * @param filepath Path to the file to read.
 * @return Pointer to malloc'd null-terminated content on success, or NULL.
 */
char* read_file_content(const char* filepath) {
    FILE* f = fopen(filepath, "r");
    if (!f) return NULL;
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char* content = (char*)malloc(size + 1);
    if (!content) {
        fclose(f);
        return NULL;
    }
    
    size_t read = fread(content, 1, size, f);
    content[read] = '\0';
    
    fclose(f);
    return content;
}

/**
 * write_file_content
 * @brief Atomically write `content` to `filepath` using a temporary file and
 *        rename(2).
 *
 * This reduces the chance of partial writes being observed by other
 * processes. Returns 0 on success.
 *
 * @param filepath Destination path to write.
 * @param content Null-terminated string to write.
 * @return 0 on success, -1 on failure.
 */
int write_file_content(const char* filepath, const char* content) {
    char temppath[MAX_PATH];
    snprintf(temppath, sizeof(temppath), "%s.tmp", filepath);
    
    FILE* f = fopen(temppath, "w");
    if (!f) return -1;
    
    fprintf(f, "%s", content);
    fclose(f);
    
    // Atomic rename
    if (rename(temppath, filepath) != 0) {
        unlink(temppath);
        return -1;
    }
    
    return 0;
}

/**
 * file_exists
 * @brief Check whether a file exists at `filepath`.
 *
 * @param filepath Path to check.
 * @return 1 if exists, 0 otherwise.
 */
int file_exists(const char* filepath) {
    return access(filepath, F_OK) == 0;
}

/**
 * get_file_size
 * @brief Return the size in bytes of the specified file.
 *
 * @param filepath Path to the file.
 * @return File size in bytes, or -1 on error.
 */
long get_file_size(const char* filepath) {
    struct stat st;
    if (stat(filepath, &st) == 0) {
        return st.st_size;
    }
    return -1;
}

/**
 * create_directory
 * @brief Create a directory and any missing parent directories (mkdir -p).
 *
 * Silent on errors caused by the directory already existing. Uses mode
 * 0755 for created directories.
 *
 * @param path Directory path to create.
 */
void create_directory(const char* path) {
    char tmp[MAX_PATH];
    char* p = NULL;
    size_t len;
    
    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    if (tmp[len - 1] == '/') {
        tmp[len - 1] = 0;
    }
    
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

/**
 * get_error_message
 * @brief Map internal error codes to human-readable strings.
 *
 * These strings are used in logs and message responses to clients.
 *
 * @param error_code ERR_* code to translate.
 * @return Pointer to a static string describing the error.
 */
char* get_error_message(int error_code) {
    switch (error_code) {
        case ERR_SUCCESS: return "Success";
        case ERR_FILE_NOT_FOUND: return "File not found";
        case ERR_PERMISSION_DENIED: return "Permission denied";
        case ERR_FILE_EXISTS: return "File already exists";
        case ERR_SENTENCE_LOCKED: return "Sentence is locked by another user";
        case ERR_INVALID_INDEX: return "Invalid sentence or word index";
        case ERR_NOT_OWNER: return "Only owner can perform this operation";
        case ERR_USER_NOT_FOUND: return "User not found";
        case ERR_SS_UNAVAILABLE: return "Storage server unavailable";
        case ERR_SS_DISCONNECTED: return "Storage server disconnected";
        case ERR_INVALID_COMMAND: return "Invalid command";
        case ERR_NETWORK_ERROR: return "Network error";
        case ERR_FILE_OPERATION_FAILED: return "File operation failed";
        case ERR_UNDO_NOT_AVAILABLE: return "No undo history available";
        case ERR_INVALID_SENTENCE: return "Invalid sentence index";
        case ERR_INVALID_WORD: return "Invalid word index";
        case ERR_FILE_EMPTY: return "File is empty";
        case ERR_FOLDER_NOT_FOUND: return "Folder not found";
        case ERR_FOLDER_EXISTS: return "Folder already exists";
        case ERR_INVALID_PATH: return "Invalid path";
        case ERR_CHECKPOINT_NOT_FOUND: return "Checkpoint not found";
        case ERR_CHECKPOINT_EXISTS: return "Checkpoint already exists";
        case ERR_REQUEST_EXISTS: return "Access request already exists";
        case ERR_REQUEST_NOT_FOUND: return "Access request not found";
        case ERR_ALREADY_HAS_ACCESS: return "Already has access";
        case ERR_INVALID_FILENAME: return "Invalid filename: reserved extension not allowed";
        default: return "Unknown error";
    }
}

/**
 * is_valid_filename
 * @brief Check if a filename is valid (doesn't use reserved extensions)
 * 
 * Reserved extensions that users cannot create:
 * - .meta (system metadata)
 * - .undo (undo information)
 * - .stats (file statistics)
 * - .checkpoint.* (checkpoint files)
 * 
 * @param filename The filename to validate
 * @return 1 if valid, 0 if invalid
 */
int is_valid_filename(const char* filename) {
    if (!filename || strlen(filename) == 0) {
        return 0;
    }
    
    // Check for reserved extensions
    const char* reserved_extensions[] = {
        ".meta",
        ".undo",
        ".stats"
    };
    
    size_t filename_len = strlen(filename);
    
    // Check each reserved extension
    for (size_t i = 0; i < sizeof(reserved_extensions) / sizeof(reserved_extensions[0]); i++) {
        size_t ext_len = strlen(reserved_extensions[i]);
        if (filename_len >= ext_len) {
            // Check if filename ends with this extension
            if (strcmp(filename + filename_len - ext_len, reserved_extensions[i]) == 0) {
                return 0;  // Invalid: uses reserved extension
            }
        }
    }
    
    // Check for .checkpoint. pattern (e.g., file.checkpoint.v1)
    if (strstr(filename, ".checkpoint.") != NULL) {
        return 0;  // Invalid: uses checkpoint pattern
    }
    
    return 1;  // Valid filename
}
