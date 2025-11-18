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
            if (locked_files[i].sentences) {
                free_sentences(locked_files[i].sentences, locked_files[i].sentence_count);
                locked_files[i].sentences = NULL;
            }
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
 * @brief Add a new locked file to the registry
 * @return ERR_SUCCESS on success, error code otherwise
 */
int add_locked_file(const char* filename, const char* username, int sentence_idx,
                    Sentence* sentences, int sentence_count) {
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
    locked_files[slot].sentences = sentences;
    locked_files[slot].sentence_count = sentence_count;
    locked_files[slot].is_active = 1;
    
    if (slot >= locked_file_count) {
        locked_file_count = slot + 1;
    }
    
    pthread_mutex_unlock(&registry_mutex);
    
    char msg[256];
    snprintf(msg, sizeof(msg), 
             "Lock acquired on '%s' sentence %d (active locks: %d)", 
             filename, sentence_idx, locked_file_count);
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
            
            // Free sentences if allocated
            if (locked_files[i].sentences) {
                free_sentences(locked_files[i].sentences, locked_files[i].sentence_count);
                locked_files[i].sentences = NULL;
            }
            
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
 * get_locked_sentence
 * @brief Get the sentence array for a locked file
 * @return Pointer to sentences if found, NULL otherwise
 */
Sentence* get_locked_sentence(const char* filename, const char* username, int* count) {
    pthread_mutex_lock(&registry_mutex);
    
    for (int i = 0; i < MAX_LOCKED_FILES; i++) {
        if (locked_files[i].is_active &&
            strcmp(locked_files[i].filename, filename) == 0 &&
            strcmp(locked_files[i].username, username) == 0) {
            
            if (count) {
                *count = locked_files[i].sentence_count;
            }
            pthread_mutex_unlock(&registry_mutex);
            return locked_files[i].sentences;
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
            
            if (locked_files[i].sentences) {
                free_sentences(locked_files[i].sentences, locked_files[i].sentence_count);
                locked_files[i].sentences = NULL;
            }
            
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