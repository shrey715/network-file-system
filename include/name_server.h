#ifndef NAME_SERVER_H
#define NAME_SERVER_H

#include "common.h"

// ============ DATA STRUCTURES ============

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
    
    pthread_mutex_t lock;
} NameServerState;

// ============ FUNCTION DECLARATIONS ============

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

// Handlers
void* handle_client_connection(void* arg);
void* handle_ss_connection(void* arg);

// Persistence
void save_state(void);
void load_state(void);

#endif // NAME_SERVER_H
