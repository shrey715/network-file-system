#include "storage_server.h"
#include <sys/stat.h>
#include <dirent.h>
#include <time.h>

extern SSConfig ss_config;

/**
 * Creates a checkpoint for the given file with the specified tag
 * Checkpoint format: filename.checkpoint.<tag>
 */
int ss_create_checkpoint(const char* filename, const char* checkpoint_tag) {
    char filepath[MAX_PATH];
    char checkpoint_path[MAX_PATH];
    
    // Build path to the main file
    if (ss_build_filepath(filepath, sizeof(filepath), filename, NULL) < 0) {
        return ERR_INVALID_PATH;
    }
    
    // Check if file exists
    struct stat st;
    if (stat(filepath, &st) != 0) {
        return ERR_FILE_NOT_FOUND;
    }
    
    // Build checkpoint filename
    if (ss_build_filepath(checkpoint_path, sizeof(checkpoint_path), filename, NULL) < 0) {
        return ERR_INVALID_PATH;
    }
    
    // Append checkpoint extension
    int len = strlen(checkpoint_path);
    int remaining = sizeof(checkpoint_path) - len - 1;
    int needed = snprintf(checkpoint_path + len, remaining, ".checkpoint.%s", checkpoint_tag);
    
    if (needed >= remaining) {
        return ERR_INVALID_PATH;
    }
    
    // Check if checkpoint already exists
    if (stat(checkpoint_path, &st) == 0) {
        return ERR_CHECKPOINT_EXISTS;
    }
    
    // Read current file content
    FILE* src = fopen(filepath, "r");
    if (!src) {
        return ERR_FILE_NOT_FOUND;
    }
    
    // Create checkpoint file
    FILE* dest = fopen(checkpoint_path, "w");
    if (!dest) {
        fclose(src);
        return ERR_FILE_OPERATION_FAILED;
    }
    
    // Copy content
    char buffer[BUFFER_SIZE];
    size_t bytes;
    while ((bytes = fread(buffer, 1, sizeof(buffer), src)) > 0) {
        if (fwrite(buffer, 1, bytes, dest) != bytes) {
            fclose(src);
            fclose(dest);
            unlink(checkpoint_path);
            return ERR_FILE_OPERATION_FAILED;
        }
    }
    
    fclose(src);
    fclose(dest);
    
    // Create metadata file for checkpoint (stores creation time)
    char meta_path[MAX_PATH * 2];
    snprintf(meta_path, sizeof(meta_path), "%s.meta", checkpoint_path);
    FILE* meta = fopen(meta_path, "w");
    if (meta) {
        time_t now = time(NULL);
        fprintf(meta, "%ld\n", (long)now);
        fclose(meta);
    }
    
    return ERR_SUCCESS;
}

/**
 * Views the content of a specific checkpoint
 */
int ss_view_checkpoint(const char* filename, const char* checkpoint_tag, char** content) {
    char checkpoint_path[MAX_PATH];
    
    // Build checkpoint path
    if (ss_build_filepath(checkpoint_path, sizeof(checkpoint_path), filename, NULL) < 0) {
        return ERR_INVALID_PATH;
    }
    
    int len = strlen(checkpoint_path);
    int remaining = sizeof(checkpoint_path) - len - 1;
    int needed = snprintf(checkpoint_path + len, remaining, ".checkpoint.%s", checkpoint_tag);
    
    if (needed >= remaining) {
        return ERR_INVALID_PATH;
    }
    
    // Check if checkpoint exists
    struct stat st;
    if (stat(checkpoint_path, &st) != 0) {
        return ERR_CHECKPOINT_NOT_FOUND;
    }
    
    // Read checkpoint content
    FILE* fp = fopen(checkpoint_path, "r");
    if (!fp) {
        return ERR_CHECKPOINT_NOT_FOUND;
    }
    
    // Get file size
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    // Allocate memory for content
    *content = (char*)malloc(size + 1);
    if (!*content) {
        fclose(fp);
        return ERR_FILE_OPERATION_FAILED;
    }
    
    // Read content
    size_t bytes_read = fread(*content, 1, size, fp);
    (*content)[bytes_read] = '\0';
    
    fclose(fp);
    return ERR_SUCCESS;
}

/**
 * Reverts a file to a specific checkpoint
 */
int ss_revert_checkpoint(const char* filename, const char* checkpoint_tag) {
    char filepath[MAX_PATH];
    char checkpoint_path[MAX_PATH];
    char temp_path[MAX_PATH * 2];
    
    // Build paths
    if (ss_build_filepath(filepath, sizeof(filepath), filename, NULL) < 0) {
        return ERR_INVALID_PATH;
    }
    
    if (ss_build_filepath(checkpoint_path, sizeof(checkpoint_path), filename, NULL) < 0) {
        return ERR_INVALID_PATH;
    }
    
    int len = strlen(checkpoint_path);
    int remaining = sizeof(checkpoint_path) - len - 1;
    int needed = snprintf(checkpoint_path + len, remaining, ".checkpoint.%s", checkpoint_tag);
    
    if (needed >= remaining) {
        return ERR_INVALID_PATH;
    }
    
    // Check if checkpoint exists
    struct stat st;
    if (stat(checkpoint_path, &st) != 0) {
        return ERR_CHECKPOINT_NOT_FOUND;
    }
    
    // Save current state as undo before reverting
    ss_save_undo(filename);
    
    // Create temporary file for atomic operation
    snprintf(temp_path, sizeof(temp_path), "%s.tmp", filepath);
    
    // Copy checkpoint content to temp file
    FILE* src = fopen(checkpoint_path, "r");
    if (!src) {
        return ERR_CHECKPOINT_NOT_FOUND;
    }
    
    FILE* dest = fopen(temp_path, "w");
    if (!dest) {
        fclose(src);
        return ERR_FILE_OPERATION_FAILED;
    }
    
    char buffer[BUFFER_SIZE];
    size_t bytes;
    while ((bytes = fread(buffer, 1, sizeof(buffer), src)) > 0) {
        if (fwrite(buffer, 1, bytes, dest) != bytes) {
            fclose(src);
            fclose(dest);
            unlink(temp_path);
            return ERR_FILE_OPERATION_FAILED;
        }
    }
    
    fclose(src);
    fclose(dest);
    
    // Atomically replace file with checkpoint content
    if (rename(temp_path, filepath) != 0) {
        unlink(temp_path);
        return ERR_FILE_OPERATION_FAILED;
    }
    
    return ERR_SUCCESS;
}

/**
 * Lists all checkpoints for a given file
 * Returns a formatted string with checkpoint tags and timestamps
 */
int ss_list_checkpoints(const char* filename, char** checkpoint_list) {
    char filepath[MAX_PATH];
    
    // Build base filepath
    if (ss_build_filepath(filepath, sizeof(filepath), filename, NULL) < 0) {
        return ERR_INVALID_PATH;
    }
    
    // Extract directory and base filename
    char dir_path[MAX_PATH];
    char base_filename[MAX_FILENAME];
    
    const char* last_slash = strrchr(filepath, '/');
    if (last_slash) {
        int dir_len = last_slash - filepath;
        strncpy(dir_path, filepath, dir_len);
        dir_path[dir_len] = '\0';
        strcpy(base_filename, last_slash + 1);
    } else {
        strcpy(dir_path, ".");
        strcpy(base_filename, filepath);
    }
    
    // Open directory
    DIR* dir = opendir(dir_path);
    if (!dir) {
        return ERR_FILE_NOT_FOUND;
    }
    
    // Build search pattern
    char pattern[MAX_PATH];
    snprintf(pattern, sizeof(pattern), "%s.checkpoint.", base_filename);
    int pattern_len = strlen(pattern);
    
    // Count checkpoints and build list
    struct dirent* entry;
    int count = 0;
    char temp_list[BUFFER_SIZE * 4] = "";
    
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, pattern, pattern_len) == 0) {
            // Found a checkpoint file
            const char* tag = entry->d_name + pattern_len;
            
            // Skip if it's a meta file
            if (strstr(tag, ".meta")) {
                continue;
            }
            
            // Get checkpoint timestamp from meta file
            char meta_path[MAX_PATH * 2];
            snprintf(meta_path, sizeof(meta_path), "%s/%s.meta", dir_path, entry->d_name);
            
            time_t timestamp = 0;
            FILE* meta = fopen(meta_path, "r");
            if (meta) {
                long ts;
                if (fscanf(meta, "%ld", &ts) == 1) {
                    timestamp = (time_t)ts;
                }
                fclose(meta);
            }
            
            // Format checkpoint info
            char checkpoint_info[512];
            if (timestamp > 0) {
                char time_str[64];
                struct tm* tm_info = localtime(&timestamp);
                strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);
                snprintf(checkpoint_info, sizeof(checkpoint_info), 
                         "  [%s] - %s\n", tag, time_str);
            } else {
                snprintf(checkpoint_info, sizeof(checkpoint_info), 
                         "  [%s] - (no timestamp)\n", tag);
            }
            
            // Append to list
            if (strlen(temp_list) + strlen(checkpoint_info) < sizeof(temp_list) - 1) {
                strcat(temp_list, checkpoint_info);
                count++;
            }
        }
    }
    
    closedir(dir);
    
    // Prepare result
    if (count == 0) {
        *checkpoint_list = strdup("No checkpoints found for this file.\n");
        return ERR_SUCCESS;
    }
    
    // Allocate and copy result
    char header[256];
    snprintf(header, sizeof(header), "Checkpoints for '%s' (%d total):\n", filename, count);
    
    *checkpoint_list = (char*)malloc(strlen(header) + strlen(temp_list) + 1);
    if (!*checkpoint_list) {
        return ERR_FILE_OPERATION_FAILED;
    }
    
    strcpy(*checkpoint_list, header);
    strcat(*checkpoint_list, temp_list);
    
    return ERR_SUCCESS;
}

