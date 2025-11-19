#include "common.h"
#include "name_server.h"
#include <limits.h>

extern NameServerState ns_state;

/**
 * nm_register_file
 * @brief Register a new file in the Name Server registry and persist state.
 *
 * Creates a FileMetadata entry for `filename`, sets the owner and assigns the
 * storage server id where the file will reside. Initializes basic counters
 * and an ACL entry granting the owner full read/write permissions.
 *
 * @param filename Null-terminated name of the file to register.
 * @param folder_path Path to folder containing file (empty string for root).
 * @param owner Null-terminated username who will own the file.
 * @param ss_id ID of the Storage Server assigned to hold the file.
 * @return ERR_SUCCESS on success, or an ERR_* code on failure (e.g.
 *         ERR_FILE_EXISTS, ERR_FILE_OPERATION_FAILED).
 */
int nm_register_file(const char* filename, const char* folder_path, const char* owner, int ss_id) {
    pthread_mutex_lock(&ns_state.lock);
    
    // Check if file already exists in the same folder
    for (int i = 0; i < ns_state.file_count; i++) {
        if (strcmp(ns_state.files[i].filename, filename) == 0 &&
            strcmp(ns_state.files[i].folder_path, folder_path) == 0) {
            pthread_mutex_unlock(&ns_state.lock);
            return ERR_FILE_EXISTS;
        }
    }
    
    // If folder_path is not empty, verify folder exists
    if (folder_path && strlen(folder_path) > 0) {
        FolderMetadata* folder = nm_find_folder(folder_path);
        if (!folder) {
            pthread_mutex_unlock(&ns_state.lock);
            return ERR_FOLDER_NOT_FOUND;
        }
    }
    
    // Add new file
    if (ns_state.file_count >= MAX_FILES) {
        pthread_mutex_unlock(&ns_state.lock);
        return ERR_FILE_OPERATION_FAILED;
    }
    
    FileMetadata* file = &ns_state.files[ns_state.file_count];
    strcpy(file->filename, filename);
    strcpy(file->folder_path, folder_path ? folder_path : "");
    strcpy(file->owner, owner);
    file->ss_id = ss_id;
    file->created_time = time(NULL);
    file->last_modified = time(NULL);
    file->last_accessed = time(NULL);
    file->file_size = 0;
    file->word_count = 0;
    file->char_count = 0;
    
    // Initialize ACL with owner having full access
    file->acl = malloc(sizeof(AccessControlEntry));
    strcpy(file->acl[0].username, owner);
    file->acl[0].read_permission = 1;
    file->acl[0].write_permission = 1;
    file->acl_count = 1;
    
    int file_idx = ns_state.file_count;
    ns_state.file_count++;
    
    // Insert into Trie for efficient search
    char full_path[MAX_PATH];
    if (folder_path && strlen(folder_path) > 0) {
        snprintf(full_path, MAX_PATH, "%s/%s", folder_path, filename);
    } else {
        snprintf(full_path, MAX_PATH, "%s", filename);
    }
    
    if (ns_state.file_trie_root) {
        trie_insert(ns_state.file_trie_root, full_path, file_idx);
    }
    
    pthread_mutex_unlock(&ns_state.lock);
    
    save_state();
    
    char msg[256];
    snprintf(msg, sizeof(msg), 
             "Registered file '%s' in folder '%s' (SS: %d)", 
             filename, folder_path ? folder_path : "/", ss_id);
    log_message("NM", "INFO", msg);
    
    return ERR_SUCCESS;
}

/**
 * nm_find_file
 * @brief Look up a file by its full path or just filename using efficient search.
 *
 * Uses two-level caching strategy:
 * 1. LRU Cache: O(1) lookup for recently accessed files
 * 2. Trie: O(m) lookup where m is path length
 * 3. Linear fallback: Only if Trie/cache unavailable
 *
 * This function handles both formats:
 * - Full path: "projects/backend/server.py" 
 * - Just filename: "server.py" (searches root only)
 *
 * @param filename Null-terminated filename or full path to search for.
 * @return Pointer to FileMetadata on success, or NULL if not found.
 */
FileMetadata* nm_find_file(const char* filename) {
    // Construct full path for search
    char full_path[MAX_PATH];
    const char* last_slash = strrchr(filename, '/');
    
    if (last_slash) {
        strncpy(full_path, filename, MAX_PATH - 1);
        full_path[MAX_PATH - 1] = '\0';
    } else {
        // Root file - use filename as-is
        snprintf(full_path, MAX_PATH, "%s", filename);
    }
    
    // LEVEL 1: Check LRU cache first (O(1) for cache hits)
    if (ns_state.file_cache) {
        int cached_index = cache_get(ns_state.file_cache, full_path);
        if (cached_index >= 0 && cached_index < ns_state.file_count) {
            // Verify cache entry is still valid
            FileMetadata* file = &ns_state.files[cached_index];
            char file_full_path[MAX_FULL_PATH];
            if (strlen(file->folder_path) > 0) {
                snprintf(file_full_path, MAX_FULL_PATH, "%s/%s", file->folder_path, file->filename);
            } else {
                snprintf(file_full_path, MAX_FULL_PATH, "%s", file->filename);
            }
            
            if (strcmp(file_full_path, full_path) == 0) {
                return file;  // Cache hit!
            }
        }
    }
    
    // LEVEL 2: Check Trie (O(m) where m is path length)
    if (ns_state.file_trie_root) {
        int trie_index = trie_search(ns_state.file_trie_root, full_path);
        if (trie_index >= 0 && trie_index < ns_state.file_count) {
            FileMetadata* file = &ns_state.files[trie_index];
            
            // Add to cache for future O(1) lookups
            if (ns_state.file_cache) {
                cache_put(ns_state.file_cache, full_path, trie_index);
            }
            
            return file;  // Found in Trie
        }
    }
    
    // LEVEL 3: Fallback to linear search (O(N) - only if Trie unavailable)
    if (last_slash) {
        // Filename contains path - parse it
        char folder_path[MAX_PATH];
        char base_filename[MAX_FILENAME];
        
        int folder_len = last_slash - filename;
        strncpy(folder_path, filename, folder_len);
        folder_path[folder_len] = '\0';
        strcpy(base_filename, last_slash + 1);
        
        // Search for file with matching folder_path and filename
        for (int i = 0; i < ns_state.file_count; i++) {
            if (strcmp(ns_state.files[i].filename, base_filename) == 0 &&
                strcmp(ns_state.files[i].folder_path, folder_path) == 0) {
                
                // Add to Trie and cache for future lookups
                if (ns_state.file_trie_root) {
                    trie_insert(ns_state.file_trie_root, full_path, i);
                }
                if (ns_state.file_cache) {
                    cache_put(ns_state.file_cache, full_path, i);
                }
                
                return &ns_state.files[i];
            }
        }
    } else {
        // No path - search for file in root (empty folder_path)
        for (int i = 0; i < ns_state.file_count; i++) {
            if (strcmp(ns_state.files[i].filename, filename) == 0 &&
                ns_state.files[i].folder_path[0] == '\0') {
                
                // Add to Trie and cache
                if (ns_state.file_trie_root) {
                    trie_insert(ns_state.file_trie_root, full_path, i);
                }
                if (ns_state.file_cache) {
                    cache_put(ns_state.file_cache, full_path, i);
                }
                
                return &ns_state.files[i];
            }
        }
    }
    
    return NULL;
}

/**
 * nm_delete_file
 * @brief Remove a file entry from the registry and persist the change.
 *
 * Frees any ACL memory for the deleted file and compacts the in-memory
 * array of files. If the file isn't found, returns an error.
 *
 * @param filename Null-terminated filename to remove.
 * @return ERR_SUCCESS on success, or ERR_FILE_NOT_FOUND if the file
 *         does not exist.
 */
int nm_delete_file(const char* filename) {
    pthread_mutex_lock(&ns_state.lock);
    
    // Construct full path for Trie/cache invalidation
    FileMetadata* file = nm_find_file(filename);
    if (file) {
        char full_path[MAX_FULL_PATH];
        if (strlen(file->folder_path) > 0) {
            snprintf(full_path, MAX_FULL_PATH, "%s/%s", file->folder_path, file->filename);
        } else {
            snprintf(full_path, MAX_FULL_PATH, "%s", file->filename);
        }
        
        // Remove from Trie
        if (ns_state.file_trie_root) {
            trie_delete(ns_state.file_trie_root, full_path);
        }
        
        // Invalidate cache entry
        if (ns_state.file_cache) {
            cache_invalidate(ns_state.file_cache, full_path);
        }
    }
    
    int found = -1;
    for (int i = 0; i < ns_state.file_count; i++) {
        if (strcmp(ns_state.files[i].filename, filename) == 0) {
            found = i;
            break;
        }
    }
    
    if (found == -1) {
        pthread_mutex_unlock(&ns_state.lock);
        return ERR_FILE_NOT_FOUND;
    }
    
    // Free ACL
    if (ns_state.files[found].acl) {
        free(ns_state.files[found].acl);
    }
    
    // Shift remaining files
    for (int i = found; i < ns_state.file_count - 1; i++) {
        ns_state.files[i] = ns_state.files[i + 1];
    }
    ns_state.file_count--;
    
    pthread_mutex_unlock(&ns_state.lock);
    
    save_state();
    
    char msg[256];
    snprintf(msg, sizeof(msg), "Deleted file '%s'", filename);
    log_message("NM", "INFO", msg);
    
    return ERR_SUCCESS;
}

/**
 * nm_check_permission
 * @brief Verify whether `username` has read/write access to `filename`.
 *
 * Looks up the file and its ACL entries. If `need_write` is non-zero, the
 * function checks write permission; otherwise it checks read permission.
 *
 * @param filename Null-terminated filename to check.
 * @param username Null-terminated username requesting access.
 * @param need_write Non-zero to require write permission, zero to require read.
 * @return ERR_SUCCESS if permission granted, ERR_PERMISSION_DENIED if not,
 *         or ERR_FILE_NOT_FOUND if the file does not exist.
 */
int nm_check_permission(const char* filename, const char* username, int need_write) {
    pthread_mutex_lock(&ns_state.lock);
    
    FileMetadata* file = nm_find_file(filename);
    if (!file) {
        pthread_mutex_unlock(&ns_state.lock);
        return ERR_FILE_NOT_FOUND;
    }
    
    // Owner always has full permissions
    if (strcmp(file->owner, username) == 0) {
        pthread_mutex_unlock(&ns_state.lock);
        return ERR_SUCCESS;
    }
    
    // Check ACL
    for (int i = 0; i < file->acl_count; i++) {
        if (strcmp(file->acl[i].username, username) == 0) {
            if (need_write) {
                int has_permission = file->acl[i].write_permission;
                pthread_mutex_unlock(&ns_state.lock);
                return has_permission ? ERR_SUCCESS : ERR_PERMISSION_DENIED;
            } else {
                int has_permission = file->acl[i].read_permission;
                pthread_mutex_unlock(&ns_state.lock);
                return has_permission ? ERR_SUCCESS : ERR_PERMISSION_DENIED;
            }
        }
    }
    
    pthread_mutex_unlock(&ns_state.lock);
    return ERR_PERMISSION_DENIED;
}

/**
 * nm_register_storage_server
 * @brief Register or update a Storage Server's information in the Name Server.
 *
 * Adds a new StorageServerInfo entry or updates an existing one if the
 * `server_id` is already present. Marks the server active and records
 * heartbeat time. Does not verify network reachability.
 *
 * @param server_id Numeric identifier for the storage server.
 * @param ip Null-terminated IPv4 address string for the storage server.
 * @param nm_port Port number the storage server uses to contact NM (unused by
 *                some workflows but stored for completeness).
 * @param client_port Port number clients should use to contact the storage
 *                    server for data operations.
 * @return ERR_SUCCESS on success, or ERR_FILE_OPERATION_FAILED if registry
 *         capacity is exhausted.
 */
int nm_register_storage_server(int server_id, const char* ip, int nm_port, int client_port) {
    pthread_mutex_lock(&ns_state.lock);
    
    // Check if server already registered
    for (int i = 0; i < ns_state.ss_count; i++) {
        if (ns_state.storage_servers[i].server_id == server_id) {
            // Update existing server
            strcpy(ns_state.storage_servers[i].ip, ip);
            ns_state.storage_servers[i].nm_port = nm_port;
            ns_state.storage_servers[i].client_port = client_port;
            ns_state.storage_servers[i].is_active = 1;
            ns_state.storage_servers[i].last_heartbeat = time(NULL);
            pthread_mutex_unlock(&ns_state.lock);
            return ERR_SUCCESS;
        }
    }
    
    // Add new server
    if (ns_state.ss_count >= MAX_STORAGE_SERVERS) {
        pthread_mutex_unlock(&ns_state.lock);
        return ERR_FILE_OPERATION_FAILED;
    }
    
    StorageServerInfo* ss = &ns_state.storage_servers[ns_state.ss_count];
    ss->server_id = server_id;
    strcpy(ss->ip, ip);
    ss->nm_port = nm_port;
    ss->client_port = client_port;
    ss->is_active = 1;
    ss->last_heartbeat = time(NULL);
    ss->files = NULL;
    ss->file_count = 0;
    
    ns_state.ss_count++;
    
    pthread_mutex_unlock(&ns_state.lock);
    
    char msg[256];
    snprintf(msg, sizeof(msg), 
             "Registered Storage Server %d (%s:%d)", 
             server_id, ip, client_port);
    log_message("NM", "INFO", msg);
    
    return ERR_SUCCESS;
}

/**
 * nm_find_storage_server
 * @brief Return a pointer to a registered, active Storage Server by id.
 *
 * This function searches the in-memory storage server list and returns a
 * pointer to the StorageServerInfo if it exists and is active.
 *
 * @param ss_id Storage server id to look up.
 * @return Pointer to StorageServerInfo or NULL if not found/active.
 */
StorageServerInfo* nm_find_storage_server(int ss_id) {
    for (int i = 0; i < ns_state.ss_count; i++) {
        if (ns_state.storage_servers[i].server_id == ss_id && 
            ns_state.storage_servers[i].is_active) {
            return &ns_state.storage_servers[i];
        }
    }
    return NULL;
}

/**
 * nm_select_storage_server
 * @brief Choose an active storage server using a simple round-robin policy.
 *
 * Returns the server id of the selected storage server, or -1 if no active
 * storage servers are available.
 *
 * @return Storage server id >= 0 when successful, -1 on failure.
 */
int nm_select_storage_server(void) {
    static int last_selected = 0;
    
    pthread_mutex_lock(&ns_state.lock);
    
    if (ns_state.ss_count == 0) {
        pthread_mutex_unlock(&ns_state.lock);
        return -1;
    }
    
    // Find next active server
    for (int i = 0; i < ns_state.ss_count; i++) {
        int idx = (last_selected + i) % ns_state.ss_count;
        if (ns_state.storage_servers[idx].is_active) {
            last_selected = (idx + 1) % ns_state.ss_count;
            int ss_id = ns_state.storage_servers[idx].server_id;
            pthread_mutex_unlock(&ns_state.lock);
            return ss_id;
        }
    }
    
    pthread_mutex_unlock(&ns_state.lock);
    return -1;
}

/**
 * nm_add_access
 * @brief Add or update an ACL entry for a file.
 *
 * If `username` already exists in the file's ACL, permissions are updated.
 * Otherwise a new ACL entry is appended.
 *
 * @param filename Null-terminated filename to modify.
 * @param username Null-terminated username to add/update.
 * @param read Integer flag (0/1) to grant read permission.
 * @param write Integer flag (0/1) to grant write permission.
 * @return ERR_SUCCESS on success, or ERR_FILE_NOT_FOUND if file doesn't exist.
 */
int nm_add_access(const char* filename, const char* username, int read, int write) {
    pthread_mutex_lock(&ns_state.lock);
    
    FileMetadata* file = nm_find_file(filename);
    if (!file) {
        pthread_mutex_unlock(&ns_state.lock);
        return ERR_FILE_NOT_FOUND;
    }
    
    // Check if user already in ACL
    for (int i = 0; i < file->acl_count; i++) {
        if (strcmp(file->acl[i].username, username) == 0) {
            // Update permissions
            file->acl[i].read_permission = read;
            file->acl[i].write_permission = write;
            pthread_mutex_unlock(&ns_state.lock);
            save_state();
            return ERR_SUCCESS;
        }
    }
    
    // Add new ACL entry
    file->acl = realloc(file->acl, sizeof(AccessControlEntry) * (file->acl_count + 1));
    strcpy(file->acl[file->acl_count].username, username);
    file->acl[file->acl_count].read_permission = read;
    file->acl[file->acl_count].write_permission = write;
    file->acl_count++;
    
    pthread_mutex_unlock(&ns_state.lock);
    
    save_state();
    
    char msg[256];
    snprintf(msg, sizeof(msg), 
             "Granted access to '%s' (read:%d write:%d)", 
             filename, read, write);
    log_message("NM", "INFO", msg);
    
    return ERR_SUCCESS;
}

/**
 * nm_remove_access
 * @brief Remove an ACL entry for `username` on `filename`.
 *
 * The file owner cannot be removed. Returns an error when the user is not
 * present in the ACL or the file does not exist.
 *
 * @param filename Null-terminated filename to modify.
 * @param username Null-terminated username to remove from ACL.
 * @return ERR_SUCCESS on success, or ERR_USER_NOT_FOUND / ERR_PERMISSION_DENIED
 *         / ERR_FILE_NOT_FOUND on failure.
 */
int nm_remove_access(const char* filename, const char* username) {
    pthread_mutex_lock(&ns_state.lock);
    
    FileMetadata* file = nm_find_file(filename);
    if (!file) {
        pthread_mutex_unlock(&ns_state.lock);
        return ERR_FILE_NOT_FOUND;
    }
    
    // Don't allow removing owner
    if (strcmp(file->owner, username) == 0) {
        pthread_mutex_unlock(&ns_state.lock);
        return ERR_PERMISSION_DENIED;
    }
    
    // Find and remove entry
    int found = -1;
    for (int i = 0; i < file->acl_count; i++) {
        if (strcmp(file->acl[i].username, username) == 0) {
            found = i;
            break;
        }
    }
    
    if (found == -1) {
        pthread_mutex_unlock(&ns_state.lock);
        return ERR_USER_NOT_FOUND;
    }
    
    // Shift remaining entries
    for (int i = found; i < file->acl_count - 1; i++) {
        file->acl[i] = file->acl[i + 1];
    }
    file->acl_count--;
    
    pthread_mutex_unlock(&ns_state.lock);
    
    save_state();
    
    char msg[256];
    snprintf(msg, sizeof(msg), "Revoked access to '%s'", filename);
    log_message("NM", "INFO", msg);
    
    return ERR_SUCCESS;
}

/**
 * nm_find_file_in_folder
 * @brief Find a file within a specific folder.
 *
 * @param filename File to find.
 * @param folder_path Folder path to search in.
 * @return Pointer to FileMetadata or NULL if not found.
 */
FileMetadata* nm_find_file_in_folder(const char* filename, const char* folder_path) {
    for (int i = 0; i < ns_state.file_count; i++) {
        if (strcmp(ns_state.files[i].filename, filename) == 0 &&
            strcmp(ns_state.files[i].folder_path, folder_path ? folder_path : "") == 0) {
            return &ns_state.files[i];
        }
    }
    return NULL;
}

/**
 * nm_move_file
 * @brief Move a file to a different folder.
 *
 * @param filename File to move.
 * @param new_folder_path Destination folder path.
 * @return ERR_SUCCESS on success, error code otherwise.
 */
int nm_move_file(const char* filename, const char* new_folder_path) {
    pthread_mutex_lock(&ns_state.lock);
    
    FileMetadata* file = nm_find_file(filename);
    if (!file) {
        pthread_mutex_unlock(&ns_state.lock);
        return ERR_FILE_NOT_FOUND;
    }
    
    // Check if destination folder exists (unless moving to root)
    if (new_folder_path && strlen(new_folder_path) > 0) {
        FolderMetadata* folder = nm_find_folder(new_folder_path);
        if (!folder) {
            pthread_mutex_unlock(&ns_state.lock);
            return ERR_FOLDER_NOT_FOUND;
        }
    }
    
    // Check if file with same name already exists in destination
    for (int i = 0; i < ns_state.file_count; i++) {
        if (strcmp(ns_state.files[i].filename, filename) == 0 &&
            strcmp(ns_state.files[i].folder_path, new_folder_path ? new_folder_path : "") == 0 &&
            &ns_state.files[i] != file) {
            pthread_mutex_unlock(&ns_state.lock);
            return ERR_FILE_EXISTS;
        }
    }
    
    // Construct old and new full paths for Trie/cache update
    char old_full_path[MAX_FULL_PATH];
    char new_full_path[MAX_FULL_PATH];
    
    if (strlen(file->folder_path) > 0) {
        snprintf(old_full_path, sizeof(old_full_path), "%s/%s", file->folder_path, file->filename);
    } else {
        snprintf(old_full_path, sizeof(old_full_path), "%s", file->filename);
    }
    
    if (new_folder_path && strlen(new_folder_path) > 0) {
        snprintf(new_full_path, sizeof(new_full_path), "%s/%s", new_folder_path, file->filename);
    } else {
        snprintf(new_full_path, sizeof(new_full_path), "%s", file->filename);
    }
    
    // Find the file index for Trie/cache operations
    int file_index = file - ns_state.files;
    
    // Remove old path from Trie and cache
    if (ns_state.file_trie_root) {
        trie_delete(ns_state.file_trie_root, old_full_path);
    }
    if (ns_state.file_cache) {
        cache_invalidate(ns_state.file_cache, old_full_path);
    }
    
    // Update folder path
    strcpy(file->folder_path, new_folder_path ? new_folder_path : "");
    file->last_modified = time(NULL);
    
    // Add new path to Trie and cache
    if (ns_state.file_trie_root) {
        trie_insert(ns_state.file_trie_root, new_full_path, file_index);
    }
    if (ns_state.file_cache) {
        cache_put(ns_state.file_cache, new_full_path, file_index);
    }
    
    pthread_mutex_unlock(&ns_state.lock);
    save_state();
    
    char msg[256];
    snprintf(msg, sizeof(msg), 
             "Moved file '%s' to folder '%s'", 
             filename, new_folder_path ? new_folder_path : "/");
    log_message("NM", "INFO", msg);
    
    return ERR_SUCCESS;
}

/**
 * nm_create_folder
 * @brief Create a new folder in the hierarchy.
 *
 * @param foldername Full path of folder to create (e.g., "folder1" or "folder1/folder2").
 * @param owner Username of folder owner.
 * @return ERR_SUCCESS on success, error code otherwise.
 */
int nm_create_folder(const char* foldername, const char* owner) {
    pthread_mutex_lock(&ns_state.lock);
    
    // Check if folder already exists
    for (int i = 0; i < ns_state.folder_count; i++) {
        if (strcmp(ns_state.folders[i].foldername, foldername) == 0) {
            pthread_mutex_unlock(&ns_state.lock);
            return ERR_FOLDER_EXISTS;
        }
    }
    
    // Check capacity
    if (ns_state.folder_count >= MAX_FOLDERS) {
        pthread_mutex_unlock(&ns_state.lock);
        return ERR_FILE_OPERATION_FAILED;
    }
    
    // Find parent folder index
    int parent_idx = -1;
    char* last_slash = strrchr(foldername, '/');
    if (last_slash) {
        // Has parent folder
        char parent_path[MAX_PATH];
        int len = last_slash - foldername;
        strncpy(parent_path, foldername, len);
        parent_path[len] = '\0';
        
        // Find parent
        for (int i = 0; i < ns_state.folder_count; i++) {
            if (strcmp(ns_state.folders[i].foldername, parent_path) == 0) {
                parent_idx = i;
                break;
            }
        }
        
        if (parent_idx == -1) {
            pthread_mutex_unlock(&ns_state.lock);
            return ERR_FOLDER_NOT_FOUND;  // Parent doesn't exist
        }
    }
    
    // Create folder
    FolderMetadata* folder = &ns_state.folders[ns_state.folder_count];
    strcpy(folder->foldername, foldername);
    strcpy(folder->owner, owner);
    folder->created_time = time(NULL);
    folder->parent_folder_idx = parent_idx;
    
    // Initialize ACL with owner having full access
    folder->acl = malloc(sizeof(AccessControlEntry));
    strcpy(folder->acl[0].username, owner);
    folder->acl[0].read_permission = 1;
    folder->acl[0].write_permission = 1;
    folder->acl_count = 1;
    
    ns_state.folder_count++;
    
    pthread_mutex_unlock(&ns_state.lock);
    save_state();
    
    char msg[256];
    snprintf(msg, sizeof(msg), "Created folder '%s'", foldername);
    log_message("NM", "INFO", msg);
    
    return ERR_SUCCESS;
}

/**
 * nm_find_folder
 * @brief Find a folder by its full path.
 *
 * @param foldername Full folder path.
 * @return Pointer to FolderMetadata or NULL if not found.
 */
FolderMetadata* nm_find_folder(const char* foldername) {
    for (int i = 0; i < ns_state.folder_count; i++) {
        if (strcmp(ns_state.folders[i].foldername, foldername) == 0) {
            return &ns_state.folders[i];
        }
    }
    return NULL;
}

/**
 * nm_check_folder_permission
 * @brief Check if a user has permission to access a folder.
 *
 * @param foldername Folder to check.
 * @param username User requesting access.
 * @param need_write 1 if write access needed, 0 for read.
 * @return ERR_SUCCESS if allowed, error code otherwise.
 */
int nm_check_folder_permission(const char* foldername, const char* username, int need_write) {
    FolderMetadata* folder = nm_find_folder(foldername);
    if (!folder) {
        return ERR_FOLDER_NOT_FOUND;
    }
    
    // Owner always has access
    if (strcmp(folder->owner, username) == 0) {
        return ERR_SUCCESS;
    }
    
    // Check ACL
    for (int i = 0; i < folder->acl_count; i++) {
        if (strcmp(folder->acl[i].username, username) == 0) {
            if (need_write && !folder->acl[i].write_permission) {
                return ERR_PERMISSION_DENIED;
            }
            if (!folder->acl[i].read_permission) {
                return ERR_PERMISSION_DENIED;
            }
            return ERR_SUCCESS;
        }
    }
    
    return ERR_PERMISSION_DENIED;
}

/**
 * nm_list_folder_contents
 * @brief List all files and subfolders in a folder.
 *
 * @param foldername Folder to list.
 * @param username User requesting the list.
 * @param buffer Buffer to store results.
 * @param buffer_size Size of buffer.
 * @return ERR_SUCCESS on success, error code otherwise.
 */
int nm_list_folder_contents(const char* foldername, const char* username, 
                            char* buffer, size_t buffer_size) {
    buffer[0] = '\0';
    
    // Check folder permission
    if (foldername && strlen(foldername) > 0) {
        int perm = nm_check_folder_permission(foldername, username, 0);
        if (perm != ERR_SUCCESS) {
            return perm;
        }
    }
    
    // List subfolders
    int found_any = 0;
    for (int i = 0; i < ns_state.folder_count; i++) {
        FolderMetadata* folder = &ns_state.folders[i];
        
        // Check if this folder is a direct child
        if (foldername && strlen(foldername) > 0) {
            // Looking in a specific folder
            if (strncmp(folder->foldername, foldername, strlen(foldername)) == 0) {
                const char* rest = folder->foldername + strlen(foldername);
                if (rest[0] == '/') rest++;  // Skip leading slash
                
                // Check if it's a direct child (no more slashes)
                if (rest[0] != '\0' && strchr(rest, '/') == NULL) {
                    char line[512];
                    snprintf(line, sizeof(line), "[DIR]  %s\n", rest);
                    strncat(buffer, line, buffer_size - strlen(buffer) - 1);
                    found_any = 1;
                }
            }
        } else {
            // Root folder - list top-level folders only
            if (strchr(folder->foldername, '/') == NULL) {
                char line[512];
                snprintf(line, sizeof(line), "[DIR]  %s\n", folder->foldername);
                strncat(buffer, line, buffer_size - strlen(buffer) - 1);
                found_any = 1;
            }
        }
    }
    
    // List files in this folder
    for (int i = 0; i < ns_state.file_count; i++) {
        FileMetadata* file = &ns_state.files[i];
        
        if (strcmp(file->folder_path, foldername ? foldername : "") == 0) {
            // Check if user has access
            int has_access = 0;
            for (int j = 0; j < file->acl_count; j++) {
                if (strcmp(file->acl[j].username, username) == 0) {
                    has_access = 1;
                    break;
                }
            }
            
            if (has_access || strcmp(file->owner, username) == 0) {
                char line[512];
                snprintf(line, sizeof(line), "[FILE] %s\n", file->filename);
                strncat(buffer, line, buffer_size - strlen(buffer) - 1);
                found_any = 1;
            }
        }
    }
    
    if (!found_any) {
        strcpy(buffer, "(empty folder)\n");
    }
    
    return ERR_SUCCESS;
}

/**
 * nm_add_folder_access
 * @brief Add or update ACL for a folder.
 *
 * @param foldername Folder to modify.
 * @param username User to grant access to.
 * @param read Read permission flag.
 * @param write Write permission flag.
 * @return ERR_SUCCESS on success, error code otherwise.
 */
int nm_add_folder_access(const char* foldername, const char* username, int read, int write) {
    pthread_mutex_lock(&ns_state.lock);
    
    FolderMetadata* folder = nm_find_folder(foldername);
    if (!folder) {
        pthread_mutex_unlock(&ns_state.lock);
        return ERR_FOLDER_NOT_FOUND;
    }
    
    // Check if user already in ACL
    for (int i = 0; i < folder->acl_count; i++) {
        if (strcmp(folder->acl[i].username, username) == 0) {
            // Update permissions
            folder->acl[i].read_permission = read;
            folder->acl[i].write_permission = write;
            pthread_mutex_unlock(&ns_state.lock);
            save_state();
            return ERR_SUCCESS;
        }
    }
    
    // Add new ACL entry
    folder->acl = realloc(folder->acl, sizeof(AccessControlEntry) * (folder->acl_count + 1));
    strcpy(folder->acl[folder->acl_count].username, username);
    folder->acl[folder->acl_count].read_permission = read;
    folder->acl[folder->acl_count].write_permission = write;
    folder->acl_count++;
    
    pthread_mutex_unlock(&ns_state.lock);
    save_state();
    
    return ERR_SUCCESS;
}

/**
 * nm_request_access
 * @brief Submit a request to access a file
 */
int nm_request_access(const char* filename, const char* requester, int read_requested, int write_requested) {
    pthread_mutex_lock(&ns_state.lock);
    
    // Check if file exists
    FileMetadata* file = nm_find_file(filename);
    if (!file) {
        pthread_mutex_unlock(&ns_state.lock);
        return ERR_FILE_NOT_FOUND;
    }
    
    // Check if requester is the owner
    if (strcmp(file->owner, requester) == 0) {
        pthread_mutex_unlock(&ns_state.lock);
        return ERR_SUCCESS;  // Owner already has access
    }
    
    // Check if requester already has sufficient access
    for (int i = 0; i < file->acl_count; i++) {
        if (strcmp(file->acl[i].username, requester) == 0) {
            // Check if they already have the requested permissions
            int has_sufficient = 1;
            if (read_requested && !file->acl[i].read_permission) has_sufficient = 0;
            if (write_requested && !file->acl[i].write_permission) has_sufficient = 0;
            if (has_sufficient) {
                pthread_mutex_unlock(&ns_state.lock);
                // Return special code to indicate they already have access
                // Store what access they have in flags for the client to read
                return ERR_ALREADY_HAS_ACCESS;
            }
        }
    }
    
    // Check if request already exists - if so, update it
    for (int i = 0; i < ns_state.request_count; i++) {
        if (strcmp(ns_state.access_requests[i].filename, filename) == 0 &&
            strcmp(ns_state.access_requests[i].requester, requester) == 0) {
            // Update existing request
            ns_state.access_requests[i].read_requested = read_requested;
            ns_state.access_requests[i].write_requested = write_requested;
            ns_state.access_requests[i].request_time = time(NULL);
            pthread_mutex_unlock(&ns_state.lock);
            save_state();
            return ERR_SUCCESS;
        }
    }
    
    // Add new request
    if (ns_state.request_count >= MAX_FILES) {
        pthread_mutex_unlock(&ns_state.lock);
        return ERR_FILE_OPERATION_FAILED;  // Too many requests
    }
    
    strcpy(ns_state.access_requests[ns_state.request_count].filename, filename);
    strcpy(ns_state.access_requests[ns_state.request_count].requester, requester);
    ns_state.access_requests[ns_state.request_count].request_time = time(NULL);
    ns_state.access_requests[ns_state.request_count].read_requested = read_requested;
    ns_state.access_requests[ns_state.request_count].write_requested = write_requested;
    ns_state.request_count++;
    
    pthread_mutex_unlock(&ns_state.lock);
    save_state();
    
    return ERR_SUCCESS;
}

/**
 * nm_view_requests
 * @brief View all pending access requests for a file (owner only)
 */
int nm_view_requests(const char* filename, const char* owner, char* buffer, size_t buffer_size) {
    pthread_mutex_lock(&ns_state.lock);
    
    // Check if file exists
    FileMetadata* file = nm_find_file(filename);
    if (!file) {
        pthread_mutex_unlock(&ns_state.lock);
        return ERR_FILE_NOT_FOUND;
    }
    
    // Check if caller is owner
    if (strcmp(file->owner, owner) != 0) {
        pthread_mutex_unlock(&ns_state.lock);
        return ERR_NOT_OWNER;
    }
    
    // Build list of requests
    char temp[BUFFER_SIZE * 2] = "";
    int count = 0;
    
    for (int i = 0; i < ns_state.request_count; i++) {
        if (strcmp(ns_state.access_requests[i].filename, filename) == 0) {
            char time_str[64];
            struct tm* tm_info = localtime(&ns_state.access_requests[i].request_time);
            strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);
            
            // Determine permission type
            char perm_str[32];
            if (ns_state.access_requests[i].read_requested && ns_state.access_requests[i].write_requested) {
                strcpy(perm_str, "Read+Write");
            } else if (ns_state.access_requests[i].write_requested) {
                strcpy(perm_str, "Write");
            } else {
                strcpy(perm_str, "Read");
            }
            
            char line[256];
            snprintf(line, sizeof(line), "  [%s] - %s access - Requested on %s\n",
                     ns_state.access_requests[i].requester, perm_str, time_str);
            
            if (strlen(temp) + strlen(line) < sizeof(temp) - 1) {
                strcat(temp, line);
                count++;
            }
        }
    }
    
    if (count == 0) {
        snprintf(buffer, buffer_size, "No pending access requests for '%s'.\n", filename);
    } else {
        snprintf(buffer, buffer_size, "Pending access requests for '%s' (%d total):\n%s",
                 filename, count, temp);
    }
    
    pthread_mutex_unlock(&ns_state.lock);
    return ERR_SUCCESS;
}

/**
 * nm_approve_request
 * @brief Approve an access request and grant read access
 */
int nm_approve_request(const char* filename, const char* owner, const char* requester) {
    pthread_mutex_lock(&ns_state.lock);
    
    // Check if file exists
    FileMetadata* file = nm_find_file(filename);
    if (!file) {
        pthread_mutex_unlock(&ns_state.lock);
        return ERR_FILE_NOT_FOUND;
    }
    
    // Check if caller is owner
    if (strcmp(file->owner, owner) != 0) {
        pthread_mutex_unlock(&ns_state.lock);
        return ERR_NOT_OWNER;
    }
    
    // Find the request and get the requested permissions
    int found = 0;
    int read_requested = 0;
    int write_requested = 0;
    
    for (int i = 0; i < ns_state.request_count; i++) {
        if (strcmp(ns_state.access_requests[i].filename, filename) == 0 &&
            strcmp(ns_state.access_requests[i].requester, requester) == 0) {
            // Save the requested permissions
            read_requested = ns_state.access_requests[i].read_requested;
            write_requested = ns_state.access_requests[i].write_requested;
            
            // Remove request by shifting array
            for (int j = i; j < ns_state.request_count - 1; j++) {
                ns_state.access_requests[j] = ns_state.access_requests[j + 1];
            }
            ns_state.request_count--;
            found = 1;
            break;
        }
    }
    
    if (!found) {
        pthread_mutex_unlock(&ns_state.lock);
        return ERR_REQUEST_NOT_FOUND;
    }
    
    pthread_mutex_unlock(&ns_state.lock);
    
    // Grant the requested permissions
    int result = nm_add_access(filename, requester, read_requested, write_requested);
    
    return result;
}

/**
 * nm_deny_request
 * @brief Deny an access request
 */
int nm_deny_request(const char* filename, const char* owner, const char* requester) {
    pthread_mutex_lock(&ns_state.lock);
    
    // Check if file exists
    FileMetadata* file = nm_find_file(filename);
    if (!file) {
        pthread_mutex_unlock(&ns_state.lock);
        return ERR_FILE_NOT_FOUND;
    }
    
    // Check if caller is owner
    if (strcmp(file->owner, owner) != 0) {
        pthread_mutex_unlock(&ns_state.lock);
        return ERR_NOT_OWNER;
    }
    
    // Find and remove the request
    int found = 0;
    for (int i = 0; i < ns_state.request_count; i++) {
        if (strcmp(ns_state.access_requests[i].filename, filename) == 0 &&
            strcmp(ns_state.access_requests[i].requester, requester) == 0) {
            // Remove request by shifting array
            for (int j = i; j < ns_state.request_count - 1; j++) {
                ns_state.access_requests[j] = ns_state.access_requests[j + 1];
            }
            ns_state.request_count--;
            found = 1;
            break;
        }
    }
    
    if (!found) {
        pthread_mutex_unlock(&ns_state.lock);
        return ERR_REQUEST_NOT_FOUND;
    }
    
    pthread_mutex_unlock(&ns_state.lock);
    save_state();
    
    return ERR_SUCCESS;
}

/**
 * save_state
 * @brief Persist the in-memory Name Server registry to disk (`data/nm_state.dat`).
 *
 * The file format is simple and intended only for recovery between runs. If
 * writing fails the function returns silently; callers should log if needed.
 */
void save_state(void) {
    FILE* f = fopen("data/nm_state.dat", "w");
    if (!f) return;
    
    // Save files
    fprintf(f, "%d\n", ns_state.file_count);
    for (int i = 0; i < ns_state.file_count; i++) {
        FileMetadata* file = &ns_state.files[i];
        fprintf(f, "%s|%s|%s|%d|%ld|%ld|%ld|%ld|%d|%d|%d\n",
                file->filename, file->folder_path, file->owner, file->ss_id,
                file->created_time, file->last_modified, file->last_accessed,
                file->file_size, file->word_count, file->char_count, file->acl_count);
        
        for (int j = 0; j < file->acl_count; j++) {
            fprintf(f, "%s|%d|%d\n",
                    file->acl[j].username,
                    file->acl[j].read_permission,
                    file->acl[j].write_permission);
        }
    }
    
    // Save folders
    fprintf(f, "%d\n", ns_state.folder_count);
    for (int i = 0; i < ns_state.folder_count; i++) {
        FolderMetadata* folder = &ns_state.folders[i];
        fprintf(f, "%s|%s|%ld|%d|%d\n",
                folder->foldername, folder->owner,
                folder->created_time, folder->parent_folder_idx, folder->acl_count);
        
        for (int j = 0; j < folder->acl_count; j++) {
            fprintf(f, "%s|%d|%d\n",
                    folder->acl[j].username,
                    folder->acl[j].read_permission,
                    folder->acl[j].write_permission);
        }
    }
    
    // Save access requests
    fprintf(f, "%d\n", ns_state.request_count);
    for (int i = 0; i < ns_state.request_count; i++) {
        AccessRequest* req = &ns_state.access_requests[i];
        fprintf(f, "%s|%s|%ld|%d|%d\n",
                req->filename, req->requester, req->request_time,
                req->read_requested, req->write_requested);
    }
    
    fclose(f);
}

/**
 * load_state
 * @brief Load the Name Server registry from disk (`data/nm_state.dat`) into
 *        memory.
 *
 * If the state file is missing the function does nothing. Any malformed
 * entries may produce undefined behavior; the format is expected to match
 * `save_state`'s output.
 */
void load_state(void) {
    FILE* f = fopen("data/nm_state.dat", "r");
    if (!f) return;
    
    // Load files
    fscanf(f, "%d\n", &ns_state.file_count);
    for (int i = 0; i < ns_state.file_count; i++) {
        FileMetadata* file = &ns_state.files[i];
        fscanf(f, "%[^|]|%[^|]|%[^|]|%d|%ld|%ld|%ld|%ld|%d|%d|%d\n",
               file->filename, file->folder_path, file->owner, &file->ss_id,
               &file->created_time, &file->last_modified, &file->last_accessed,
               &file->file_size, &file->word_count, &file->char_count, &file->acl_count);
        
        file->acl = malloc(sizeof(AccessControlEntry) * file->acl_count);
        for (int j = 0; j < file->acl_count; j++) {
            fscanf(f, "%[^|]|%d|%d\n",
                   file->acl[j].username,
                   &file->acl[j].read_permission,
                   &file->acl[j].write_permission);
        }
    }
    
    // Load folders
    if (fscanf(f, "%d\n", &ns_state.folder_count) == 1) {
        for (int i = 0; i < ns_state.folder_count; i++) {
            FolderMetadata* folder = &ns_state.folders[i];
            fscanf(f, "%[^|]|%[^|]|%ld|%d|%d\n",
                   folder->foldername, folder->owner,
                   &folder->created_time, &folder->parent_folder_idx, &folder->acl_count);
            
            folder->acl = malloc(sizeof(AccessControlEntry) * folder->acl_count);
            for (int j = 0; j < folder->acl_count; j++) {
                fscanf(f, "%[^|]|%d|%d\n",
                       folder->acl[j].username,
                       &folder->acl[j].read_permission,
                       &folder->acl[j].write_permission);
            }
        }
    }
    
    // Load access requests
    if (fscanf(f, "%d\n", &ns_state.request_count) == 1) {
        for (int i = 0; i < ns_state.request_count; i++) {
            AccessRequest* req = &ns_state.access_requests[i];
            if (fscanf(f, "%[^|]|%[^|]|%ld|%d|%d\n",
                   req->filename, req->requester, &req->request_time,
                   &req->read_requested, &req->write_requested) < 3) {
                // Old format without permissions - default to read only
                req->read_requested = 1;
                req->write_requested = 0;
            }
        }
    }
    
    fclose(f);
    log_message("NM", "INFO", "Loaded persistent state");
    
    // Rebuild Trie from loaded files for efficient search
    if (ns_state.file_trie_root) {
        for (int i = 0; i < ns_state.file_count; i++) {
            FileMetadata* file = &ns_state.files[i];
            char full_path[MAX_FULL_PATH];
            
            if (strlen(file->folder_path) > 0) {
                snprintf(full_path, MAX_FULL_PATH, "%s/%s", file->folder_path, file->filename);
            } else {
                snprintf(full_path, MAX_FULL_PATH, "%s", file->filename);
            }
            
            trie_insert(ns_state.file_trie_root, full_path, i);
        }
        
        char msg[128];
        snprintf(msg, sizeof(msg), "Rebuilt Trie with %d files", ns_state.file_count);
        log_message("NM", "INFO", msg);
    }
}

/**
 * nm_print_search_stats
 * @brief Print statistics about search performance for monitoring.
 */
void nm_print_search_stats(void) {
    if (ns_state.file_cache) {
        cache_print_stats(ns_state.file_cache);
    }
    
    char msg[256];
    snprintf(msg, sizeof(msg), "Total files indexed: %d", ns_state.file_count);
    log_message("NM", "INFO", msg);
}
