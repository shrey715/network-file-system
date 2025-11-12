#ifndef CLIENT_H
#define CLIENT_H

#include "common.h"

// ============ CLIENT STATE ============
typedef struct {
    char username[MAX_USERNAME];
    char nm_ip[MAX_IP];
    int nm_port;
    int nm_socket;
    int is_connected;
} ClientState;

// ============ COMMAND FUNCTIONS ============
int execute_view(ClientState* state, int flags);
int execute_read(ClientState* state, const char* filename);
int execute_create(ClientState* state, const char* filename);
int execute_write(ClientState* state, const char* filename, int sentence_idx);
int execute_undo(ClientState* state, const char* filename);
int execute_info(ClientState* state, const char* filename);
int execute_delete(ClientState* state, const char* filename);
int execute_stream(ClientState* state, const char* filename);
int execute_list(ClientState* state);
int execute_addaccess(ClientState* state, const char* filename, const char* username, int read, int write);
int execute_remaccess(ClientState* state, const char* filename, const char* username);
int execute_exec(ClientState* state, const char* filename);
int execute_createfolder(ClientState* state, const char* foldername);
int execute_move(ClientState* state, const char* filename, const char* foldername);
int execute_viewfolder(ClientState* state, const char* foldername);
int execute_checkpoint(ClientState* state, const char* filename, const char* checkpoint_tag);
int execute_viewcheckpoint(ClientState* state, const char* filename, const char* checkpoint_tag);
int execute_revert(ClientState* state, const char* filename, const char* checkpoint_tag);
int execute_listcheckpoints(ClientState* state, const char* filename);
int execute_requestaccess(ClientState* state, const char* filename, int flags);
int execute_viewrequests(ClientState* state, const char* filename);
int execute_approverequest(ClientState* state, const char* filename, const char* username);
int execute_denyrequest(ClientState* state, const char* filename, const char* username);

// ============ PARSER FUNCTIONS ============
int parse_command(const char* input, char* command, char* arg1, char* arg2, int* flags);

#endif // CLIENT_H
