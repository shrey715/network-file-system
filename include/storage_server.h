#ifndef STORAGE_SERVER_H
#define STORAGE_SERVER_H

#include "common.h"

// ============ DATA STRUCTURES ============

// Sentence node structure with locking (linked list)
typedef struct SentenceNode {
  char *text;
  char *trailing_ws; // whitespace (spaces/newlines) that followed the sentence
                     // delimiter
  pthread_mutex_t lock;
  char locked_by[MAX_USERNAME];
  int is_locked;
  struct SentenceNode *next; // Pointer to next sentence in linked list
} SentenceNode;

// Alias for backward compatibility
typedef SentenceNode Sentence;

// File with sentences
typedef struct {
  char filename[MAX_FILENAME];
  Sentence *sentences;
  int sentence_count;
  char *undo_content;
  pthread_rwlock_t file_lock;
} FileWithSentences;

// Storage Server configuration
typedef struct {
  int server_id;
  char nm_ip[MAX_IP];
  int nm_port;
  int client_port;
  char storage_dir[MAX_STORAGE_DIR];
  char replica_ip[MAX_IP];
  int replica_port;
} SSConfig;

extern SSConfig config;

// Active file sessions
typedef struct {
  char filename[MAX_FILENAME];
  char username[MAX_USERNAME];
  int sentence_index;
  char *temp_buffer;
  time_t start_time;
} WriteSession;

// ======= LOCK REGISTRY =======
#define MAX_LOCKED_FILES 100

typedef struct {
  char filename[MAX_FILENAME];
  char username[MAX_USERNAME];
  int sentence_idx;          // Original index when lock was acquired (for
                             // logging/debugging)
  SentenceNode *locked_node; // Pointer to the locked sentence node in the
                             // file's linked list
  SentenceNode *sentence_list_head; // Head of the sentence linked list snapshot
                                    // (for editing)
  int sentence_count;               // Total sentences in the snapshot
  char original_text[MAX_SENTENCE_CONTENT]; // Original text at lock time (for
                                            // matching on unlock)
  int is_active;
  int undo_saved; // Flag: 1 if undo snapshot was saved before first edit
} LockedFile;

// ============ FUNCTION DECLARATIONS ============

// Lock registry API
void init_locked_file_registry(void);
void cleanup_locked_file_registry(void);
LockedFile *find_locked_file(const char *filename, const char *username);
int add_locked_file(const char *filename, const char *username,
                    int sentence_idx, SentenceNode *locked_node,
                    SentenceNode *sentence_list_head, int count,
                    const char *original_text);
int check_lock(const char *filename, int sentence_idx, const char *username);
int check_lock_by_node(const char *filename, SentenceNode *node,
                       const char *username);
int remove_lock(const char *filename, int sentence_idx);
int remove_lock_by_node(const char *filename, SentenceNode *node);
SentenceNode *get_locked_sentence_list(const char *filename,
                                       const char *username, int *count);
LockedFile *get_locked_file_by_node(const char *filename, const char *username,
                                    SentenceNode *node);
int cleanup_user_locks(const char *username);
int get_file_locks(const char *filename, char *lock_info_out, size_t bufsize);

// File operations
int ss_create_file(const char *filename, const char *owner);
int ss_delete_file(const char *filename);
int ss_read_file(const char *filename, char **content);
int ss_get_file_info(const char *filename, long *size, int *words, int *chars);
int ss_move_file(const char *old_filename, const char *new_filename);

// Statistics tracking
void increment_edit_stats(const char *filename, const char *username);
int get_file_stats(const char *filename, char *stats_out, size_t bufsize);

// Sentence operations
SentenceNode *parse_sentences_to_list(const char *text, int *count);
int parse_sentences(
    const char *text,
    Sentence **sentences); // Legacy - creates array for compatibility
int lock_sentence(FileWithSentences *file, int sentence_idx,
                  const char *username);
int unlock_sentence(FileWithSentences *file, int sentence_idx,
                    const char *username);
void free_sentence_list(SentenceNode *head);         // Free linked list
void free_sentences(Sentence *sentences, int count); // Legacy - for array-based

// Write operations
int ss_write_lock(const char *filename, int sentence_idx, const char *username);
int ss_write_word(const char *filename, int sentence_idx, int word_idx,
                  const char *new_word, const char *username);
int ss_write_unlock(const char *filename, int sentence_idx,
                    const char *username);

// Undo operations
int ss_save_undo(const char *filename);
int ss_undo_file(const char *filename);

// Checkpoint operations
int ss_create_checkpoint(const char *filename, const char *checkpoint_tag);
int ss_view_checkpoint(const char *filename, const char *checkpoint_tag,
                       char **content);
int ss_revert_checkpoint(const char *filename, const char *checkpoint_tag);
int ss_list_checkpoints(const char *filename, char **checkpoint_list);

// Stream operations
int ss_stream_file(int client_socket, const char *filename);

// Handlers
void *handle_nm_communication(void *arg);
void *handle_client_request(void *arg);

// Request handler helpers (internal)
void send_simple_response(int client_fd, int msg_type, int error_code);
void send_content_response(int client_fd, int result, const char *content);
int ss_forward_to_replica(MessageHeader *header, const char *payload,
                          const char *op_name);
int handle_ss_create(int client_fd, MessageHeader *header, const char *payload);
int handle_ss_delete(int client_fd, MessageHeader *header);
int handle_ss_read(int client_fd, MessageHeader *header);
void handle_ss_write_lock(int client_fd, MessageHeader *header);
void handle_ss_write_word(int client_fd, MessageHeader *header,
                          const char *payload);
void handle_ss_write_unlock(int client_fd, MessageHeader *header);
void handle_ss_info(int client_fd, MessageHeader *header);
void handle_ss_undo(int client_fd, MessageHeader *header);
void handle_ss_move(int client_fd, MessageHeader *header, const char *payload);
void handle_ss_checkpoint(int client_fd, MessageHeader *header);
void handle_ss_viewcheckpoint(int client_fd, MessageHeader *header);
void handle_ss_revert(int client_fd, MessageHeader *header);
void handle_ss_listcheckpoints(int client_fd, MessageHeader *header);

// Persistence
void load_files(void);
void save_file_metadata(const char *filename, const char *owner);

// Safe path construction
int ss_build_filepath(char *dest, size_t dest_size, const char *filename,
                      const char *extension);

// Sync / Recovery
void ss_start_recovery_sync(const char *replica_ip, int replica_port);
void handle_ss_sync(int client_fd, MessageHeader *header, const char *payload);

// Live Updates
time_t ss_get_file_mtime(const char *filename);

#endif // STORAGE_SERVER_H