#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <fcntl.h>

// ============ CONSTANTS ============
#define MAX_FILENAME 256
#define MAX_USERNAME 64
#define MAX_IP 16
#define MAX_PATH 512
#define BUFFER_SIZE 4096
#define MAX_STORAGE_SERVERS 10
#define MAX_FILES 1000
#define MAX_CLIENTS 100
#define MAX_SENTENCE_LOCKS 100

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

// ============ MESSAGE STRUCTURE ============
typedef struct {
    int msg_type;
    int op_code;
    char username[MAX_USERNAME];
    char filename[MAX_FILENAME];
    int data_length;
    int error_code;
    int sentence_index;
    int word_index;
    int flags;  // For VIEW command flags
} MessageHeader;

// ============ NETWORK FUNCTIONS ============
int send_message(int sockfd, MessageHeader* header, const char* payload);
int recv_message(int sockfd, MessageHeader* header, char** payload);
int create_server_socket(int port);
int connect_to_server(const char* ip, int port);

// ============ LOGGING FUNCTIONS ============
void log_message(const char* component, const char* level, const char* message);
void log_operation(const char* component, const char* username, const char* operation, const char* filename, int status);

// ============ UTILITY FUNCTIONS ============
char* read_file_content(const char* filepath);
int write_file_content(const char* filepath, const char* content);
int file_exists(const char* filepath);
long get_file_size(const char* filepath);
void create_directory(const char* path);
char* get_error_message(int error_code);

#endif // COMMON_H
