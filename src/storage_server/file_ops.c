#include "common.h"
#include "storage_server.h"

extern SSConfig config;

/**
 * Safe path construction with bounds checking
 * @param dest Destination buffer
 * @param dest_size Size of destination buffer
 * @param filename Filename to append (may include folder path like "folder1/file.txt")
 * @param extension Optional extension (can be NULL), e.g., ".meta", ".undo"
 * @return ERR_SUCCESS on success, ERR_FILE_OPERATION_FAILED if path would be truncated
 * 
 * This function properly handles path length limits and returns an error
 * instead of silently truncating, preventing potential security issues.
 * 
 * If filename contains slashes (folders), the necessary directory structure
 * will be created automatically.
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
        char errmsg[256];
        snprintf(errmsg, sizeof(errmsg), 
                 "Path construction failed for '%s' - exceeds %zu byte limit", 
                 filename, dest_size);
        log_message("SS", "ERROR", errmsg);
        return ERR_FILE_OPERATION_FAILED;
    }
    
    // Create parent directories if filename contains folder path
    char* last_slash = strrchr(dest, '/');
    if (last_slash && last_slash != dest) {
        char dir_path[MAX_PATH];
        int dir_len = last_slash - dest;
        strncpy(dir_path, dest, dir_len);
        dir_path[dir_len] = '\0';
        create_directory(dir_path);
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
    snprintf(msg, sizeof(msg), "Created file '%s'", filename);
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
    snprintf(msg, sizeof(msg), "Deleted file '%s'", filename);
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
    
    long file_size = strlen(*content);
    char msg[256];
    snprintf(msg, sizeof(msg), "Read file '%s' (%ld bytes)", filename, file_size);
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
    
    // Allocate a copy of the content for tokenization (strtok modifies the string)
    char* content_copy = strdup(content);
    if (!content_copy) {
        free(content);
        return ERR_FILE_OPERATION_FAILED;
    }
    
    char* token = strtok(content_copy, " \t\n\r");
    while (token != NULL) {
        (*words)++;
        token = strtok(NULL, " \t\n\r");
    }
    
    free(content_copy);
    free(content);
    return ERR_SUCCESS;
}

/**
 * ss_move_file
 * @brief Physically move/rename a file on the storage server.
 *
 * This function moves both the file and its metadata to the new location.
 * It creates necessary parent directories for the new path.
 *
 * @param old_filename Current file path.
 * @param new_filename New file path.
 * @return ERR_SUCCESS on success, or an ERR_* code on failure.
 */
int ss_move_file(const char* old_filename, const char* new_filename) {
    char old_filepath[MAX_PATH];
    char new_filepath[MAX_PATH];
    char old_metapath[MAX_PATH];
    char new_metapath[MAX_PATH];
    char old_undopath[MAX_PATH];
    char new_undopath[MAX_PATH];
    
    // Construct old paths
    if (ss_build_filepath(old_filepath, sizeof(old_filepath), old_filename, NULL) != ERR_SUCCESS) {
        return ERR_FILE_OPERATION_FAILED;
    }
    if (ss_build_filepath(old_metapath, sizeof(old_metapath), old_filename, ".meta") != ERR_SUCCESS) {
        return ERR_FILE_OPERATION_FAILED;
    }
    if (ss_build_filepath(old_undopath, sizeof(old_undopath), old_filename, ".undo") != ERR_SUCCESS) {
        return ERR_FILE_OPERATION_FAILED;
    }
    
    // Check if old file exists
    if (!file_exists(old_filepath)) {
        return ERR_FILE_NOT_FOUND;
    }
    
    // Construct new paths (this will create parent directories)
    if (ss_build_filepath(new_filepath, sizeof(new_filepath), new_filename, NULL) != ERR_SUCCESS) {
        return ERR_FILE_OPERATION_FAILED;
    }
    if (ss_build_filepath(new_metapath, sizeof(new_metapath), new_filename, ".meta") != ERR_SUCCESS) {
        return ERR_FILE_OPERATION_FAILED;
    }
    if (ss_build_filepath(new_undopath, sizeof(new_undopath), new_filename, ".undo") != ERR_SUCCESS) {
        return ERR_FILE_OPERATION_FAILED;
    }
    
    // Check if new file already exists
    if (file_exists(new_filepath)) {
        return ERR_FILE_EXISTS;
    }
    
    // Move the main file
    if (rename(old_filepath, new_filepath) != 0) {
        char errmsg[256];
        snprintf(errmsg, sizeof(errmsg), 
                 "Failed to move '%s' to '%s': %s", 
                 old_filename, new_filename, strerror(errno));
        log_message("SS", "ERROR", errmsg);
        return ERR_FILE_OPERATION_FAILED;
    }
    
    // Move metadata file if it exists
    if (file_exists(old_metapath)) {
        rename(old_metapath, new_metapath);  // Ignore errors
    }
    
    // Move undo file if it exists
    if (file_exists(old_undopath)) {
        rename(old_undopath, new_undopath);  // Ignore errors
    }
    
    char msg[512];
    snprintf(msg, sizeof(msg), "Moved '%s' -> '%s'", old_filename, new_filename);
    log_message("SS", "INFO", msg);
    
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
        char errmsg[256];
        snprintf(errmsg, sizeof(errmsg), 
                 "Failed to construct metadata path for '%s'", 
                 filename);
        log_message("SS", "ERROR", errmsg);
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

/**
 * increment_edit_stats
 * @brief Increment edit counter for a file
 * @param filename File to update
 * @param username User performing the edit
 */
void increment_edit_stats(const char* filename, const char* username) {
    char stats_path[MAX_PATH];
    
    // Build path to .stats file
    if (ss_build_filepath(stats_path, sizeof(stats_path), filename, ".stats") != ERR_SUCCESS) {
        return;
    }
    
    // Read existing stats or create new
    FILE* f = fopen(stats_path, "r");
    long total_edits = 0;
    
    // Storage for user statistics (simple implementation)
    typedef struct {
        char username[MAX_USERNAME];
        long edit_count;
    } UserStat;
    
    UserStat user_stats[100]; // Support up to 100 users per file
    int user_count = 0;
    
    if (f) {
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            if (strncmp(line, "total_edits:", 12) == 0) {
                sscanf(line, "total_edits:%ld", &total_edits);
            } else if (strncmp(line, "user:", 5) == 0) {
                if (user_count < 100) {
                    sscanf(line, "user:%[^:]:%ld", 
                           user_stats[user_count].username, 
                           &user_stats[user_count].edit_count);
                    user_count++;
                }
            }
        }
        fclose(f);
    }
    
    total_edits++;
    
    // Update or add user-specific counter
    int user_found = 0;
    for (int i = 0; i < user_count; i++) {
        if (strcmp(user_stats[i].username, username) == 0) {
            user_stats[i].edit_count++;
            user_found = 1;
            break;
        }
    }
    
    if (!user_found && user_count < 100) {
        strncpy(user_stats[user_count].username, username, MAX_USERNAME - 1);
        user_stats[user_count].username[MAX_USERNAME - 1] = '\0';
        user_stats[user_count].edit_count = 1;
        user_count++;
    }
    
    // Write updated stats
    f = fopen(stats_path, "w");
    if (f) {
        fprintf(f, "total_edits:%ld\n", total_edits);
        for (int i = 0; i < user_count; i++) {
            fprintf(f, "user:%s:%ld\n", user_stats[i].username, user_stats[i].edit_count);
        }
        fclose(f);
    }
}

/**
 * get_file_stats
 * @brief Get statistics for a file
 * @param filename File to query
 * @param stats_out Output buffer for formatted statistics
 * @param bufsize Size of output buffer
 * @return ERR_SUCCESS or error code
 */
int get_file_stats(const char* filename, char* stats_out, size_t bufsize) {
    char stats_path[MAX_PATH];
    
    if (ss_build_filepath(stats_path, sizeof(stats_path), filename, ".stats") != ERR_SUCCESS) {
        snprintf(stats_out, bufsize, 
                "  %s├─%s Total Edits: %s0%s\n",
                ANSI_GREEN, ANSI_RESET,
                ANSI_BRIGHT_GREEN, ANSI_RESET);
        return ERR_SUCCESS;
    }
    
    FILE* f = fopen(stats_path, "r");
    if (!f) {
        snprintf(stats_out, bufsize, 
                "  %s├─%s Total Edits: %s0%s\n",
                ANSI_GREEN, ANSI_RESET,
                ANSI_BRIGHT_GREEN, ANSI_RESET);
        return ERR_SUCCESS;
    }
    
    long total_edits = 0;
    char most_active_user[MAX_USERNAME] = "none";
    long max_user_edits = 0;
    
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "total_edits:", 12) == 0) {
            sscanf(line, "total_edits:%ld", &total_edits);
        } else if (strncmp(line, "user:", 5) == 0) {
            char username[MAX_USERNAME];
            long user_edits;
            if (sscanf(line, "user:%[^:]:%ld", username, &user_edits) == 2) {
                if (user_edits > max_user_edits) {
                    max_user_edits = user_edits;
                    strncpy(most_active_user, username, sizeof(most_active_user) - 1);
                    most_active_user[sizeof(most_active_user) - 1] = '\0';
                }
            }
        }
    }
    fclose(f);
    
    if (total_edits > 0) {
        snprintf(stats_out, bufsize,
                "  %s├─%s Total Edits: %s%ld%s\n"
                "  %s└─%s Most Active User: %s%s%s (%s%ld%s edits)\n",
                ANSI_GREEN, ANSI_RESET, ANSI_BRIGHT_GREEN, total_edits, ANSI_RESET,
                ANSI_GREEN, ANSI_RESET, ANSI_BRIGHT_MAGENTA, most_active_user, ANSI_RESET,
                ANSI_BRIGHT_GREEN, max_user_edits, ANSI_RESET);
    } else {
        snprintf(stats_out, bufsize,
                "  %s└─%s Total Edits: %s0%s\n",
                ANSI_GREEN, ANSI_RESET,
                ANSI_BRIGHT_GREEN, ANSI_RESET);
    }
    
    return ERR_SUCCESS;
}
