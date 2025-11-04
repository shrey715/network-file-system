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
 * @param owner Null-terminated username who will own the file.
 * @param ss_id ID of the Storage Server assigned to hold the file.
 * @return ERR_SUCCESS on success, or an ERR_* code on failure (e.g.
 *         ERR_FILE_EXISTS, ERR_FILE_OPERATION_FAILED).
 */
int nm_register_file(const char* filename, const char* owner, int ss_id) {
    pthread_mutex_lock(&ns_state.lock);
    
    // Check if file already exists
    for (int i = 0; i < ns_state.file_count; i++) {
        if (strcmp(ns_state.files[i].filename, filename) == 0) {
            pthread_mutex_unlock(&ns_state.lock);
            return ERR_FILE_EXISTS;
        }
    }
    
    // Add new file
    if (ns_state.file_count >= MAX_FILES) {
        pthread_mutex_unlock(&ns_state.lock);
        return ERR_FILE_OPERATION_FAILED;
    }
    
    FileMetadata* file = &ns_state.files[ns_state.file_count];
    strcpy(file->filename, filename);
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
    
    ns_state.file_count++;
    
    pthread_mutex_unlock(&ns_state.lock);
    
    save_state();
    
    char msg[256];
    snprintf(msg, sizeof(msg), "Registered file: %s (owner: %s, SS: %d)", 
             filename, owner, ss_id);
    log_message("NM", "INFO", msg);
    
    return ERR_SUCCESS;
}

/**
 * nm_find_file
 * @brief Look up a file by its filename in the in-memory registry.
 *
 * This performs a linear search through the registered files and returns a
 * pointer to the FileMetadata if found. The caller must not free the returned
 * pointer.
 *
 * @param filename Null-terminated filename to search for.
 * @return Pointer to FileMetadata on success, or NULL if not found.
 */
FileMetadata* nm_find_file(const char* filename) {
    for (int i = 0; i < ns_state.file_count; i++) {
        if (strcmp(ns_state.files[i].filename, filename) == 0) {
            return &ns_state.files[i];
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
    snprintf(msg, sizeof(msg), "Deleted file: %s", filename);
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
    snprintf(msg, sizeof(msg), "Registered Storage Server: %d (%s:%d)", 
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
    snprintf(msg, sizeof(msg), "Added access: %s for user %s (R:%d W:%d)", 
             filename, username, read, write);
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
    snprintf(msg, sizeof(msg), "Removed access: %s for user %s", filename, username);
    log_message("NM", "INFO", msg);
    
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
    
    fprintf(f, "%d\n", ns_state.file_count);
    for (int i = 0; i < ns_state.file_count; i++) {
        FileMetadata* file = &ns_state.files[i];
        fprintf(f, "%s|%s|%d|%ld|%ld|%ld|%ld|%d|%d|%d\n",
                file->filename, file->owner, file->ss_id,
                file->created_time, file->last_modified, file->last_accessed,
                file->file_size, file->word_count, file->char_count, file->acl_count);
        
        for (int j = 0; j < file->acl_count; j++) {
            fprintf(f, "%s|%d|%d\n",
                    file->acl[j].username,
                    file->acl[j].read_permission,
                    file->acl[j].write_permission);
        }
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
    
    fscanf(f, "%d\n", &ns_state.file_count);
    for (int i = 0; i < ns_state.file_count; i++) {
        FileMetadata* file = &ns_state.files[i];
        fscanf(f, "%[^|]|%[^|]|%d|%ld|%ld|%ld|%ld|%d|%d|%d\n",
               file->filename, file->owner, &file->ss_id,
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
    
    fclose(f);
    log_message("NM", "INFO", "Loaded persistent state");
}
