#include "common.h"
#include "client.h"

/**
 * execute_view
 * @brief Send a VIEW request to the Name Server and print the response.
 *
 * Supports flags for showing all files (-a) and long listing (-l).
 *
 * @param state Pointer to the client's state (contains NM socket & username).
 * @param flags Bitmask where bit 0 => -a, bit 1 => -l.
 * @return Error code returned by the Name Server (ERR_SUCCESS on success).
 */
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

/**
 * execute_read
 * @brief Request the storage server location from NM then fetch file content.
 *
 * Steps:
 *  1. Ask NM for the storage server holding `filename`.
 *  2. Connect to the storage server and request the file content.
 *  3. Print file contents or an error message.
 *
 * @param state Client state pointer.
 * @param filename Name of the file to read.
 * @return ERR_SUCCESS on success or an ERR_* code on failure.
 */
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
    
    char* content = NULL;
    recv_message(ss_socket, &header, &content);
    close(ss_socket);
    
    if (header.msg_type == MSG_RESPONSE) {
        if (content) {
            printf("%s\n", content);
        } else {
            printf("(empty file)\n");
        }
    } else {
        printf("Error: %s\n", get_error_message(header.error_code));
    }
    
    if (content) free(content);
    return header.error_code;
}

/**
 * execute_create
 * @brief Ask NM to create a new file; NM selects a storage server and the
 *        storage server creates the file.
 *
 * @param state Client state pointer.
 * @param filename Name of the file to create.
 * @return Error code returned by NM.
 */
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

/**
 * execute_write
 * @brief Perform a sentence-level write session against a storage server.
 *
 * The client requests storage server info from NM, obtains a lock on the
 * requested sentence, then accepts interactive word-replacement commands from
 * stdin until the user types ETIRW to finish and release the lock.
 *
 * @param state Client state pointer.
 * @param filename Target filename.
 * @param sentence_idx Index of the sentence to edit (0-based).
 * @return ERR_SUCCESS on success or an ERR_* code on failure.
 */
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
    
    char* ss_info = NULL;
    recv_message(state->nm_socket, &header, &ss_info);
    
    if (header.msg_type != MSG_RESPONSE) {
        printf("Error: %s\n", get_error_message(header.error_code));
        if (ss_info) free(ss_info);
        return header.error_code;
    }
    
    // Parse SS IP and port
    char ss_ip[MAX_IP];
    int ss_port;
    if (sscanf(ss_info, "%[^:]:%d", ss_ip, &ss_port) != 2) {
        printf("Error: Invalid storage server info\n");
        free(ss_info);
        return ERR_NETWORK_ERROR;
    }
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
    
    char* response = NULL;
    recv_message(ss_socket, &header, &response);
    if (response) free(response);
    
    if (header.msg_type != MSG_ACK) {
        printf("Error: %s\n", get_error_message(header.error_code));
        close(ss_socket);
        return header.error_code;
    }
    
    printf("Sentence locked. Enter word modifications (word_index content), then ETIRW:\n");
    printf("  Format: <word_index> <content>\n");
    printf("  Type ETIRW when done.\n");
    fflush(stdout);
    
    int success = 0;
    
    // Read write commands - use getline for robustness
    while (1) {
        printf("  ");
        fflush(stdout);
        
        // Check and clear stdin errors
        if (feof(stdin)) clearerr(stdin);
        if (ferror(stdin)) clearerr(stdin);
        
        char* line = NULL;
        size_t len = 0;
        ssize_t read;
        
        // Use getline for dynamic allocation and better EOF handling
        read = getline(&line, &len, stdin);
        
        if (read == -1) {
            // True EOF or error
            if (line) free(line);
            if (feof(stdin)) {
                printf("\n(Reached end of input - unlocking without saving)\n");
                clearerr(stdin);
            } else {
                printf("\n(Input error - unlocking without saving)\n");
            }
            break;
        }
        
        // Remove trailing newline/whitespace
        while (read > 0 && (line[read-1] == '\n' || line[read-1] == '\r' || 
                            line[read-1] == ' ' || line[read-1] == '\t')) {
            line[--read] = '\0';
        }
        
        // Skip empty lines
        if (read == 0 || strlen(line) == 0) {
            free(line);
            continue;
        }
        
        // Check for ETIRW (finish and save)
        if (strcmp(line, "ETIRW") == 0 || strcmp(line, "etirw") == 0) {
            free(line);
            // Unlock sentence
            memset(&header, 0, sizeof(header));
            header.msg_type = MSG_REQUEST;
            header.op_code = OP_SS_WRITE_UNLOCK;
            strcpy(header.filename, filename);
            strcpy(header.username, state->username);
            header.sentence_index = sentence_idx;
            header.data_length = 0;
            
            send_message(ss_socket, &header, NULL);
            
            response = NULL;
            recv_message(ss_socket, &header, &response);
            if (response) free(response);
            
            if (header.msg_type == MSG_ACK) {
                printf("Write successful!\n");
                success = 1;
            } else {
                printf("Error: %s\n", get_error_message(header.error_code));
            }
            break;
        }
        
        // Parse word modification: "word_index content"
        int word_idx;
        char* space_ptr = strchr(line, ' ');
        
        if (!space_ptr) {
            printf("  Invalid format. Use: <word_index> <content>\n");
            printf("  Example: 0 Hello\n");
            free(line);
            continue;
        }
        
        if (sscanf(line, "%d", &word_idx) != 1 || word_idx < 0) {
            printf("  Invalid word index. Must be a non-negative integer.\n");
            free(line);
            continue;
        }
        
        // Get the rest of the line as the new word (skip leading spaces after index)
        space_ptr++;
        while (*space_ptr == ' ' || *space_ptr == '\t') space_ptr++;
        
        if (strlen(space_ptr) == 0) {
            printf("  Error: Word content cannot be empty.\n");
            free(line);
            continue;
        }
        
        // Allocate new_word dynamically to handle any length
        char* new_word = strdup(space_ptr);
        if (!new_word) {
            printf("  Error: Memory allocation failed.\n");
            free(line);
            continue;
        }
        
        // Send write word command
        memset(&header, 0, sizeof(header));
        header.msg_type = MSG_REQUEST;
        header.op_code = OP_SS_WRITE_WORD;
        strcpy(header.filename, filename);
        strcpy(header.username, state->username);
        header.sentence_index = sentence_idx;
        
        // Build payload dynamically
        size_t payload_len = snprintf(NULL, 0, "%d %s", word_idx, new_word) + 1;
        char* payload = malloc(payload_len);
        if (!payload) {
            printf("  Error: Memory allocation failed.\n");
            free(new_word);
            free(line);
            continue;
        }
        
        snprintf(payload, payload_len, "%d %s", word_idx, new_word);
        header.data_length = strlen(payload);
        
        send_message(ss_socket, &header, payload);
        
        response = NULL;
        recv_message(ss_socket, &header, &response);
        
        if (response) free(response);
        
        if (header.msg_type == MSG_ACK) {
            printf("  -> Word %d set to: %s\n", word_idx, new_word);
        } else {
            printf("  Error: %s\n", get_error_message(header.error_code));
            printf("  (Try using a valid word index or type ETIRW to finish)\n");
        }
        
        free(payload);
        free(new_word);
        free(line);
    }
    
    close(ss_socket);
    return success ? ERR_SUCCESS : ERR_FILE_OPERATION_FAILED;
}

/**
 * execute_undo
 * @brief Request the storage server to restore the last undo snapshot.
 *
 * @param state Client state pointer.
 * @param filename Target filename.
 * @return Error code returned by the storage server.
 */
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

/**
 * execute_info
 * @brief Request file metadata information from NM and print it.
 *
 * @param state Client state pointer.
 * @param filename Filename to query.
 * @return ERR_SUCCESS on success or an ERR_* code on failure.
 */
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

/**
 * execute_delete
 * @brief Request NM to delete a file. NM verifies ownership and forwards
 *        the deletion to the storage server.
 *
 * @param state Client state pointer.
 * @param filename Filename to delete.
 * @return Error code returned by NM.
 */
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

/**
 * execute_stream
 * @brief Stream file contents word-by-word from the storage server and
 *        print them as they arrive.
 *
 * @param state Client state pointer.
 * @param filename Filename to stream.
 * @return ERR_SUCCESS on success or an ERR_* code on failure.
 */
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

        if (header.msg_type == MSG_ERROR) {
            printf("\nError: %s\n", get_error_message(header.error_code));
            if (word) free(word);
            close(ss_socket);
            return header.error_code;
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

/**
 * execute_list
 * @brief Request NM for the currently registered clients and print them.
 *
 * @param state Client state pointer.
 * @return ERR_SUCCESS on success or an ERR_* code on failure.
 */
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

/**
 * execute_addaccess
 * @brief Request NM to add or update an ACL entry for `username` on `filename`.
 *
 * @param state Client state pointer.
 * @param filename Target filename.
 * @param username Username to grant access to.
 * @param read Non-zero to grant read access.
 * @param write Non-zero to grant write access.
 * @return ERR_SUCCESS on success or an ERR_* code on failure.
 */
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

/**
 * execute_remaccess
 * @brief Request NM to remove an ACL entry for `username` on `filename`.
 *
 * Only the file owner is permitted to remove ACL entries.
 *
 * @param state Client state pointer.
 * @param filename Target filename.
 * @param username Username to remove.
 * @return ERR_SUCCESS on success or an ERR_* code on failure.
 */
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

/**
 * execute_exec
 * @brief Request NM and the storage server to fetch the file and execute it
 *        as shell commands, returning command output.
 *
 * Use with caution: executing arbitrary file contents can be dangerous.
 *
 * @param state Client state pointer.
 * @param filename Filename to execute on the server.
 * @return ERR_SUCCESS on success or an ERR_* code on failure.
 */
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
