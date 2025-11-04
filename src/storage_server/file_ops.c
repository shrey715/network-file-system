#include "common.h"
#include "storage_server.h"

extern SSConfig config;

// Create a new empty file
int ss_create_file(const char* filename, const char* owner) {
    char filepath[MAX_PATH];
    snprintf(filepath, sizeof(filepath), "%s/%s", config.storage_dir, filename);
    
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
    snprintf(filepath, sizeof(filepath), "%s/%s", config.storage_dir, filename);
    
    if (!file_exists(filepath)) {
        return ERR_FILE_NOT_FOUND;
    }
    
    if (unlink(filepath) != 0) {
        return ERR_FILE_OPERATION_FAILED;
    }
    
    // Delete metadata
    char metapath[MAX_PATH];
    snprintf(metapath, sizeof(metapath), "%s/%s.meta", config.storage_dir, filename);
    unlink(metapath);
    
    // Delete undo file
    char undopath[MAX_PATH];
    snprintf(undopath, sizeof(undopath), "%s/%s.undo", config.storage_dir, filename);
    unlink(undopath);
    
    char msg[256];
    snprintf(msg, sizeof(msg), "Deleted file: %s", filename);
    log_message("SS", "INFO", msg);
    
    return ERR_SUCCESS;
}

// Read file content
int ss_read_file(const char* filename, char** content) {
    char filepath[MAX_PATH];
    snprintf(filepath, sizeof(filepath), "%s/%s", config.storage_dir, filename);
    
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
    snprintf(filepath, sizeof(filepath), "%s/%s", config.storage_dir, filename);
    
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
    snprintf(metapath, sizeof(metapath), "%s/%s.meta", config.storage_dir, filename);
    
    FILE* f = fopen(metapath, "w");
    if (f) {
        fprintf(f, "owner:%s\n", owner);
        fprintf(f, "created:%ld\n", time(NULL));
        fprintf(f, "modified:%ld\n", time(NULL));
        fclose(f);
    }
}
