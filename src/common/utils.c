#include "common.h"

// Read entire file content into memory
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

// Write content to file atomically
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

// Check if file exists
int file_exists(const char* filepath) {
    return access(filepath, F_OK) == 0;
}

// Get file size
long get_file_size(const char* filepath) {
    struct stat st;
    if (stat(filepath, &st) == 0) {
        return st.st_size;
    }
    return -1;
}

// Create directory recursively
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

// Get human-readable error message
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
        default: return "Unknown error";
    }
}
