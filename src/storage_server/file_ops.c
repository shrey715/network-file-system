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

// Delete a file
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

// Read file content
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

// Get file information
int ss_get_file_info(const char* filename, long* size, int* words, int* chars) {
    char filepath[MAX_PATH];
    
    // Safely construct the file path
    if (ss_build_filepath(filepath, sizeof(filepath), filename, NULL) != ERR_SUCCESS) {
        return ERR_FILE_OPERATION_FAILED;
    }
    
    if (!file_exists(filepath)) {
        return ERR_FILE_NOT_FOUND;
    }
    
    // Get size
    *size = get_file_size(filepath);
    
    // Count words and characters
    char* content = read_file_content(filepath);
    if (!content) {
        return ERR_FILE_OPERATION_FAILED;
    }
    
    *chars = strlen(content);
    *words = 0;
    
    int in_word = 0;
    for (char* p = content; *p; p++) {
        if (*p == ' ' || *p == '\n' || *p == '\t') {
            in_word = 0;
        } else if (!in_word) {
            in_word = 1;
            (*words)++;
        }
    }
    
    free(content);
    return ERR_SUCCESS;
}

// Save file metadata
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
