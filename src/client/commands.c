#include "common.h"
#include "client.h"

// Execute VIEW command
int execute_view(ClientState* state, int flags) {
    MessageHeader header;
    memset(&header, 0, sizeof(header));
    header.msg_type = MSG_REQUEST;
    header.op_code = OP_VIEW;
    strcpy(header.username, state->username);
    header.flags = flags;
    header.data_length = 0;
    
    send_message(state->nm_socket, &header, NULL);
    
    char* response;
    recv_message(state->nm_socket, &header, &response);
    
    if (header.msg_type == MSG_RESPONSE) {
        if (flags & 2) {  // -l flag
            printf("%-20s %5s %5s %16s %s\n", "Filename", "Words", "Chars", "Last Access", "Owner");
            printf("------------------------------------------------------------\n");
        }
        printf("%s", response);
    } else {
        printf("Error: %s\n", get_error_message(header.error_code));
    }
    
    if (response) free(response);
    return header.error_code;
}

// Execute READ command
int execute_read(ClientState* state, const char* filename) {
    // Request SS info from NM
    MessageHeader header;
    memset(&header, 0, sizeof(header));
    header.msg_type = MSG_REQUEST;
    header.op_code = OP_READ;
    strcpy(header.username, state->username);
    strcpy(header.filename, filename);
    header.data_length = 0;
    
    send_message(state->nm_socket, &header, NULL);
    
    char* ss_info;
    recv_message(state->nm_socket, &header, &ss_info);
    
    if (header.msg_type != MSG_RESPONSE) {
        printf("Error: %s\n", get_error_message(header.error_code));
        if (ss_info) free(ss_info);
        return header.error_code;
    }
    
    // Parse SS IP and port
    char ss_ip[MAX_IP];
    int ss_port;
    sscanf(ss_info, "%[^:]:%d", ss_ip, &ss_port);
    free(ss_info);
    
    // Connect to SS
    int ss_socket = connect_to_server(ss_ip, ss_port);
    if (ss_socket < 0) {
        printf("Error: Failed to connect to storage server\n");
        return ERR_SS_UNAVAILABLE;
    }
    
    // Request file content from SS
    memset(&header, 0, sizeof(header));
    header.msg_type = MSG_REQUEST;
    header.op_code = OP_SS_READ;
    strcpy(header.filename, filename);
    strcpy(header.username, state->username);
    header.data_length = 0;
    
    send_message(ss_socket, &header, NULL);
    
    char* content;
    recv_message(ss_socket, &header, &content);
    close(ss_socket);
    
    if (header.msg_type == MSG_RESPONSE) {
        printf("%s\n", content);
    } else {
        printf("Error: %s\n", get_error_message(header.error_code));
    }
    
    if (content) free(content);
    return header.error_code;
}

// Execute CREATE command
int execute_create(ClientState* state, const char* filename) {
    MessageHeader header;
    memset(&header, 0, sizeof(header));
    header.msg_type = MSG_REQUEST;
    header.op_code = OP_CREATE;
    strcpy(header.username, state->username);
    strcpy(header.filename, filename);
    header.data_length = strlen(state->username);
    
    send_message(state->nm_socket, &header, state->username);
    
    char* response;
    recv_message(state->nm_socket, &header, &response);
    
    if (header.msg_type == MSG_ACK) {
        printf("File '%s' created successfully!\n", filename);
    } else {
        printf("Error: %s\n", get_error_message(header.error_code));
    }
    
    if (response) free(response);
    return header.error_code;
}

// Execute WRITE command
int execute_write(ClientState* state, const char* filename, int sentence_idx) {
    // Request SS info from NM
    MessageHeader header;
    memset(&header, 0, sizeof(header));
    header.msg_type = MSG_REQUEST;
    header.op_code = OP_WRITE;
    strcpy(header.username, state->username);
    strcpy(header.filename, filename);
    header.data_length = 0;
    
    send_message(state->nm_socket, &header, NULL);
    
    char* ss_info;
    recv_message(state->nm_socket, &header, &ss_info);
    
    if (header.msg_type != MSG_RESPONSE) {
        printf("Error: %s\n", get_error_message(header.error_code));
        if (ss_info) free(ss_info);
        return header.error_code;
    }
    
    // Parse SS IP and port
    char ss_ip[MAX_IP];
    int ss_port;
    sscanf(ss_info, "%[^:]:%d", ss_ip, &ss_port);
    free(ss_info);
    
    // Connect to SS
    int ss_socket = connect_to_server(ss_ip, ss_port);
    if (ss_socket < 0) {
        printf("Error: Failed to connect to storage server\n");
        return ERR_SS_UNAVAILABLE;
    }
    
    // Lock sentence
    memset(&header, 0, sizeof(header));
    header.msg_type = MSG_REQUEST;
    header.op_code = OP_SS_WRITE_LOCK;
    strcpy(header.filename, filename);
    strcpy(header.username, state->username);
    header.sentence_index = sentence_idx;
    header.data_length = 0;
    
    send_message(ss_socket, &header, NULL);
    
    char* response;
    recv_message(ss_socket, &header, &response);
    if (response) free(response);
    
    if (header.msg_type != MSG_ACK) {
        printf("Error: %s\n", get_error_message(header.error_code));
        close(ss_socket);
        return header.error_code;
    }
    
    printf("Sentence locked. Enter word modifications (word_index content), then ETIRW:\n");
    
    // Read write commands
    char input[BUFFER_SIZE];
    while (1) {
        printf("  ");
        fflush(stdout);
        
        if (fgets(input, sizeof(input), stdin) == NULL) {
            break;
        }
        
        input[strcspn(input, "\n")] = 0;
        
        if (strcmp(input, "ETIRW") == 0) {
            // Unlock sentence
            memset(&header, 0, sizeof(header));
            header.msg_type = MSG_REQUEST;
            header.op_code = OP_SS_WRITE_UNLOCK;
            strcpy(header.filename, filename);
            strcpy(header.username, state->username);
            header.sentence_index = sentence_idx;
            header.data_length = 0;
            
            send_message(ss_socket, &header, NULL);
            
            recv_message(ss_socket, &header, &response);
            if (response) free(response);
            
            if (header.msg_type == MSG_ACK) {
                printf("Write successful!\n");
            } else {
                printf("Error: %s\n", get_error_message(header.error_code));
            }
            break;
        }
        
        // Parse word modification: "word_index content"
        int word_idx;
        char new_word[256];
        if (sscanf(input, "%d %s", &word_idx, new_word) != 2) {
            printf("Invalid format. Use: <word_index> <content>\n");
            continue;
        }
        
        // Send write word command
        memset(&header, 0, sizeof(header));
        header.msg_type = MSG_REQUEST;
        header.op_code = OP_SS_WRITE_WORD;
        strcpy(header.filename, filename);
        strcpy(header.username, state->username);
        header.sentence_index = sentence_idx;
        
        char payload[BUFFER_SIZE];
        snprintf(payload, sizeof(payload), "%d %s", word_idx, new_word);
        header.data_length = strlen(payload);
        
        send_message(ss_socket, &header, payload);
        
        recv_message(ss_socket, &header, &response);
        if (response) free(response);
        
        if (header.msg_type != MSG_ACK) {
            printf("Error: %s\n", get_error_message(header.error_code));
        }
    }
    
    close(ss_socket);
    return ERR_SUCCESS;
}

// Execute UNDO command
int execute_undo(ClientState* state, const char* filename) {
    // Request SS info from NM
    MessageHeader header;
    memset(&header, 0, sizeof(header));
    header.msg_type = MSG_REQUEST;
    header.op_code = OP_UNDO;
    strcpy(header.username, state->username);
    strcpy(header.filename, filename);
    header.data_length = 0;
    
    send_message(state->nm_socket, &header, NULL);
    
    char* ss_info;
    recv_message(state->nm_socket, &header, &ss_info);
    
    if (header.msg_type != MSG_RESPONSE) {
        printf("Error: %s\n", get_error_message(header.error_code));
        if (ss_info) free(ss_info);
        return header.error_code;
    }
    
    // Parse SS IP and port
    char ss_ip[MAX_IP];
    int ss_port;
    sscanf(ss_info, "%[^:]:%d", ss_ip, &ss_port);
    free(ss_info);
    
    // Connect to SS
    int ss_socket = connect_to_server(ss_ip, ss_port);
    if (ss_socket < 0) {
        printf("Error: Failed to connect to storage server\n");
        return ERR_SS_UNAVAILABLE;
    }
    
    // Request undo
    memset(&header, 0, sizeof(header));
    header.msg_type = MSG_REQUEST;
    header.op_code = OP_UNDO;
    strcpy(header.filename, filename);
    strcpy(header.username, state->username);
    header.data_length = 0;
    
    send_message(ss_socket, &header, NULL);
    
    char* response;
    recv_message(ss_socket, &header, &response);
    close(ss_socket);
    
    if (header.msg_type == MSG_ACK) {
        printf("Undo successful!\n");
    } else {
        printf("Error: %s\n", get_error_message(header.error_code));
    }
    
    if (response) free(response);
    return header.error_code;
}

// Execute INFO command
int execute_info(ClientState* state, const char* filename) {
    MessageHeader header;
    memset(&header, 0, sizeof(header));
    header.msg_type = MSG_REQUEST;
    header.op_code = OP_INFO;
    strcpy(header.username, state->username);
    strcpy(header.filename, filename);
    header.data_length = 0;
    
    send_message(state->nm_socket, &header, NULL);
    
    char* response;
    recv_message(state->nm_socket, &header, &response);
    
    if (header.msg_type == MSG_RESPONSE) {
        printf("%s", response);
    } else {
        printf("Error: %s\n", get_error_message(header.error_code));
    }
    
    if (response) free(response);
    return header.error_code;
}

// Execute DELETE command
int execute_delete(ClientState* state, const char* filename) {
    MessageHeader header;
    memset(&header, 0, sizeof(header));
    header.msg_type = MSG_REQUEST;
    header.op_code = OP_DELETE;
    strcpy(header.username, state->username);
    strcpy(header.filename, filename);
    header.data_length = 0;
    
    send_message(state->nm_socket, &header, NULL);
    
    char* response;
    recv_message(state->nm_socket, &header, &response);
    
    if (header.msg_type == MSG_ACK) {
        printf("File '%s' deleted successfully!\n", filename);
    } else {
        printf("Error: %s\n", get_error_message(header.error_code));
    }
    
    if (response) free(response);
    return header.error_code;
}

// Execute STREAM command
int execute_stream(ClientState* state, const char* filename) {
    // Request SS info from NM
    MessageHeader header;
    memset(&header, 0, sizeof(header));
    header.msg_type = MSG_REQUEST;
    header.op_code = OP_STREAM;
    strcpy(header.username, state->username);
    strcpy(header.filename, filename);
    header.data_length = 0;
    
    send_message(state->nm_socket, &header, NULL);
    
    char* ss_info;
    recv_message(state->nm_socket, &header, &ss_info);
    
    if (header.msg_type != MSG_RESPONSE) {
        printf("Error: %s\n", get_error_message(header.error_code));
        if (ss_info) free(ss_info);
        return header.error_code;
    }
    
    // Parse SS IP and port
    char ss_ip[MAX_IP];
    int ss_port;
    sscanf(ss_info, "%[^:]:%d", ss_ip, &ss_port);
    free(ss_info);
    
    // Connect to SS
    int ss_socket = connect_to_server(ss_ip, ss_port);
    if (ss_socket < 0) {
        printf("Error: Failed to connect to storage server\n");
        return ERR_SS_UNAVAILABLE;
    }
    
    // Request stream
    memset(&header, 0, sizeof(header));
    header.msg_type = MSG_REQUEST;
    header.op_code = OP_STREAM;
    strcpy(header.filename, filename);
    strcpy(header.username, state->username);
    header.data_length = 0;
    
    send_message(ss_socket, &header, NULL);
    
    // Receive and print words
    while (1) {
        char* word;
        recv_message(ss_socket, &header, &word);
        
        if (header.msg_type == MSG_STOP) {
            if (word) free(word);
            break;
        }
        
        if (header.msg_type == MSG_RESPONSE && word) {
            printf("%s ", word);
            fflush(stdout);
            free(word);
        } else {
            if (word) free(word);
            break;
        }
    }
    
    printf("\n");
    close(ss_socket);
    return ERR_SUCCESS;
}

// Execute LIST command
int execute_list(ClientState* state) {
    MessageHeader header;
    memset(&header, 0, sizeof(header));
    header.msg_type = MSG_REQUEST;
    header.op_code = OP_LIST;
    strcpy(header.username, state->username);
    header.data_length = 0;
    
    send_message(state->nm_socket, &header, NULL);
    
    char* response;
    recv_message(state->nm_socket, &header, &response);
    
    if (header.msg_type == MSG_RESPONSE) {
        printf("Users:\n%s", response);
    } else {
        printf("Error: %s\n", get_error_message(header.error_code));
    }
    
    if (response) free(response);
    return header.error_code;
}

// Execute ADDACCESS command
int execute_addaccess(ClientState* state, const char* filename, const char* username, 
                     int read, int write) {
    MessageHeader header;
    memset(&header, 0, sizeof(header));
    header.msg_type = MSG_REQUEST;
    header.op_code = OP_ADDACCESS;
    strcpy(header.username, state->username);
    strcpy(header.filename, filename);
    
    char payload[BUFFER_SIZE];
    snprintf(payload, sizeof(payload), "%s %d %d", username, read, write);
    header.data_length = strlen(payload);
    
    send_message(state->nm_socket, &header, payload);
    
    char* response;
    recv_message(state->nm_socket, &header, &response);
    
    if (header.msg_type == MSG_ACK) {
        printf("Access granted successfully!\n");
    } else {
        printf("Error: %s\n", get_error_message(header.error_code));
    }
    
    if (response) free(response);
    return header.error_code;
}

// Execute REMACCESS command
int execute_remaccess(ClientState* state, const char* filename, const char* username) {
    MessageHeader header;
    memset(&header, 0, sizeof(header));
    header.msg_type = MSG_REQUEST;
    header.op_code = OP_REMACCESS;
    strcpy(header.username, state->username);
    strcpy(header.filename, filename);
    header.data_length = strlen(username);
    
    send_message(state->nm_socket, &header, username);
    
    char* response;
    recv_message(state->nm_socket, &header, &response);
    
    if (header.msg_type == MSG_ACK) {
        printf("Access removed successfully!\n");
    } else {
        printf("Error: %s\n", get_error_message(header.error_code));
    }
    
    if (response) free(response);
    return header.error_code;
}

// Execute EXEC command
int execute_exec(ClientState* state, const char* filename) {
    MessageHeader header;
    memset(&header, 0, sizeof(header));
    header.msg_type = MSG_REQUEST;
    header.op_code = OP_EXEC;
    strcpy(header.username, state->username);
    strcpy(header.filename, filename);
    header.data_length = 0;
    
    send_message(state->nm_socket, &header, NULL);
    
    char* response;
    recv_message(state->nm_socket, &header, &response);
    
    if (header.msg_type == MSG_RESPONSE) {
        printf("%s", response);
    } else {
        printf("Error: %s\n", get_error_message(header.error_code));
    }
    
    if (response) free(response);
    return header.error_code;
}
