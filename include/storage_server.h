#ifndef STORAGE_SERVER_H
#define STORAGE_SERVER_H

#include "common.h"

// ============ DATA STRUCTURES ============

// Sentence structure with locking
typedef struct {
    char* text;
    char* trailing_ws; // whitespace (spaces/newlines) that followed the sentence delimiter
    pthread_mutex_t lock;
    char locked_by[MAX_USERNAME];
    int is_locked;
} Sentence;

// File with sentences
typedef struct {
    char filename[MAX_FILENAME];
    Sentence* sentences;
    int sentence_count;
    char* undo_content;
    pthread_rwlock_t file_lock;
} FileWithSentences;

// Storage Server configuration
typedef struct {
    int server_id;
    char nm_ip[MAX_IP];
    int nm_port;
    int client_port;
    char storage_dir[MAX_STORAGE_DIR];
} SSConfig;

// Active file sessions
typedef struct {
    char filename[MAX_FILENAME];
    char username[MAX_USERNAME];
    int sentence_index;
    char* temp_buffer;
    time_t start_time;
} WriteSession;

// ======= LOCK REGISTRY =======
#define MAX_LOCKED_FILES 100

typedef struct {
    char filename[MAX_FILENAME];
    char username[MAX_USERNAME];
    int sentence_idx;
    Sentence* sentences;
    int sentence_count;
    int is_active;
} LockedFile;

// ============ FUNCTION DECLARATIONS ============

// Lock registry API
void init_locked_file_registry(void);
void cleanup_locked_file_registry(void);
LockedFile* find_locked_file(const char* filename, const char* username);
int add_locked_file(const char* filename, const char* username, int sentence_idx, 
                    Sentence* sentences, int count);
int check_lock(const char* filename, int sentence_idx, const char* username);
int remove_lock(const char* filename, int sentence_idx);
Sentence* get_locked_sentence(const char* filename, const char* username, int* count);
int cleanup_user_locks(const char* username);

// File operations
int ss_create_file(const char* filename, const char* owner);
int ss_delete_file(const char* filename);
int ss_read_file(const char* filename, char** content);
int ss_get_file_info(const char* filename, long* size, int* words, int* chars);

// Sentence operations
int parse_sentences(const char* text, Sentence** sentences);
int lock_sentence(FileWithSentences* file, int sentence_idx, const char* username);
int unlock_sentence(FileWithSentences* file, int sentence_idx, const char* username);
void free_sentences(Sentence* sentences, int count);

// Write operations
int ss_write_lock(const char* filename, int sentence_idx, const char* username);
int ss_write_word(const char* filename, int sentence_idx, int word_idx, const char* new_word, const char* username);
int ss_write_unlock(const char* filename, int sentence_idx, const char* username);

// Undo operations
int ss_save_undo(const char* filename);
int ss_undo_file(const char* filename);

// Stream operations
int ss_stream_file(int client_socket, const char* filename);

// Handlers
void* handle_nm_communication(void* arg);
void* handle_client_request(void* arg);

// Persistence
void load_files(void);
void save_file_metadata(const char* filename, const char* owner);

// Safe path construction
int ss_build_filepath(char* dest, size_t dest_size, const char* filename, const char* extension);

#endif // STORAGE_SERVER_H