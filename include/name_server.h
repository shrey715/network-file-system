#ifndef NAME_SERVER_H
#define NAME_SERVER_H

#include "common.h"

// ============ DATA STRUCTURES ============

// Trie node for efficient file search
typedef struct TrieNode {
    struct TrieNode* children[256];  // ASCII character set
    int file_index;                   // Index in files array (-1 if not a file)
    int is_end_of_path;              // Flag to mark end of file path
} TrieNode;

// LRU Cache node for recent file lookups
typedef struct CacheNode {
    char key[MAX_PATH];              // Full file path
    int file_index;                  // Index in files array
    struct CacheNode* prev;
    struct CacheNode* next;
    time_t last_access;              // Timestamp of last access
} CacheNode;

// LRU Cache structure
typedef struct {
    CacheNode* head;                 // Most recently used
    CacheNode* tail;                 // Least recently used
    CacheNode* nodes[LRU_CACHE_SIZE];
    int size;
    int capacity;
    pthread_mutex_t lock;
    // Statistics
    long hits;
    long misses;
} LRUCache;

// Access Control Entry
typedef struct {
    char username[MAX_USERNAME];
    int read_permission;
    int write_permission;
} AccessControlEntry;

// Folder metadata
typedef struct {
    char foldername[MAX_FOLDERNAME];  // Full path (e.g., "folder1/folder2")
    char owner[MAX_USERNAME];
    time_t created_time;
    int parent_folder_idx;  // Index of parent folder, -1 for root
    AccessControlEntry* acl;
    int acl_count;
} FolderMetadata;

// File metadata
typedef struct {
    char filename[MAX_FILENAME];
    char folder_path[MAX_PATH];  // Path to folder containing file (empty for root)
    char owner[MAX_USERNAME];
    int ss_id;
    time_t created_time;
    time_t last_modified;
    time_t last_accessed;
    long file_size;
    int word_count;
    int char_count;
    AccessControlEntry* acl;
    int acl_count;
} FileMetadata;

// Storage Server information
typedef struct {
    int server_id;
    char ip[MAX_IP];
    int nm_port;
    int client_port;
    int is_active;
    time_t last_heartbeat;
    char** files;
    int file_count;
} StorageServerInfo;

// Client information
typedef struct {
    char username[MAX_USERNAME];
    char ip[MAX_IP];
    int port;
    int is_connected;
    time_t last_activity;
} ClientInfo;

// Access Request
typedef struct {
    char filename[MAX_FILENAME];
    char requester[MAX_USERNAME];
    time_t request_time;
    int read_requested;   // 1 if read access requested
    int write_requested;  // 1 if write access requested
} AccessRequest;

// Name Server state
typedef struct {
    StorageServerInfo storage_servers[MAX_STORAGE_SERVERS];
    int ss_count;
    
    FileMetadata files[MAX_FILES];
    int file_count;
    
    FolderMetadata folders[MAX_FOLDERS];
    int folder_count;
    
    ClientInfo clients[MAX_CLIENTS];
    int client_count;
    
    AccessRequest access_requests[MAX_FILES];  // Max one request per file per user (simplified)
    int request_count;
    
    // Efficient search structures
    TrieNode* file_trie_root;        // Trie for O(m) file lookups
    LRUCache* file_cache;             // LRU cache for frequent lookups
    
    pthread_mutex_t lock;
} NameServerState;

// ============ FUNCTION DECLARATIONS ============

// Trie operations
TrieNode* trie_create_node(void);
void trie_insert(TrieNode* root, const char* path, int file_index);
int trie_search(TrieNode* root, const char* path);
void trie_delete(TrieNode* root, const char* path);
void trie_free(TrieNode* root);

// LRU Cache operations
LRUCache* cache_create(int capacity);
int cache_get(LRUCache* cache, const char* key);
void cache_put(LRUCache* cache, const char* key, int file_index);
void cache_invalidate(LRUCache* cache, const char* key);
void cache_free(LRUCache* cache);
void cache_print_stats(LRUCache* cache);

// File registry operations
int nm_register_file(const char* filename, const char* folder_path, const char* owner, int ss_id);
FileMetadata* nm_find_file(const char* filename);
FileMetadata* nm_find_file_in_folder(const char* filename, const char* folder_path);
int nm_delete_file(const char* filename);
int nm_check_permission(const char* filename, const char* username, int need_write);
int nm_move_file(const char* filename, const char* new_folder_path);

// Folder registry operations
int nm_create_folder(const char* foldername, const char* owner);
FolderMetadata* nm_find_folder(const char* foldername);
int nm_check_folder_permission(const char* foldername, const char* username, int need_write);
int nm_list_folder_contents(const char* foldername, const char* username, char* buffer, size_t buffer_size);

// Storage server operations
int nm_register_storage_server(int server_id, const char* ip, int nm_port, int client_port);
StorageServerInfo* nm_find_storage_server(int ss_id);
int nm_select_storage_server(void);

// Access control
int nm_add_access(const char* filename, const char* username, int read, int write);
int nm_remove_access(const char* filename, const char* username);
int nm_add_folder_access(const char* foldername, const char* username, int read, int write);

// Access requests
int nm_request_access(const char* filename, const char* requester, int read_requested, int write_requested);
int nm_view_requests(const char* filename, const char* owner, char* buffer, size_t buffer_size);
int nm_approve_request(const char* filename, const char* owner, const char* requester);
int nm_deny_request(const char* filename, const char* owner, const char* requester);

// Handlers
void* handle_client_connection(void* arg);
void* handle_ss_connection(void* arg);

// Monitoring
void nm_print_search_stats(void);

// Persistence
void save_state(void);
void load_state(void);

#endif // NAME_SERVER_H
