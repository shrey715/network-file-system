#include "common.h"
#include "storage_server.h"

extern SSConfig config;

/**
 * Safe path construction with bounds checking
 * @param dest Destination buffer
 * @param dest_size Size of destination buffer
 * @param filename Filename to append
 * @param extension Optional extension (can be NULL), e.g., ".meta", ".undo"
 * @return ERR_SUCCESS on success, ERR_FILE_OPERATION_FAILED if path would be truncated
 * 
 * This function properly handles path length limits and returns an error
 * instead of silently truncating, preventing potential security issues.
 */
int ss_build_filepath(char* dest, size_t dest_size, const char* filename, const char* extension) {
    int written;
    
    if (extension) {
        written = snprintf(dest, dest_size, "%s/%s%s", config.storage_dir, filename, extension);
    } else {
        written = snprintf(dest, dest_size, "%s/%s", config.storage_dir, filename);
    }
    
    // snprintf returns the number of characters that would have been written (excluding null)
    // If written >= dest_size, truncation occurred
    if (written < 0 || (size_t)written >= dest_size) {
        log_message("SS", "ERROR", "Path construction failed - path too long");
        return ERR_FILE_OPERATION_FAILED;
    }
    
    return ERR_SUCCESS;
}

// Create a new empty file
/**
 * ss_create_file
 * @brief Create a new empty file on the Storage Server and initialize its
 *        metadata.
 *
 * Constructs a safe path under the configured storage directory, ensures the
 * file does not already exist, creates it, and writes basic metadata.
 *
 * @param filename Null-terminated filename to create.
 * @param owner Null-terminated owner username for metadata.
 * @return ERR_SUCCESS on success, or ERR_FILE_EXISTS / ERR_FILE_OPERATION_FAILED.
 */
int ss_create_file(const char* filename, const char* owner) {
    char filepath[MAX_PATH];
    
    // Safely construct the file path
    if (ss_build_filepath(filepath, sizeof(filepath), filename, NULL) != ERR_SUCCESS) {
        return ERR_FILE_OPERATION_FAILED;
    }
    
    // Check if file already exists
    if (file_exists(filepath)) {
        return ERR_FILE_EXISTS;
    }
    
    // Create empty file
    FILE* f = fopen(filepath, "w");
    if (!f) {
        return ERR_FILE_OPERATION_FAILED;
    }
    fclose(f);
    
    // Save metadata
    save_file_metadata(filename, owner);
    
    char msg[256];
    snprintf(msg, sizeof(msg), "Created file: %s (owner: %s)", filename, owner);
    log_message("SS", "INFO", msg);
    
    return ERR_SUCCESS;
}

/**
 * ss_delete_file
 * @brief Delete the specified file and any associated metadata/undo files.
 *
 * Removes the file from disk and attempts to remove `.meta` and `.undo`
 * auxiliary files. Returns an error if the primary file does not exist or the
 * unlink operation fails.
 *
 * @param filename Null-terminated filename to delete.
 * @return ERR_SUCCESS on success, or ERR_FILE_NOT_FOUND / ERR_FILE_OPERATION_FAILED.
 */
int ss_delete_file(const char* filename) {
    char filepath[MAX_PATH];
    char metapath[MAX_PATH];
    char undopath[MAX_PATH];
    
    // Safely construct paths
    if (ss_build_filepath(filepath, sizeof(filepath), filename, NULL) != ERR_SUCCESS) {
        return ERR_FILE_OPERATION_FAILED;
    }
    
    if (!file_exists(filepath)) {
        return ERR_FILE_NOT_FOUND;
    }
    
    if (unlink(filepath) != 0) {
        return ERR_FILE_OPERATION_FAILED;
    }
    
    // Delete metadata (ignore errors - file may not exist)
    if (ss_build_filepath(metapath, sizeof(metapath), filename, ".meta") == ERR_SUCCESS) {
        unlink(metapath);
    }
    
    // Delete undo file (ignore errors - file may not exist)
    if (ss_build_filepath(undopath, sizeof(undopath), filename, ".undo") == ERR_SUCCESS) {
        unlink(undopath);
    }
    
    char msg[256];
    snprintf(msg, sizeof(msg), "Deleted file: %s", filename);
    log_message("SS", "INFO", msg);
    
    return ERR_SUCCESS;
}

/**
 * ss_read_file
 * @brief Read the entire contents of `filename` into a malloc'd buffer.
 *
 * The caller is responsible for freeing `*content` on success.
 *
 * @param filename Null-terminated filename to read.
 * @param content Out parameter; set to malloc'd buffer on success.
 * @return ERR_SUCCESS on success, or ERR_FILE_NOT_FOUND / ERR_FILE_OPERATION_FAILED.
 */
int ss_read_file(const char* filename, char** content) {
    char filepath[MAX_PATH];
    
    // Safely construct the file path
    if (ss_build_filepath(filepath, sizeof(filepath), filename, NULL) != ERR_SUCCESS) {
        return ERR_FILE_OPERATION_FAILED;
    }
    
    if (!file_exists(filepath)) {
        return ERR_FILE_NOT_FOUND;
    }
    
    *content = read_file_content(filepath);
    if (!*content) {
        return ERR_FILE_OPERATION_FAILED;
    }
    
    char msg[256];
    snprintf(msg, sizeof(msg), "Read file: %s", filename);
    log_message("SS", "INFO", msg);
    
    return ERR_SUCCESS;
}

/**
 * ss_get_file_info
 * @brief Populate basic statistics about `filename` (size, word count, chars).
 *
 * Reads the file contents to compute words and character counts. Caller must
 * provide pointers for the output values.
 *
 * @param filename Null-terminated filename to inspect.
 * @param size Out parameter for file size in bytes.
 * @param words Out parameter for approximate word count.
 * @param chars Out parameter for character count.
 * @return ERR_SUCCESS on success, or ERR_FILE_NOT_FOUND / ERR_FILE_OPERATION_FAILED.
 */
int ss_get_file_info(const char* filename, long* size, int* words, int* chars) {
    char filepath[MAX_PATH];
    if (ss_build_filepath(filepath, sizeof(filepath), filename, NULL) != ERR_SUCCESS) {
        return ERR_FILE_OPERATION_FAILED;
    }
    
    if (!file_exists(filepath)) {
        return ERR_FILE_NOT_FOUND;
    }
    
    // Get file size
    struct stat st;
    if (stat(filepath, &st) != 0) {
        return ERR_FILE_OPERATION_FAILED;
    }
    *size = st.st_size;
    
    // Read content to count words and chars
    char* content = read_file_content(filepath);
    if (!content) {
        *words = 0;
        *chars = 0;
        return ERR_SUCCESS; // Empty file is valid
    }
    
    // Count characters (excluding null terminator)
    *chars = strlen(content);
    
    // Count words (space-separated tokens)
    *words = 0;
    char content_copy[BUFFER_SIZE];
    strncpy(content_copy, content, BUFFER_SIZE - 1);
    content_copy[BUFFER_SIZE - 1] = '\0';
    
    char* token = strtok(content_copy, " \t\n\r");
    while (token != NULL) {
        (*words)++;
        token = strtok(NULL, " \t\n\r");
    }
    
    free(content);
    return ERR_SUCCESS;
}

/**
 * save_file_metadata
 * @brief Write basic metadata for `filename` into a `.meta` file.
 *
 * Writes owner, creation and modification timestamps. Errors are logged but
 * not returned to callers.
 *
 * @param filename Null-terminated filename to describe.
 * @param owner Null-terminated owner username.
 */
void save_file_metadata(const char* filename, const char* owner) {
    char metapath[MAX_PATH];
    
    // Safely construct the metadata file path
    if (ss_build_filepath(metapath, sizeof(metapath), filename, ".meta") != ERR_SUCCESS) {
        log_message("SS", "ERROR", "Failed to construct metadata path");
        return;
    }
    
    FILE* f = fopen(metapath, "w");
    if (f) {
        fprintf(f, "owner:%s\n", owner);
        fprintf(f, "created:%ld\n", time(NULL));
        fprintf(f, "modified:%ld\n", time(NULL));
        fclose(f);
    }
}
