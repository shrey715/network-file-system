#ifndef COMMON_H
#define COMMON_H

#include "table.h"
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

// ============ ANSI COLOR CODES ============
#define ANSI_RESET "\033[0m"
#define ANSI_BOLD "\033[1m"

// Foreground colors
#define ANSI_BLACK "\033[30m"
#define ANSI_RED "\033[31m"
#define ANSI_GREEN "\033[32m"
#define ANSI_YELLOW "\033[33m"
#define ANSI_BLUE "\033[34m"
#define ANSI_MAGENTA "\033[35m"
#define ANSI_CYAN "\033[36m"
#define ANSI_WHITE "\033[37m"

// Bright foreground colors
#define ANSI_BRIGHT_BLACK "\033[90m"
#define ANSI_BRIGHT_RED "\033[91m"
#define ANSI_BRIGHT_GREEN "\033[92m"
#define ANSI_BRIGHT_YELLOW "\033[93m"
#define ANSI_BRIGHT_BLUE "\033[94m"
#define ANSI_BRIGHT_MAGENTA "\033[95m"
#define ANSI_BRIGHT_CYAN "\033[96m"
#define ANSI_BRIGHT_WHITE "\033[97m"

// Global toggle to enable/disable colors at runtime. Define in one C file.
extern int enable_colors;

// Convenience print macros that respect enable_colors.
#define PRINT_ERR(fmt, ...)                                                    \
  do {                                                                         \
    if (enable_colors)                                                         \
      printf(ANSI_BRIGHT_RED "Error: " fmt ANSI_RESET "\n", ##__VA_ARGS__);    \
    else                                                                       \
      printf("Error: " fmt "\n", ##__VA_ARGS__);                               \
  } while (0)

#define PRINT_OK(fmt, ...)                                                     \
  do {                                                                         \
    if (enable_colors)                                                         \
      printf(ANSI_GREEN fmt ANSI_RESET "\n", ##__VA_ARGS__);                   \
    else                                                                       \
      printf(fmt "\n", ##__VA_ARGS__);                                         \
  } while (0)

#define PRINT_WARN(fmt, ...)                                                   \
  do {                                                                         \
    if (enable_colors)                                                         \
      printf(ANSI_YELLOW fmt ANSI_RESET "\n", ##__VA_ARGS__);                  \
    else                                                                       \
      printf(fmt "\n", ##__VA_ARGS__);                                         \
  } while (0)

#define PRINT_INFO(fmt, ...)                                                   \
  do {                                                                         \
    if (enable_colors)                                                         \
      printf(ANSI_CYAN fmt ANSI_RESET "\n", ##__VA_ARGS__);                    \
    else                                                                       \
      printf(fmt "\n", ##__VA_ARGS__);                                         \
  } while (0)

#define PRINT_PROMPT()                                                         \
  do {                                                                         \
    if (enable_colors)                                                         \
      printf(ANSI_BRIGHT_BLUE "> " ANSI_RESET);                                \
    else                                                                       \
      printf("> ");                                                            \
    fflush(stdout);                                                            \
  } while (0)

// ============ CONSTANTS ============
#define MAX_FILENAME 256
#define MAX_FOLDERNAME 256
#define MAX_USERNAME 64
#define MAX_CHECKPOINT_TAG 64
#define MAX_IP 16
#define MAX_STORAGE_DIR 512 // Maximum length for storage directory path
#define MAX_PATH 1024       // Maximum length for full file paths
#define MAX_FULL_PATH                                                          \
  1536 // MAX_PATH + MAX_FILENAME + extra (for folder_path/filename)
#define BUFFER_SIZE 4096
#define MAX_STORAGE_SERVERS 10
#define MAX_FILES 1000
#define MAX_FOLDERS 500
#define MAX_CLIENTS 100
#define MAX_SENTENCE_LOCKS 100
#define MAX_SENTENCE_CONTENT                                                   \
  2048                         // Maximum length for sentence content snapshot
#define LRU_CACHE_SIZE 100     // Size of LRU cache for file lookups
#define TRIE_ALPHABET_SIZE 256 // ASCII character set for Trie
#define HEARTBEAT_TIMEOUT                                                      \
  30 // Seconds before marking storage server as inactive
#define HEARTBEAT_CHECK_INTERVAL 10 // Seconds between heartbeat checks

// ============ MESSAGE TYPES ============
#define MSG_REQUEST 1
#define MSG_RESPONSE 2
#define MSG_ERROR 3
#define MSG_ACK 4
#define MSG_STOP 5

// ============ OPERATION CODES ============
// Client operations
#define OP_VIEW 10
#define OP_READ 11
#define OP_CREATE 12
#define OP_WRITE 13
#define OP_ETIRW 14
#define OP_UNDO 15
#define OP_INFO 16
#define OP_DELETE 17
#define OP_STREAM 18
#define OP_LIST 19
#define OP_ADDACCESS 20
#define OP_REMACCESS 21
#define OP_EXEC 22
#define OP_CREATEFOLDER 23
#define OP_MOVE 24
#define OP_VIEWFOLDER 25
#define OP_CHECKPOINT 26
#define OP_VIEWCHECKPOINT 27
#define OP_REVERT 28
#define OP_LISTCHECKPOINTS 29
#define OP_REQUESTACCESS 35
#define OP_VIEWREQUESTS 36
#define OP_APPROVEREQUEST 37
#define OP_DENYREQUEST 38

// System operations
#define OP_REGISTER_SS 30
#define OP_CONNECT_CLIENT 31
#define OP_DISCONNECT 32
#define OP_HEARTBEAT 33

// Storage server operations
#define OP_SS_CREATE 40
#define OP_SS_DELETE 41
#define OP_SS_READ 42
#define OP_SS_WRITE_LOCK 43
#define OP_SS_WRITE_WORD 44
#define OP_SS_WRITE_UNLOCK 45
#define OP_SS_STREAM 46
#define OP_SS_MOVE 47
#define OP_SS_CHECKPOINT 48
#define OP_SS_VIEWCHECKPOINT 49
#define OP_SS_REVERT 50
#define OP_SS_LISTCHECKPOINTS 51

// ============ ERROR CODES ============
#define ERR_SUCCESS 0
#define ERR_FILE_NOT_FOUND 101
#define ERR_PERMISSION_DENIED 102
#define ERR_FILE_EXISTS 103
#define ERR_SENTENCE_LOCKED 104
#define ERR_INVALID_INDEX 105
#define ERR_NOT_OWNER 106
#define ERR_USER_NOT_FOUND 107
#define ERR_SS_UNAVAILABLE 108
#define ERR_SS_DISCONNECTED 109
#define ERR_INVALID_COMMAND 110
#define ERR_NETWORK_ERROR 111
#define ERR_FILE_OPERATION_FAILED 112
#define ERR_UNDO_NOT_AVAILABLE 113
#define ERR_INVALID_SENTENCE 114
#define ERR_INVALID_WORD 115
#define ERR_FILE_EMPTY 116
#define ERR_FOLDER_NOT_FOUND 117
#define ERR_FOLDER_EXISTS 118
#define ERR_INVALID_PATH 119
#define ERR_CHECKPOINT_NOT_FOUND 120
#define ERR_CHECKPOINT_EXISTS 121
#define ERR_REQUEST_EXISTS 122
#define ERR_REQUEST_NOT_FOUND 123
#define ERR_ALREADY_HAS_ACCESS 124
#define ERR_INVALID_FILENAME 125
#define ERR_USERNAME_TAKEN 126
#define ERR_SS_EXISTS 127 // Storage Server ID already in use

// ============ MESSAGE STRUCTURE ============
typedef struct {
  int msg_type;
  int op_code;
  char username[MAX_USERNAME];
  char filename[MAX_FILENAME];
  char foldername[MAX_FOLDERNAME];         // For folder operations
  char checkpoint_tag[MAX_CHECKPOINT_TAG]; // For checkpoint operations
  int data_length;
  int error_code;
  int sentence_index;
  int word_index;
  int flags; // For VIEW command flags
} MessageHeader;

// ============ NETWORK FUNCTIONS ============
int send_message(int sockfd, MessageHeader *header, const char *payload);
int recv_message(int sockfd, MessageHeader *header, char **payload);
int create_server_socket(int port);
int connect_to_server(const char *ip, int port);

// ============ MESSAGE HELPERS ============
void init_message_header(MessageHeader *header, int msg_type, int op_code,
                         const char *username);
int parse_ss_info(const char *ss_info, char *ip_out, int *port_out);
void safe_close_socket(int *sockfd);

// ============ LOGGING FUNCTIONS ============
void log_message(const char *component, const char *level, const char *message);
void log_operation(const char *component, const char *level,
                   const char *operation, const char *username, const char *ip,
                   int port, const char *details, int error_code);

// ============ UTILITY FUNCTIONS ============
int visual_strlen(
    const char *str); // Calculate visual width excluding ANSI codes
char *read_file_content(const char *filepath);
int write_file_content(const char *filepath, const char *content);
int file_exists(const char *filepath);
long get_file_size(const char *filepath);
int is_valid_filename(
    const char *filename); // Check if filename doesn't use reserved extensions
void create_directory(const char *path);
char *get_error_message(int error_code);

// ============ NETWORK UTILITY FUNCTIONS ============
int get_local_network_ip(char *ip_out, size_t size);

#endif // COMMON_H
