#include "common.h"
#include "storage_server.h"

#include <string.h>
#include <time.h>
#include <stdlib.h>

static LockedFileRegistry lock_registry;
static int registry_initialized = 0;

int init_locked_file_registry(void) {
    if (!registry_initialized) {
        memset(&lock_registry, 0, sizeof(lock_registry));
        pthread_mutex_init(&lock_registry.lock, NULL);
        registry_initialized = 1;
    }
    return ERR_SUCCESS;
}

LockedFileEntry* find_locked_file(const char* filename, const char* username) {
    pthread_mutex_lock(&lock_registry.lock);
    for (int i = 0; i < MAX_LOCKED_FILES; i++) {
        if (lock_registry.entries[i].is_active &&
            strcmp(lock_registry.entries[i].filename, filename) == 0 &&
            strcmp(lock_registry.entries[i].username, username) == 0) {
            pthread_mutex_unlock(&lock_registry.lock);
            return &lock_registry.entries[i];
        }
    }
    pthread_mutex_unlock(&lock_registry.lock);
    return NULL;
}

int add_locked_file(const char* filename, const char* username, int sentence_idx,
                    Sentence* sentences, int count) {
    pthread_mutex_lock(&lock_registry.lock);
    for (int i = 0; i < MAX_LOCKED_FILES; i++) {
        if (!lock_registry.entries[i].is_active) {
            strncpy(lock_registry.entries[i].filename, filename, MAX_FILENAME - 1);
            lock_registry.entries[i].filename[MAX_FILENAME-1] = '\0';
            strncpy(lock_registry.entries[i].username, username, MAX_USERNAME - 1);
            lock_registry.entries[i].username[MAX_USERNAME-1] = '\0';
            lock_registry.entries[i].sentence_index = sentence_idx;
            lock_registry.entries[i].sentences = sentences;
            lock_registry.entries[i].sentence_count = count;
            lock_registry.entries[i].lock_time = time(NULL);
            lock_registry.entries[i].is_active = 1;
            pthread_mutex_unlock(&lock_registry.lock);
            return ERR_SUCCESS;
        }
    }
    pthread_mutex_unlock(&lock_registry.lock);
    return ERR_SS_UNAVAILABLE; // Registry full
}

int remove_locked_file(const char* filename, const char* username) {
    pthread_mutex_lock(&lock_registry.lock);
    for (int i = 0; i < MAX_LOCKED_FILES; i++) {
        if (lock_registry.entries[i].is_active &&
            strcmp(lock_registry.entries[i].filename, filename) == 0 &&
            strcmp(lock_registry.entries[i].username, username) == 0) {

            // Free sentences if present
            if (lock_registry.entries[i].sentences) {
                free_sentences(lock_registry.entries[i].sentences, 
                             lock_registry.entries[i].sentence_count);
            }

            // Clear entry
            memset(&lock_registry.entries[i], 0, sizeof(LockedFileEntry));

            pthread_mutex_unlock(&lock_registry.lock);
            return ERR_SUCCESS;
        }
    }
    pthread_mutex_unlock(&lock_registry.lock);
    return ERR_FILE_NOT_FOUND;
}

void cleanup_locked_file_registry(void) {
    if (registry_initialized) {
        for (int i = 0; i < MAX_LOCKED_FILES; i++) {
            if (lock_registry.entries[i].is_active && lock_registry.entries[i].sentences) {
                free_sentences(lock_registry.entries[i].sentences, 
                             lock_registry.entries[i].sentence_count);
            }
        }
        pthread_mutex_destroy(&lock_registry.lock);
        registry_initialized = 0;
    }
}
