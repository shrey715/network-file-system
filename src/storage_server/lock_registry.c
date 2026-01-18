#include "common.h"
#include "storage_server.h"

static LockedFile locked_files[MAX_LOCKED_FILES];
static int locked_file_count = 0;
static pthread_mutex_t registry_mutex = PTHREAD_MUTEX_INITIALIZER;
static int registry_initialized = 0;

/**
 * init_locked_file_registry
 * @brief Initialize the locked file registry (thread-safe, idempotent)
 */
void init_locked_file_registry(void) {
    pthread_mutex_lock(&registry_mutex);
    if (!registry_initialized) {
        memset(locked_files, 0, sizeof(locked_files));
        locked_file_count = 0;
        registry_initialized = 1;
        log_message("SS", "INFO", "Locked file registry initialized");
    }
    pthread_mutex_unlock(&registry_mutex);
}

/**
 * cleanup_locked_file_registry
 * @brief Clean up all locks and free resources
 */
void cleanup_locked_file_registry(void) {
    pthread_mutex_lock(&registry_mutex);
    
    int cleaned = 0;
    for (int i = 0; i < MAX_LOCKED_FILES; i++) {
        if (locked_files[i].is_active) {
            if (locked_files[i].sentence_list_head) {
                free_sentence_list(locked_files[i].sentence_list_head);
                locked_files[i].sentence_list_head = NULL;
            }
            locked_files[i].locked_node = NULL;
            locked_files[i].is_active = 0;
            cleaned++;
        }
    }
    
    locked_file_count = 0;
    registry_initialized = 0;
    
    pthread_mutex_unlock(&registry_mutex);
    
    if (cleaned > 0) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Cleaned up %d active locks", cleaned);
        log_message("SS", "INFO", msg);
    }
}

/**
 * find_locked_file
 * @brief Find a locked file entry by filename and username
 * @return Pointer to LockedFile if found, NULL otherwise
 */
LockedFile* find_locked_file(const char* filename, const char* username) {
    pthread_mutex_lock(&registry_mutex);
    
    for (int i = 0; i < MAX_LOCKED_FILES; i++) {
        if (locked_files[i].is_active &&
            strcmp(locked_files[i].filename, filename) == 0 &&
            strcmp(locked_files[i].username, username) == 0) {
            pthread_mutex_unlock(&registry_mutex);
            return &locked_files[i];
        }
    }
    
    pthread_mutex_unlock(&registry_mutex);
    return NULL;
}

/**
 * add_locked_file
 * @brief Add a new locked file to the registry with node pointer
 * @return ERR_SUCCESS on success, error code otherwise
 */
int add_locked_file(const char* filename, const char* username, int sentence_idx,
                    SentenceNode* locked_node, SentenceNode* sentence_list_head, int sentence_count,
                    const char* original_text) {
    pthread_mutex_lock(&registry_mutex);
    
    // Find empty slot
    int slot = -1;
    for (int i = 0; i < MAX_LOCKED_FILES; i++) {
        if (!locked_files[i].is_active) {
            slot = i;
            break;
        }
    }
    
    if (slot == -1) {
        pthread_mutex_unlock(&registry_mutex);
        char errmsg[256];
        snprintf(errmsg, sizeof(errmsg), 
                 "Lock registry full (max %d locks) - cannot add lock for '%s' by '%s'", 
                 MAX_LOCKED_FILES, filename, username);
        log_message("SS", "WARN", errmsg);
        return ERR_FILE_OPERATION_FAILED;
    }
    
    // Store lock info
    strncpy(locked_files[slot].filename, filename, MAX_FILENAME - 1);
    locked_files[slot].filename[MAX_FILENAME - 1] = '\0';
    
    strncpy(locked_files[slot].username, username, MAX_USERNAME - 1);
    locked_files[slot].username[MAX_USERNAME - 1] = '\0';
    
    locked_files[slot].sentence_idx = sentence_idx;
    locked_files[slot].locked_node = locked_node;  // Store pointer to locked node
    locked_files[slot].sentence_list_head = sentence_list_head;  // Store head of sentence list
    locked_files[slot].sentence_count = sentence_count;
    
    // Store original text at lock time (before any edits)
    if (original_text) {
        strncpy(locked_files[slot].original_text, original_text, MAX_SENTENCE_CONTENT - 1);
        locked_files[slot].original_text[MAX_SENTENCE_CONTENT - 1] = '\0';
    } else {
        locked_files[slot].original_text[0] = '\0';
    }
    
    locked_files[slot].is_active = 1;
    locked_files[slot].undo_saved = 0;  // Reset for new edit session
    
    if (slot >= locked_file_count) {
        locked_file_count = slot + 1;
    }
    
    pthread_mutex_unlock(&registry_mutex);
    
    char msg[512];
    const char* node_text = locked_node && locked_node->text ? locked_node->text : "(empty)";
    snprintf(msg, sizeof(msg), 
             "Lock acquired on '%s' sentence %d (node: '%.50s%s') (active locks: %d)", 
             filename, sentence_idx, 
             node_text,
             node_text && strlen(node_text) > 50 ? "..." : "",
             locked_file_count);
    log_message("SS", "INFO", msg);
    
    return ERR_SUCCESS;
}

/**
 * check_lock
 * @brief Check if a sentence is locked and verify ownership
 * @return 1 if locked by username, -1 if locked by someone else, 0 if not locked
 */
int check_lock(const char* filename, int sentence_idx, const char* username) {
    pthread_mutex_lock(&registry_mutex);
    
    for (int i = 0; i < MAX_LOCKED_FILES; i++) {
        if (locked_files[i].is_active &&
            strcmp(locked_files[i].filename, filename) == 0 &&
            locked_files[i].sentence_idx == sentence_idx) {
            
            int is_owner = strcmp(locked_files[i].username, username) == 0;
            pthread_mutex_unlock(&registry_mutex);
            return is_owner ? 1 : -1;
        }
    }
    
    pthread_mutex_unlock(&registry_mutex);
    return 0; // Not locked
}

/**
 * remove_lock
 * @brief Remove a lock from the registry
 * @return ERR_SUCCESS on success, error code otherwise
 */
int remove_lock(const char* filename, int sentence_idx) {
    pthread_mutex_lock(&registry_mutex);
    
    for (int i = 0; i < MAX_LOCKED_FILES; i++) {
        if (locked_files[i].is_active &&
            strcmp(locked_files[i].filename, filename) == 0 &&
            locked_files[i].sentence_idx == sentence_idx) {
            
            // Free sentence list if allocated
            if (locked_files[i].sentence_list_head) {
                free_sentence_list(locked_files[i].sentence_list_head);
                locked_files[i].sentence_list_head = NULL;
            }
            locked_files[i].locked_node = NULL;
            
            locked_files[i].is_active = 0;
            locked_files[i].sentence_count = 0;
            
            pthread_mutex_unlock(&registry_mutex);
            
            char msg[256];
            snprintf(msg, sizeof(msg), 
                     "Lock released on '%s' sentence %d", 
                     filename, sentence_idx);
            log_message("SS", "INFO", msg);
            
            return ERR_SUCCESS;
        }
    }
    
    pthread_mutex_unlock(&registry_mutex);
    
    char msg[256];
    snprintf(msg, sizeof(msg), 
             "Attempted to remove non-existent lock on '%s' sentence %d", 
             filename, sentence_idx);
    log_message("SS", "WARN", msg);
    return ERR_PERMISSION_DENIED;
}

/**
 * check_lock_by_node
 * @brief Check if a specific sentence node is locked
 * @return 1 if locked by username, -1 if locked by someone else, 0 if not locked
 */
int check_lock_by_node(const char* filename, SentenceNode* node, const char* username) {
    pthread_mutex_lock(&registry_mutex);
    
    for (int i = 0; i < MAX_LOCKED_FILES; i++) {
        if (locked_files[i].is_active &&
            strcmp(locked_files[i].filename, filename) == 0 &&
            locked_files[i].locked_node == node) {
            
            int is_owner = strcmp(locked_files[i].username, username) == 0;
            pthread_mutex_unlock(&registry_mutex);
            return is_owner ? 1 : -1;
        }
    }
    
    pthread_mutex_unlock(&registry_mutex);
    return 0; // Not locked
}

/**
 * remove_lock_by_node
 * @brief Remove a lock from the registry by node pointer
 * @return ERR_SUCCESS on success, error code otherwise
 */
int remove_lock_by_node(const char* filename, SentenceNode* node) {
    pthread_mutex_lock(&registry_mutex);
    
    for (int i = 0; i < MAX_LOCKED_FILES; i++) {
        if (locked_files[i].is_active &&
            strcmp(locked_files[i].filename, filename) == 0 &&
            locked_files[i].locked_node == node) {
            
            // Free sentence list if allocated
            if (locked_files[i].sentence_list_head) {
                free_sentence_list(locked_files[i].sentence_list_head);
                locked_files[i].sentence_list_head = NULL;
            }
            locked_files[i].locked_node = NULL;
            
            locked_files[i].is_active = 0;
            locked_files[i].sentence_count = 0;
            
            pthread_mutex_unlock(&registry_mutex);
            
            char msg[256];
            snprintf(msg, sizeof(msg), 
                     "Lock released on '%s' (node-based)", 
                     filename);
            log_message("SS", "INFO", msg);
            
            return ERR_SUCCESS;
        }
    }
    
    pthread_mutex_unlock(&registry_mutex);
    
    char msg[256];
    snprintf(msg, sizeof(msg), 
             "Attempted to remove non-existent lock on '%s' (node-based)", 
             filename);
    log_message("SS", "WARN", msg);
    return ERR_PERMISSION_DENIED;
}

/**
 * get_locked_sentence_list
 * @brief Get the sentence linked list for a locked file
 * @return Pointer to sentence list head if found, NULL otherwise
 */
SentenceNode* get_locked_sentence_list(const char* filename, const char* username, int* count) {
    pthread_mutex_lock(&registry_mutex);
    
    for (int i = 0; i < MAX_LOCKED_FILES; i++) {
        if (locked_files[i].is_active &&
            strcmp(locked_files[i].filename, filename) == 0 &&
            strcmp(locked_files[i].username, username) == 0) {
            
            if (count) {
                *count = locked_files[i].sentence_count;
            }
            pthread_mutex_unlock(&registry_mutex);
            return locked_files[i].sentence_list_head;
        }
    }
    
    pthread_mutex_unlock(&registry_mutex);
    return NULL;
}

/**
 * cleanup_user_locks
 * @brief Remove all locks held by a specific user
 * @return Number of locks removed
 */
int cleanup_user_locks(const char* username) {
    pthread_mutex_lock(&registry_mutex);
    
    int removed = 0;
    for (int i = 0; i < MAX_LOCKED_FILES; i++) {
        if (locked_files[i].is_active &&
            strcmp(locked_files[i].username, username) == 0) {
            
            if (locked_files[i].sentence_list_head) {
                free_sentence_list(locked_files[i].sentence_list_head);
                locked_files[i].sentence_list_head = NULL;
            }
            locked_files[i].locked_node = NULL;
            
            locked_files[i].is_active = 0;
            locked_files[i].sentence_count = 0;
            removed++;
        }
    }
    
    pthread_mutex_unlock(&registry_mutex);
    
    if (removed > 0) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Released %d abandoned locks on disconnect", removed);
        log_message("SS", "INFO", msg);
    }
    
    return removed;
}

/**
 * check_lock_by_content
 * @brief Check if a sentence with given content is locked and verify ownership
 * @return 1 if locked by username, -1 if locked by someone else, 0 if not locked
 */
int check_lock_by_content(const char* filename, const char* sentence_content, const char* username) {
    pthread_mutex_lock(&registry_mutex);
    
    for (int i = 0; i < MAX_LOCKED_FILES; i++) {
        if (locked_files[i].is_active &&
            strcmp(locked_files[i].filename, filename) == 0) {
            
            // Use stored original text (from lock time, not current node text which may be edited)
            const char* node_content = locked_files[i].original_text;
            
            // Compare sentence content (handle empty sentences)
            int content_match = 0;
            if (!sentence_content || strlen(sentence_content) == 0) {
                content_match = (strlen(node_content) == 0);
            } else {
                content_match = (strcmp(node_content, sentence_content) == 0);
            }
            
            if (content_match) {
                int is_owner = strcmp(locked_files[i].username, username) == 0;
                pthread_mutex_unlock(&registry_mutex);
                return is_owner ? 1 : -1;
            }
        }
    }
    
    pthread_mutex_unlock(&registry_mutex);
    return 0; // Not locked
}

/**
 * get_locked_file_by_content
 * @brief Get locked file entry by filename, username, and sentence content
 * @return Pointer to LockedFile if found, NULL otherwise
 * @note This is a legacy function - extracts content from locked_node for compatibility
 */
LockedFile* get_locked_file_by_content(const char* filename, const char* username, const char* sentence_content) {
    pthread_mutex_lock(&registry_mutex);
    
    for (int i = 0; i < MAX_LOCKED_FILES; i++) {
        if (locked_files[i].is_active &&
            strcmp(locked_files[i].filename, filename) == 0 &&
            strcmp(locked_files[i].username, username) == 0) {
            
            // Use stored original text (from lock time, not current node text which may be edited)
            const char* node_content = locked_files[i].original_text;
            
            // Compare sentence content (handle empty sentences)
            int content_match = 0;
            if (!sentence_content || strlen(sentence_content) == 0) {
                content_match = (strlen(node_content) == 0);
            } else {
                content_match = (strcmp(node_content, sentence_content) == 0);
            }
            
            if (content_match) {
                pthread_mutex_unlock(&registry_mutex);
                return &locked_files[i];
            }
        }
    }
    
    pthread_mutex_unlock(&registry_mutex);
    return NULL;
}

/**
 * get_locked_file_by_node
 * @brief Get locked file entry by filename, username, and sentence node pointer
 * @return Pointer to LockedFile if found, NULL otherwise
 */
LockedFile* get_locked_file_by_node(const char* filename, const char* username, SentenceNode* node) {
    pthread_mutex_lock(&registry_mutex);
    
    for (int i = 0; i < MAX_LOCKED_FILES; i++) {
        if (locked_files[i].is_active &&
            strcmp(locked_files[i].filename, filename) == 0 &&
            strcmp(locked_files[i].username, username) == 0 &&
            locked_files[i].locked_node == node) {
            
            pthread_mutex_unlock(&registry_mutex);
            return &locked_files[i];
        }
    }
    
    pthread_mutex_unlock(&registry_mutex);
    return NULL;
}

/**
 * remove_lock_by_content
 * @brief Remove a lock from the registry by content
 * @return ERR_SUCCESS on success, error code otherwise
 */
int remove_lock_by_content(const char* filename, const char* sentence_content) {
    pthread_mutex_lock(&registry_mutex);
    
    for (int i = 0; i < MAX_LOCKED_FILES; i++) {
        if (locked_files[i].is_active &&
            strcmp(locked_files[i].filename, filename) == 0) {
            
            // Use stored original text (from lock time, not current node text which may be edited)
            const char* node_content = locked_files[i].original_text;
            
            // Compare sentence content (handle empty sentences)
            int content_match = 0;
            if (!sentence_content || strlen(sentence_content) == 0) {
                content_match = (strlen(node_content) == 0);
            } else {
                content_match = (strcmp(node_content, sentence_content) == 0);
            }
            
            if (content_match) {
                // Free sentence list if allocated
                if (locked_files[i].sentence_list_head) {
                    free_sentence_list(locked_files[i].sentence_list_head);
                    locked_files[i].sentence_list_head = NULL;
                }
                locked_files[i].locked_node = NULL;
                
                int sentence_idx = locked_files[i].sentence_idx;
                locked_files[i].is_active = 0;
                locked_files[i].sentence_count = 0;
                
                pthread_mutex_unlock(&registry_mutex);
                
                char msg[512];
                snprintf(msg, sizeof(msg), 
                         "Lock released on '%s' sentence %d (content: '%.50s%s')", 
                         filename, sentence_idx,
                         sentence_content ? sentence_content : "(empty)",
                         sentence_content && strlen(sentence_content) > 50 ? "..." : "");
                log_message("SS", "INFO", msg);
                
                return ERR_SUCCESS;
            }
        }
    }
    
    pthread_mutex_unlock(&registry_mutex);
    
    char msg[512];
    snprintf(msg, sizeof(msg), 
             "Attempted to remove non-existent lock on '%s' (content: '%.50s%s')", 
             filename,
             sentence_content ? sentence_content : "(empty)",
             sentence_content && strlen(sentence_content) > 50 ? "..." : "");
    log_message("SS", "WARN", msg);
    return ERR_PERMISSION_DENIED;
}

/**
 * get_file_locks
 * @brief Get all active locks for a specific file
 * @param filename File to query
 * @param lock_info_out Output buffer for lock information string
 * @param bufsize Size of output buffer
 * @return Number of active locks found
 */
int get_file_locks(const char* filename, char* lock_info_out, size_t bufsize) {
    pthread_mutex_lock(&registry_mutex);
    
    int count = 0;
    char temp[4096] = "";
    
    for (int i = 0; i < MAX_LOCKED_FILES; i++) {
        if (locked_files[i].is_active && 
            strcmp(locked_files[i].filename, filename) == 0) {
            
            char lock_entry[256];
            snprintf(lock_entry, sizeof(lock_entry),
                    "  %s├─%s Sentence %s%d%s: locked by %s%s%s\n",
                    ANSI_YELLOW, ANSI_RESET,
                    ANSI_BRIGHT_CYAN, locked_files[i].sentence_idx, ANSI_RESET,
                    ANSI_BRIGHT_YELLOW, locked_files[i].username, ANSI_RESET);
            
            if (strlen(temp) + strlen(lock_entry) < sizeof(temp)) {
                strcat(temp, lock_entry);
                count++;
            }
        }
    }
    
    pthread_mutex_unlock(&registry_mutex);
    
    if (count == 0) {
        snprintf(lock_info_out, bufsize, "  %sNo active locks%s\n",
                 ANSI_BRIGHT_BLACK, ANSI_RESET);
    } else {
        snprintf(lock_info_out, bufsize, "%s", temp);
    }
    
    return count;
}