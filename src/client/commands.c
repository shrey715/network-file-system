#include "common.h"
#include "client.h"
#include "editor.h"
#include "input.h"
#include <ctype.h>
#include <unistd.h>
#include <termios.h>

/**
 * extract_sentence_by_index
 * @brief Extract a specific sentence from content by index.
 *
 * Sentences are delimited by '.', '!', or '?'.
 * Caller must free the returned string.
 *
 * @param content Full file content.
 * @param idx 0-based sentence index.
 * @return Allocated string with sentence content, or NULL if not found.
 */
static char* extract_sentence_by_index(const char* content, int idx) {
    if (!content || idx < 0) return NULL;
    
    const char* start = content;
    int current_idx = 0;
    
    while (*start && current_idx < idx) {
        /* Find end of current sentence */
        while (*start && *start != '.' && *start != '!' && *start != '?') {
            start++;
        }
        if (*start) start++; /* Skip delimiter */
        while (*start && isspace((unsigned char)*start)) start++; /* Skip whitespace */
        current_idx++;
    }
    
    if (current_idx != idx || !*start) {
        /* If content is empty and idx is 0, return empty string */
        if (idx == 0 && strlen(content) == 0) {
            return strdup("");
        }
        /* If no sentences but content exists and idx is 0 */
        if (idx == 0 && strlen(content) > 0) {
            return strdup(content);
        }
        return NULL;
    }
    
    /* Find end of this sentence */
    const char* end = start;
    while (*end && *end != '.' && *end != '!' && *end != '?') {
        end++;
    }
    if (*end) end++; /* Include delimiter */
    
    size_t len = end - start;
    char* result = malloc(len + 1);
    if (result) {
        strncpy(result, start, len);
        result[len] = '\0';
    }
    return result;
}

/**
 * send_nm_request_and_get_response
 * @brief Helper to send a request to NM and receive response.
 *
 * This eliminates the repetitive pattern of send + recv + error checking.
 *
 * @param state Client state (for nm_socket).
 * @param header Initialized request header to send.
 * @param payload Optional payload to send (may be NULL).
 * @param response_out Out parameter for response payload (caller must free).
 * @return ERR_SUCCESS if response received, or error code.
 */
int send_nm_request_and_get_response(ClientState* state, MessageHeader* header, const char* payload, char** response_out) {
    if (send_message(state->nm_socket, header, payload) < 0) {
        return ERR_NETWORK_ERROR;
    }
    
    if (recv_message(state->nm_socket, header, response_out) < 0) {
        return ERR_NETWORK_ERROR;
    }
    
    return ERR_SUCCESS;
}

/**
 * get_storage_server_connection
 * @brief Query NM for storage server info and establish connection.
 *
 * This helper encapsulates the common pattern:
 *  1. Ask NM which storage server handles the file
 *  2. Parse the "IP:port" response
 *  3. Connect to that storage server
 *
 * @param state Client state.
 * @param filename Target filename.
 * @param op_code Operation code for the NM request (OP_READ, OP_WRITE, etc.).
 * @param ss_socket_out Out parameter: connected socket to storage server.
 * @return ERR_SUCCESS on success, or error code on failure.
 */
int get_storage_server_connection(ClientState* state, const char* filename, int op_code, int* ss_socket_out) {
    MessageHeader header;
    init_message_header(&header, MSG_REQUEST, op_code, state->username);
    strcpy(header.filename, filename);
    
    char* ss_info = NULL;
    if (send_nm_request_and_get_response(state, &header, NULL, &ss_info) != ERR_SUCCESS) {
        if (ss_info) free(ss_info);
        return ERR_NETWORK_ERROR;
    }
    
    if (header.msg_type != MSG_RESPONSE) {
        PRINT_ERR("%s", get_error_message(header.error_code));
        if (ss_info) free(ss_info);
        return header.error_code;
    }
    
    // Parse SS IP and port
    char ss_ip[MAX_IP];
    int ss_port;
    if (parse_ss_info(ss_info, ss_ip, &ss_port) != 0) {
        PRINT_ERR("Invalid storage server info");
        free(ss_info);
        return ERR_NETWORK_ERROR;
    }
    free(ss_info);
    
    // Connect to SS
    int ss_socket = connect_to_server(ss_ip, ss_port);
    if (ss_socket < 0) {
        PRINT_ERR("Failed to connect to storage server");
        return ERR_SS_UNAVAILABLE;
    }
    
    *ss_socket_out = ss_socket;
    return ERR_SUCCESS;
}

/**
 * get_storage_server_connection_ex
 * @brief Extended version that also returns SS IP and port for live updates.
 */
int get_storage_server_connection_ex(ClientState* state, const char* filename, int op_code, 
                                     int* ss_socket_out, char* ss_ip_out, int* ss_port_out) {
    MessageHeader header;
    init_message_header(&header, MSG_REQUEST, op_code, state->username);
    strcpy(header.filename, filename);
    
    char* ss_info = NULL;
    if (send_nm_request_and_get_response(state, &header, NULL, &ss_info) != ERR_SUCCESS) {
        if (ss_info) free(ss_info);
        return ERR_NETWORK_ERROR;
    }
    
    if (header.msg_type != MSG_RESPONSE) {
        PRINT_ERR("%s", get_error_message(header.error_code));
        if (ss_info) free(ss_info);
        return header.error_code;
    }
    
    // Parse SS IP and port
    char ss_ip[MAX_IP];
    int ss_port;
    if (parse_ss_info(ss_info, ss_ip, &ss_port) != 0) {
        PRINT_ERR("Invalid storage server info");
        free(ss_info);
        return ERR_NETWORK_ERROR;
    }
    free(ss_info);
    
    // Return SS info for live updates
    if (ss_ip_out) strncpy(ss_ip_out, ss_ip, MAX_IP - 1);
    if (ss_port_out) *ss_port_out = ss_port;
    
    // Connect to SS
    int ss_socket = connect_to_server(ss_ip, ss_port);
    if (ss_socket < 0) {
        PRINT_ERR("Failed to connect to storage server");
        return ERR_SS_UNAVAILABLE;
    }
    
    *ss_socket_out = ss_socket;
    return ERR_SUCCESS;
}

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
    init_message_header(&header, MSG_REQUEST, OP_VIEW, state->username);
    header.flags = flags;
    
    char* response = NULL;
    send_nm_request_and_get_response(state, &header, NULL, &response);
    
    if (header.msg_type == MSG_RESPONSE) {
        if (flags & 2) {  // -l flag (long listing with table formatting)
            // Create and initialize table
            Table table;
            table_init(&table);
            
            // Add columns with appropriate alignment
            table_add_column(&table, "Filename", ALIGN_LEFT);
            table_add_column(&table, "Words", ALIGN_RIGHT);
            table_add_column(&table, "Chars", ALIGN_RIGHT);
            table_add_column(&table, "Last Access", ALIGN_LEFT);
            table_add_column(&table, "Owner", ALIGN_LEFT);
            
            // Parse response line by line
            if (response && strlen(response) > 0) {
                char* response_copy = strdup(response);
                if (response_copy) {
                    char* line = strtok(response_copy, "\n");
                    while (line != NULL) {
                        char* p = line;
                        // Skip leading whitespace (spaces/tabs) that may come from NM
                        while (*p && (*p == ' ' || *p == '\t')) p++;

                        // Skip empty lines
                        if (*p == '\0') {
                            line = strtok(NULL, "\n");
                            continue;
                        }

                        char filename[MAX_FILENAME];
                        int words, chars;
                        char lastaccess[32];
                        char date[16];
                        char time[16];
                        char owner[MAX_USERNAME];

                        // char debug_info[256];
                        // sprintf(debug_info, "Parsing line: '%s'", p);
                        // log_message("CLIENT", "DEBUG", debug_info);

                        // Parse the trimmed line format from name server
                        if (sscanf(p, "%s %d %d %s %s %s", 
                                  filename, &words, &chars, date, time, owner) == 6) {
                            snprintf(lastaccess, sizeof(lastaccess), "%s %s", date, time);
                            
                            table_add_row(&table);
                            int row = table.num_rows - 1;

                            table_set_cell(&table, row, 0, filename);
                            table_set_cell_int(&table, row, 1, words);
                            table_set_cell_int(&table, row, 2, chars);
                            table_set_cell(&table, row, 3, lastaccess);
                            table_set_cell(&table, row, 4, owner);
                        }

                        line = strtok(NULL, "\n");
                    }
                    free(response_copy);
                }
                
                // Print the formatted table
                if (table.num_rows > 0) {
                    table_print(&table);
                } else {
                    printf("(No files to display)\n");
                }
                table_free(&table);
            } else {
                printf("(No files to display)\n");
            }
        } else {
            // Simple listing (no table)
            if (response && strlen(response) > 0) {
                // Trim leading whitespace that may be introduced by NM
                char* trimmed = response;
                while (*trimmed && (*trimmed == ' ' || *trimmed == '\t')) trimmed++;
                printf("%s", trimmed);
                if (trimmed[strlen(trimmed) - 1] != '\n') printf("\n");
            } else {
                printf("(No files to display)\n");
            }
        }
    } else {
        PRINT_ERR("%s", get_error_message(header.error_code));
    }
    
    if (response) free(response);
    return header.error_code;
}

/**
 * execute_read
 * @brief Request the storage server location from NM then fetch file content.
 *
 * Uses helper function to establish connection to appropriate storage server,
 * then requests and displays the file content.
 *
 * @param state Client state pointer.
 * @param filename Name of the file to read.
 * @return ERR_SUCCESS on success or an ERR_* code on failure.
 */
int execute_read(ClientState* state, const char* filename) {
    int ss_socket;
    int result = get_storage_server_connection(state, filename, OP_READ, &ss_socket);
    if (result != ERR_SUCCESS) {
        return result;
    }
    
    // Request file content from SS
    MessageHeader header;
    init_message_header(&header, MSG_REQUEST, OP_SS_READ, state->username);
    safe_strncpy(header.filename, filename, sizeof(header.filename));
    
    send_message(ss_socket, &header, NULL);
    
    char* content = NULL;
    recv_message(ss_socket, &header, &content);
    safe_close_socket(&ss_socket);
    
    if (header.msg_type == MSG_RESPONSE) {
        if (content) {
            printf("%s", content);
            if (content[strlen(content) - 1] != '\n') printf("\n");
        } else {
            PRINT_WARN("(empty file)");
        }
    } else {
        PRINT_ERR("%s", get_error_message(header.error_code));
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
    // Parse filename to extract folder path and base filename
    const char* last_slash = strrchr(filename, '/');
    const char* base_filename = last_slash ? (last_slash + 1) : filename;
    
    // Validate filename - reject reserved extensions
    if (!is_valid_filename(base_filename)) {
        PRINT_ERR("Invalid filename: Cannot use reserved extensions (.meta, .undo, .stats, .checkpoint.*)");
        return ERR_INVALID_FILENAME;
    }
    
    MessageHeader header;
    memset(&header, 0, sizeof(header));
    header.msg_type = MSG_REQUEST;
    header.op_code = OP_CREATE;
    strcpy(header.username, state->username);
    
    if (last_slash) {
        // File has a folder path
        int folder_len = last_slash - filename;
        strncpy(header.foldername, filename, folder_len);
        header.foldername[folder_len] = '\0';
        strcpy(header.filename, last_slash + 1);
    } else {
        // File in root directory
        strcpy(header.filename, filename);
        header.foldername[0] = '\0';
    }
    
    header.data_length = strlen(state->username);
    
    send_message(state->nm_socket, &header, state->username);
    
    char* response;
    recv_message(state->nm_socket, &header, &response);
    
    if (header.msg_type == MSG_ACK) {
        PRINT_OK("File '%s' created successfully!", filename);
    } else {
        PRINT_ERR("%s", get_error_message(header.error_code));
    }
    
    if (response) free(response);
    return header.error_code;
}

/**
 * execute_write
 * @brief Perform a sentence-level write session against a storage server.
 *
 * Uses helper to connect to storage server, locks the requested sentence,
 * then accepts interactive word-replacement commands until ETIRW.
 *
 * @param state Client state pointer.
 * @param filename Target filename.
 * @param sentence_idx Index of the sentence to edit (0-based).
 * @return ERR_SUCCESS on success or an ERR_* code on failure.
 */
int execute_write(ClientState* state, const char* filename, int sentence_idx) {
    int ss_socket;
    int result = get_storage_server_connection(state, filename, OP_WRITE, &ss_socket);
    if (result != ERR_SUCCESS) {
        return result;
    }
    
    // Lock sentence
    MessageHeader header;
    init_message_header(&header, MSG_REQUEST, OP_SS_WRITE_LOCK, state->username);
    strcpy(header.filename, filename);
    header.sentence_index = sentence_idx;
    
    send_message(ss_socket, &header, NULL);
    
    char* response = NULL;
    recv_message(ss_socket, &header, &response);
    if (response) free(response);
    
    if (header.msg_type != MSG_ACK) {
        PRINT_ERR("%s", get_error_message(header.error_code));
        safe_close_socket(&ss_socket);
        return header.error_code;
    }
    
    printf(ANSI_BOLD ANSI_CYAN "Interactive Edit Mode" ANSI_RESET "\n");
    printf("  • Enter: " ANSI_YELLOW "word_index<space>content" ANSI_RESET "\n");
    printf("  • " ANSI_GREEN "ENTER" ANSI_RESET " = newline in content\n");
    printf("  • " ANSI_GREEN "Ctrl+N" ANSI_RESET " = submit word\n");
    printf("  • " ANSI_GREEN "ETIRW" ANSI_RESET " = finish session\n");
    printf("  • " ANSI_RED "Ctrl+C" ANSI_RESET " = abort\n");
    fflush(stdout);
    
    int success = 0;
    
    // Enable raw mode for character-by-character input
    // Check for interactive mode
    int interactive = isatty(STDIN_FILENO);
    
    if (interactive) {
        if (enable_raw_mode() == -1) {
            safe_close_socket(&ss_socket);
            return ERR_FILE_OPERATION_FAILED;
        }
    }
    
    #define MAX_WORD_CONTENT 4096
    char content_buffer[MAX_WORD_CONTENT];
    int buffer_pos = 0;
    content_buffer[0] = '\0';
    
    if (interactive) {
        printf("\n> ");
        fflush(stdout);
    }
    
    // Input loop
    while (1) {
        char c = 0;
        int submit_buffer = 0;
        
        if (interactive) {
            // Character-by-character input for interactive mode
            if (read(STDIN_FILENO, &c, 1) != 1) break;
        } else {
            // Non-interactive: Read entire line
            if (fgets(content_buffer, sizeof(content_buffer), stdin) == NULL) break;
            
            // Trim newline
            int len = strlen(content_buffer);
            while (len > 0 && (content_buffer[len-1] == '\n' || content_buffer[len-1] == '\r')) {
                content_buffer[len-1] = '\0';
                len--;
            }
            
            submit_buffer = 1;
        }
        
        if (submit_buffer) {
             c = 14; // Simulate Ctrl+N to trigger submission logic
        }

        if (c == 3) {
            printf("\n" ANSI_RED "^C - Aborting without saving" ANSI_RESET "\n");
            success = 0;
            break;
        }
        
        // Handle Ctrl+N (ASCII 14) - finish current word_idx
        if (c == 14) {  // Ctrl+N
            if (buffer_pos == 0) {
                printf("\n" ANSI_BRIGHT_BLACK "(empty - skipped)" ANSI_RESET "\n> ");
                fflush(stdout);
                continue;
            }
            
            content_buffer[buffer_pos] = '\0';
            
            // ALWAYS check for ETIRW first, regardless of flag
            if (strcmp(content_buffer, "ETIRW") == 0 || strcmp(content_buffer, "etirw") == 0) {
                // Unlock sentence
                memset(&header, 0, sizeof(header));
                header.msg_type = MSG_REQUEST;
                header.op_code = OP_SS_WRITE_UNLOCK;
                safe_strncpy(header.filename, filename, sizeof(header.filename));
                safe_strncpy(header.username, state->username, sizeof(header.username));
                header.sentence_index = sentence_idx;
                header.data_length = 0;
                
                send_message(ss_socket, &header, NULL);
                response = NULL;
                recv_message(ss_socket, &header, &response);
                if (response) free(response);
                
                if (header.msg_type == MSG_ACK) {
                    printf("\n");
                    PRINT_OK("Write successful!");
                    success = 1;
                } else {
                    printf("\n");
                    PRINT_ERR("%s", get_error_message(header.error_code));
                }
                break;
            }
            
            // Parse "wordindex content"
            char *space_ptr = strchr(content_buffer, ' ');
            if (!space_ptr) {
                printf("\n" ANSI_RED "Invalid format. Use: word_index content" ANSI_RESET "\n> ");
                buffer_pos = 0;
                content_buffer[0] = '\0';
                fflush(stdout);
                continue;
            }
            
            int word_idx;
            if (sscanf(content_buffer, "%d", &word_idx) != 1 || word_idx < 0) {
                printf("\n" ANSI_RED "Invalid word index" ANSI_RESET "\n> ");
                buffer_pos = 0;
                content_buffer[0] = '\0';
                fflush(stdout);
                continue;
            }
            
            // Get content after space
            space_ptr++;
            while (*space_ptr == ' ') space_ptr++;
            
            if (strlen(space_ptr) == 0) {
                printf("\n" ANSI_RED "Word content cannot be empty" ANSI_RESET "\n> ");
                buffer_pos = 0;
                content_buffer[0] = '\0';
                fflush(stdout);
                continue;
            }
            
            // Encode newlines as <NL> token for transmission
            char encoded_content[MAX_WORD_CONTENT * 5];  // Worst case: all newlines
            encoded_content[0] = '\0';
            const char *src = space_ptr;
            char *dst = encoded_content;
            while (*src && (dst - encoded_content) < (int)sizeof(encoded_content) - 10) {
                if (*src == '\n') {
                    strcpy(dst, "<NL>");
                    dst += 4;
                } else {
                    *dst++ = *src;
                }
                src++;
            }
            *dst = '\0';
            
            char *new_word = strdup(encoded_content);
            if (!new_word) {
                printf("\n" ANSI_RED "Memory allocation failed" ANSI_RESET "\n> ");
                buffer_pos = 0;
                content_buffer[0] = '\0';
                fflush(stdout);
                continue;
            }
            
            // Send OP_SS_WRITE_WORD
            memset(&header, 0, sizeof(header));
            header.msg_type = MSG_REQUEST;
            header.op_code = OP_SS_WRITE_WORD;
            safe_strncpy(header.filename, filename, sizeof(header.filename));
            safe_strncpy(header.username, state->username, sizeof(header.username));
            header.sentence_index = sentence_idx;
            
            size_t payload_len = snprintf(NULL, 0, "%d %s", word_idx, new_word) + 1;
            char *payload = malloc(payload_len);
            if (!payload) {
                free(new_word);
                printf("\n" ANSI_RED "Memory allocation failed" ANSI_RESET "\n> ");
                buffer_pos = 0;
                content_buffer[0] = '\0';
                fflush(stdout);
                continue;
            }
            
            snprintf(payload, payload_len, "%d %s", word_idx, new_word);
            header.data_length = strlen(payload);
            
            send_message(ss_socket, &header, payload);
            response = NULL;
            recv_message(ss_socket, &header, &response);
            if (response) free(response);
            
            if (header.msg_type == MSG_ACK) {
                printf("\n" ANSI_GREEN "✓ Word %d set" ANSI_RESET "\n> ", word_idx);
            } else {
                printf("\n" ANSI_RED "%s" ANSI_RESET "\n> ", get_error_message(header.error_code));
            }
            
            free(payload);
            free(new_word);
            
            // Reset buffer for next word
            buffer_pos = 0;
            content_buffer[0] = '\0';
            fflush(stdout);
            continue;
        }
        
        // Handle ENTER - insert newline into content
        if (c == '\n' || c == '\r') {
            if (buffer_pos < MAX_WORD_CONTENT - 1) {
                content_buffer[buffer_pos++] = '\n';
                printf("\n  ");  // Visual feedback with indent
                fflush(stdout);
            }
            continue;
        }
        
        // Handle Backspace
        if (c == 127 || c == 8) {
            if (buffer_pos > 0) {
                // Handle newline deletion specially
                if (content_buffer[buffer_pos - 1] == '\n') {
                    buffer_pos--;
                    // Move cursor up and to end of previous line
                    printf("\033[A\033[999C");  // Up one line, far right
                } else {
                    buffer_pos--;
                    printf("\b \b");
                }
                fflush(stdout);
            }
            continue;
        }
        
        // Handle printable characters
        if (isprint((unsigned char)c) && buffer_pos < MAX_WORD_CONTENT - 1) {
            content_buffer[buffer_pos++] = c;
            printf("%c", c);
            fflush(stdout);
        }
    }
    
    // Restore terminal mode
    // Restore terminal mode
    disable_raw_mode();
    
    safe_close_socket(&ss_socket);
    return success ? ERR_SUCCESS : ERR_FILE_OPERATION_FAILED;
}

/**
 * execute_undo
 * @brief Request the storage server to restore the last undo snapshot.
 *
 * Uses helper to connect to storage server, then requests undo operation.
 *
 * @param state Client state pointer.
 * @param filename Target filename.
 * @return Error code returned by the storage server.
 */
int execute_undo(ClientState* state, const char* filename) {
    int ss_socket;
    int result = get_storage_server_connection(state, filename, OP_UNDO, &ss_socket);
    if (result != ERR_SUCCESS) {
        return result;
    }
    
    // Request undo
    MessageHeader header;
    init_message_header(&header, MSG_REQUEST, OP_UNDO, state->username);
    strcpy(header.filename, filename);
    
    send_message(ss_socket, &header, NULL);
    
    char* response = NULL;
    recv_message(ss_socket, &header, &response);
    safe_close_socket(&ss_socket);
    
    if (header.msg_type == MSG_ACK) {
        PRINT_OK("Undo successful!");
    } else {
        PRINT_ERR("%s", get_error_message(header.error_code));
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
    init_message_header(&header, MSG_REQUEST, OP_INFO, state->username);
    strcpy(header.filename, filename);
    
    char* response = NULL;
    send_nm_request_and_get_response(state, &header, NULL, &response);
    
    if (header.msg_type == MSG_RESPONSE) {
        if (response) {
            printf("%s", response);
            if (response[strlen(response) - 1] != '\n') printf("\n");
        }
    } else {
        PRINT_ERR("%s", get_error_message(header.error_code));
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
    init_message_header(&header, MSG_REQUEST, OP_DELETE, state->username);
    strcpy(header.filename, filename);
    
    char* response = NULL;
    send_nm_request_and_get_response(state, &header, NULL, &response);
    
    if (header.msg_type == MSG_ACK) {
           PRINT_OK("File '%s' deleted successfully!", filename);
    } else {
           PRINT_ERR("%s", get_error_message(header.error_code));
    }
    
    if (response) free(response);
    return header.error_code;
}

/**
 * execute_stream
 * @brief Stream file contents word-by-word from the storage server and
 *        print them as they arrive.
 *
 * Uses helper to connect to storage server, then receives and prints
 * words until MSG_STOP.
 *
 * @param state Client state pointer.
 * @param filename Filename to stream.
 * @return ERR_SUCCESS on success or an ERR_* code on failure.
 */
int execute_stream(ClientState* state, const char* filename) {
    int ss_socket;
    int result = get_storage_server_connection(state, filename, OP_STREAM, &ss_socket);
    if (result != ERR_SUCCESS) {
        return result;
    }
    
    // Request stream
    MessageHeader header;
    init_message_header(&header, MSG_REQUEST, OP_STREAM, state->username);
    strcpy(header.filename, filename);
    
    send_message(ss_socket, &header, NULL);
    
    // Receive and print words
    while (1) {
        char* word = NULL;
        recv_message(ss_socket, &header, &word);
        
        if (header.msg_type == MSG_STOP) {
            if (word) free(word);
            break;
        }

        if (header.msg_type == MSG_ERROR) {
            printf("\n"); PRINT_ERR("%s", get_error_message(header.error_code));
            if (word) free(word);
            safe_close_socket(&ss_socket);
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
    safe_close_socket(&ss_socket);
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
    init_message_header(&header, MSG_REQUEST, OP_LIST, state->username);
    
    char* response = NULL;
    send_nm_request_and_get_response(state, &header, NULL, &response);
    
    if (header.msg_type == MSG_RESPONSE) {
        if (response) {
            printf("Users:\n%s", response);
            if (response[strlen(response) - 1] != '\n') printf("\n");
        } else {
            printf("Users:\n");
        }
    } else {
        PRINT_ERR("%s", get_error_message(header.error_code));
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
    init_message_header(&header, MSG_REQUEST, OP_ADDACCESS, state->username);
    strcpy(header.filename, filename);
    
    char payload[BUFFER_SIZE];
    snprintf(payload, sizeof(payload), "%s %d %d", username, read, write);
    header.data_length = strlen(payload);
    
    char* response = NULL;
    send_nm_request_and_get_response(state, &header, payload, &response);
    
    if (header.msg_type == MSG_ACK) {
           PRINT_OK("Access granted successfully!");
    } else {
           PRINT_ERR("%s", get_error_message(header.error_code));
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
    init_message_header(&header, MSG_REQUEST, OP_REMACCESS, state->username);
    strcpy(header.filename, filename);
    header.data_length = strlen(username);
    
    char* response = NULL;
    send_nm_request_and_get_response(state, &header, username, &response);
    
    if (header.msg_type == MSG_ACK) {
           PRINT_OK("Access removed successfully!");
    } else {
           PRINT_ERR("%s", get_error_message(header.error_code));
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
    init_message_header(&header, MSG_REQUEST, OP_EXEC, state->username);
    strcpy(header.filename, filename);
    
    char* response = NULL;
    send_nm_request_and_get_response(state, &header, NULL, &response);
    
    if (header.msg_type == MSG_RESPONSE) {
        if (response) {
            printf("%s", response);
            if (response[strlen(response) - 1] != '\n') printf("\n");
        }
    } else {
        PRINT_ERR("%s", get_error_message(header.error_code));
    }
    
    if (response) free(response);
    return header.error_code;
}

/**
 * execute_createfolder
 * @brief Request NM to create a new folder.
 *
 * @param state Client state pointer.
 * @param foldername Folder name/path to create.
 * @return ERR_SUCCESS on success or an ERR_* code on failure.
 */
int execute_createfolder(ClientState* state, const char* foldername) {
    MessageHeader header;
    memset(&header, 0, sizeof(header));
    header.msg_type = MSG_REQUEST;
    header.op_code = OP_CREATEFOLDER;
    strcpy(header.username, state->username);
    strcpy(header.foldername, foldername);
    header.data_length = 0;
    
    send_message(state->nm_socket, &header, NULL);
    
    char* response;
    recv_message(state->nm_socket, &header, &response);
    
    if (header.msg_type == MSG_ACK) {
        PRINT_OK("Folder '%s' created successfully!", foldername);
    } else {
        PRINT_ERR("%s", get_error_message(header.error_code));
    }
    
    if (response) free(response);
    return header.error_code;
}

/**
 * execute_move
 * @brief Request NM to move a file to a different folder.
 *
 * @param state Client state pointer.
 * @param filename File to move.
 * @param foldername Destination folder (empty string for root).
 * @return ERR_SUCCESS on success or an ERR_* code on failure.
 */
int execute_move(ClientState* state, const char* filename, const char* foldername) {
    MessageHeader header;
    memset(&header, 0, sizeof(header));
    header.msg_type = MSG_REQUEST;
    header.op_code = OP_MOVE;
    safe_strncpy(header.username, state->username, sizeof(header.username));
    safe_strncpy(header.filename, filename, sizeof(header.filename));
    safe_strncpy(header.foldername, foldername, sizeof(header.foldername));
    header.data_length = 0;
    
    send_message(state->nm_socket, &header, NULL);
    
    char* response;
    recv_message(state->nm_socket, &header, &response);
    
    if (header.msg_type == MSG_ACK) {
        PRINT_OK("File '%s' moved to folder '%s' successfully!", 
               filename, foldername[0] ? foldername : "/");
    } else {
        PRINT_ERR("%s", get_error_message(header.error_code));
    }
    
    if (response) free(response);
    return header.error_code;
}

/**
 * execute_viewfolder
 * @brief Request NM to list contents of a folder.
 *
 * @param state Client state pointer.
 * @param foldername Folder to view (empty string for root).
 * @return ERR_SUCCESS on success or an ERR_* code on failure.
 */
int execute_viewfolder(ClientState* state, const char* foldername) {
    MessageHeader header;
    memset(&header, 0, sizeof(header));
    header.msg_type = MSG_REQUEST;
    header.op_code = OP_VIEWFOLDER;
    strcpy(header.username, state->username);
    if (foldername && strlen(foldername) > 0) {
        strcpy(header.foldername, foldername);
    } else {
        header.foldername[0] = '\0';  // Root folder
    }
    header.data_length = 0;
    
    send_message(state->nm_socket, &header, NULL);
    
    char* response;
    recv_message(state->nm_socket, &header, &response);
    
    if (header.msg_type == MSG_RESPONSE) {
        if (response) {
            printf("Contents of '%s':\n%s", 
                   foldername && foldername[0] ? foldername : "/", 
                   response);
            if (response[strlen(response) - 1] != '\n') printf("\n");
        } else {
            printf("Contents of '%s':\n", foldername && foldername[0] ? foldername : "/");
        }
    } else {
        PRINT_ERR("%s", get_error_message(header.error_code));
    }
    
    if (response) free(response);
    return header.error_code;
}

/**
 * execute_checkpoint
 * @brief Creates a checkpoint for a file with the specified tag
 */
int execute_checkpoint(ClientState* state, const char* filename, const char* checkpoint_tag) {
    MessageHeader header;
    memset(&header, 0, sizeof(header));
    header.msg_type = MSG_REQUEST;
    header.op_code = OP_CHECKPOINT;
    strcpy(header.username, state->username);
    strcpy(header.filename, filename);
    strcpy(header.checkpoint_tag, checkpoint_tag);
    header.data_length = 0;
    
    send_message(state->nm_socket, &header, NULL);
    
    char* response;
    recv_message(state->nm_socket, &header, &response);
    
    if (header.msg_type == MSG_ACK) {
        PRINT_OK("Checkpoint '%s' created successfully for file '%s'.", checkpoint_tag, filename);
    } else {
        PRINT_ERR("Error creating checkpoint: %s", get_error_message(header.error_code));
    }
    
    if (response) free(response);
    return header.error_code;
}

/**
 * execute_viewcheckpoint
 * @brief Views the content of a specific checkpoint
 */
int execute_viewcheckpoint(ClientState* state, const char* filename, const char* checkpoint_tag) {
    MessageHeader header;
    memset(&header, 0, sizeof(header));
    header.msg_type = MSG_REQUEST;
    header.op_code = OP_VIEWCHECKPOINT;
    strcpy(header.username, state->username);
    strcpy(header.filename, filename);
    strcpy(header.checkpoint_tag, checkpoint_tag);
    header.data_length = 0;
    
    send_message(state->nm_socket, &header, NULL);
    
    char* response;
    recv_message(state->nm_socket, &header, &response);
    
    if (header.msg_type == MSG_RESPONSE) {
        printf("=== Checkpoint '%s' for file '%s' ===\n", checkpoint_tag, filename);
        printf("%s\n", response);
        printf("=== End of checkpoint ===\n");
    } else {
        PRINT_ERR("Error viewing checkpoint: %s", get_error_message(header.error_code));
    }
    
    if (response) free(response);
    return header.error_code;
}

/**
 * execute_revert
 * @brief Reverts a file to a specific checkpoint
 */
int execute_revert(ClientState* state, const char* filename, const char* checkpoint_tag) {
    MessageHeader header;
    memset(&header, 0, sizeof(header));
    header.msg_type = MSG_REQUEST;
    header.op_code = OP_REVERT;
    strcpy(header.username, state->username);
    strcpy(header.filename, filename);
    strcpy(header.checkpoint_tag, checkpoint_tag);
    header.data_length = 0;
    
    send_message(state->nm_socket, &header, NULL);
    
    char* response;
    recv_message(state->nm_socket, &header, &response);
    
    if (header.msg_type == MSG_ACK) {
        PRINT_OK("File '%s' successfully reverted to checkpoint '%s'.", filename, checkpoint_tag);
    } else {
        PRINT_ERR("Error reverting to checkpoint: %s", get_error_message(header.error_code));
    }
    
    if (response) free(response);
    return header.error_code;
}

/**
 * execute_listcheckpoints
 * @brief Lists all checkpoints for a file
 */
int execute_listcheckpoints(ClientState* state, const char* filename) {
    MessageHeader header;
    memset(&header, 0, sizeof(header));
    header.msg_type = MSG_REQUEST;
    header.op_code = OP_LISTCHECKPOINTS;
    strcpy(header.username, state->username);
    strcpy(header.filename, filename);
    header.data_length = 0;
    
    send_message(state->nm_socket, &header, NULL);
    
    char* response;
    recv_message(state->nm_socket, &header, &response);
    
    if (header.msg_type == MSG_RESPONSE) {
        if (response) {
            printf("%s", response);
            if (response[strlen(response) - 1] != '\n') printf("\n");
        }
    } else {
        PRINT_ERR("Error listing checkpoints: %s", get_error_message(header.error_code));
    }
    
    if (response) free(response);
    return header.error_code;
}

/**
 * execute_requestaccess
 * @brief Request access to a file
 * @param flags: bit 0 = read, bit 1 = write (0 = default to read only)
 */
int execute_requestaccess(ClientState* state, const char* filename, int flags) {
    MessageHeader header;
    memset(&header, 0, sizeof(header));
    header.msg_type = MSG_REQUEST;
    header.op_code = OP_REQUESTACCESS;
    strcpy(header.username, state->username);
    strcpy(header.filename, filename);
    header.flags = flags;
    header.data_length = 0;
    
    send_message(state->nm_socket, &header, NULL);
    
    char* response;
    recv_message(state->nm_socket, &header, &response);
    
    if (header.msg_type == MSG_ACK) {
        // Determine what was requested based on flags
        // -W flag means both read and write, -R only means read, no flags means read
        int read_flag = (flags & 0x01) ? 1 : 0;
        int write_flag = (flags & 0x02) ? 1 : 0;
        
        int read_req, write_req;
        if (write_flag) {
            // -W flag present: both read and write
            read_req = 1;
            write_req = 1;
        } else if (read_flag) {
            // Only -R flag: read only
            read_req = 1;
            write_req = 0;
        } else {
            // No flags: default to read only
            read_req = 1;
            write_req = 0;
        }
        
        char perm_str[64];
        if (read_req && write_req) {
            strcpy(perm_str, "read and write");
        } else {
            strcpy(perm_str, "read");
        }
        
        PRINT_OK("Access request for %s submitted successfully for '%s'.", perm_str, filename);
        printf("The file owner will be able to approve or deny your request.\n");
    } else if (header.error_code == ERR_ALREADY_HAS_ACCESS) {
        // Parse what access they already have from the flags field
        int has_read = (header.flags & 0x01) ? 1 : 0;
        int has_write = (header.flags & 0x02) ? 1 : 0;
        
        char access_str[64];
        if (has_read && has_write) {
            strcpy(access_str, "read and write access");
        } else if (has_write) {
            strcpy(access_str, "write access");
        } else if (has_read) {
            strcpy(access_str, "read access");
        } else {
            strcpy(access_str, "access");
        }
        
        PRINT_INFO("You already have %s to '%s'.", access_str, filename);
    } else {
        PRINT_ERR("%s", get_error_message(header.error_code));
    }
    
    if (response) free(response);
    return header.error_code;
}

/**
 * execute_viewrequests
 * @brief View pending access requests for a file (owner only)
 */
int execute_viewrequests(ClientState* state, const char* filename) {
    MessageHeader header;
    memset(&header, 0, sizeof(header));
    header.msg_type = MSG_REQUEST;
    header.op_code = OP_VIEWREQUESTS;
    strcpy(header.username, state->username);
    strcpy(header.filename, filename);
    header.data_length = 0;
    
    send_message(state->nm_socket, &header, NULL);
    
    char* response;
    recv_message(state->nm_socket, &header, &response);
    
    if (header.msg_type == MSG_RESPONSE) {
        if (response) {
            printf("%s", response);
            if (response[strlen(response) - 1] != '\n') printf("\n");
        }
    } else {
        PRINT_ERR("%s", get_error_message(header.error_code));
    }
    
    if (response) free(response);
    return header.error_code;
}

/**
 * execute_approverequest
 * @brief Approve an access request (owner only)
 */
int execute_approverequest(ClientState* state, const char* filename, const char* username) {
    MessageHeader header;
    memset(&header, 0, sizeof(header));
    header.msg_type = MSG_REQUEST;
    header.op_code = OP_APPROVEREQUEST;
    strcpy(header.username, state->username);
    strcpy(header.filename, filename);
    header.data_length = strlen(username);
    
    send_message(state->nm_socket, &header, username);
    
    char* response;
    recv_message(state->nm_socket, &header, &response);
    
    if (header.msg_type == MSG_ACK) {
           PRINT_OK("Access request from '%s' approved successfully.", username);
           PRINT_OK("User '%s' has been granted access to '%s'.", username, filename);
    } else {
           PRINT_ERR("%s", get_error_message(header.error_code));
    }
    
    if (response) free(response);
    return header.error_code;
}

/**
 * execute_denyrequest
 * @brief Deny an access request (owner only)
 */
int execute_denyrequest(ClientState* state, const char* filename, const char* username) {
    MessageHeader header;
    memset(&header, 0, sizeof(header));
    header.msg_type = MSG_REQUEST;
    header.op_code = OP_DENYREQUEST;
    strcpy(header.username, state->username);
    strcpy(header.filename, filename);
    header.data_length = strlen(username);
    
    send_message(state->nm_socket, &header, username);
    
    char* response;
    recv_message(state->nm_socket, &header, &response);
    
    if (header.msg_type == MSG_ACK) {
           PRINT_OK("Access request from '%s' denied successfully.", username);
    } else {
           PRINT_ERR("%s", get_error_message(header.error_code));
    }
    
    if (response) free(response);
    return header.error_code;
}

/**
 * execute_edit
 * @brief Open a sentence in the terminal editor for editing.
 *
 * Locks the sentence, fetches its content, opens the editor,
 * and saves changes back when done.
 *
 * @param state Client state pointer.
 * @param filename Target filename.
 * @param sentence_idx Sentence index to edit (0-based).
 * @return ERR_SUCCESS on success or an ERR_* code on failure.
 */
int execute_edit(ClientState* state, const char* filename, int sentence_idx) {
    int ss_socket;
    int result = get_storage_server_connection(state, filename, OP_WRITE, &ss_socket);
    if (result != ERR_SUCCESS) {
        return result;
    }

    /* Lock sentence */
    MessageHeader header;
    init_message_header(&header, MSG_REQUEST, OP_SS_WRITE_LOCK, state->username);
    strcpy(header.filename, filename);
    header.sentence_index = sentence_idx;

    send_message(ss_socket, &header, NULL);

    char* response = NULL;
    recv_message(ss_socket, &header, &response);
    if (response) free(response);

    if (header.msg_type != MSG_ACK) {
        PRINT_ERR("%s", get_error_message(header.error_code));
        safe_close_socket(&ss_socket);
        return header.error_code;
    }

    /* Fetch current sentence content */
    init_message_header(&header, MSG_REQUEST, OP_SS_READ, state->username);
    safe_strncpy(header.filename, filename, sizeof(header.filename));
    header.sentence_index = sentence_idx;

    send_message(ss_socket, &header, NULL);

    char* content = NULL;
    recv_message(ss_socket, &header, &content);

    if (header.msg_type != MSG_RESPONSE) {
        if (content) free(content);
        safe_close_socket(&ss_socket);
        return header.error_code;
    }

    /* Extract just the sentence at sentence_idx */
    char* sentence_content = extract_sentence_by_index(content, sentence_idx);
    if (content) free(content);

    char* new_content = NULL;
    int should_save = 0;

    if (isatty(STDIN_FILENO)) {
        /* Interactive Mode: Run TUI Editor */
        EditorState* E = editor_init();
        if (!E) {
            if (sentence_content) free(sentence_content);
            safe_close_socket(&ss_socket);
            return ERR_FILE_OPERATION_FAILED;
        }

        enable_raw_mode();
        editor_load_content(E, sentence_content ? sentence_content : "");
        editor_set_file_info(E, filename, sentence_idx, 1, state->username);
        editor_set_status(E, "Editing sentence %d - Ctrl+S to save, Ctrl+Q to quit", sentence_idx);
        
        /* Run editor */
        editor_run(E);

        /* Get edited content */
        new_content = editor_get_content(E);
        should_save = E->save_requested;

        disable_raw_mode();
        editor_destroy(E);
    } else {
        /* Non-Interactive Mode: Read from Stdin */
        // Read entire stdin content
        // We can reuse read_file_content if we pass /dev/stdin mechanism or just implementation
        // Since read_file_content uses fseek SEEK_END which might not work on pipe...
        // We should implement a simple read-until-EOF loop.
        
        size_t cap = 1024;
        size_t len = 0;
        new_content = malloc(cap);
        
        if (new_content) {
            char buf[1024];
            size_t n;
            while ((n = fread(buf, 1, sizeof(buf), stdin)) > 0) {
                if (len + n >= cap) {
                    cap *= 2;
                    char* tmp = realloc(new_content, cap);
                    if (!tmp) { free(new_content); new_content = NULL; break; }
                    new_content = tmp;
                }
                memcpy(new_content + len, buf, n);
                len += n;
            }
            if (new_content) new_content[len] = '\0';
            should_save = 1;
        }
    }

    if (sentence_content) free(sentence_content);

    /* Reconnect to storage server (connection may have closed during edit) */
    int ss_socket2;
    result = get_storage_server_connection(state, filename, OP_WRITE, &ss_socket2);
    if (result != ERR_SUCCESS) {
        if (new_content) free(new_content);
        PRINT_ERR("Failed to reconnect to storage server for save");
        return result;
    }

    /* Save if requested */
    if (should_save && new_content) {
        init_message_header(&header, MSG_REQUEST, OP_SS_WRITE_WORD, state->username);
        safe_strncpy(header.filename, filename, sizeof(header.filename));
        header.sentence_index = sentence_idx;

        /* Send with word_idx=-1 to replace entire sentence content */
        char payload[4096];
        snprintf(payload, sizeof(payload), "-1 %s", new_content);
        header.data_length = strlen(payload);

        send_message(ss_socket2, &header, payload);
        response = NULL;
        recv_message(ss_socket2, &header, &response);
        if (response) free(response);

        if (header.msg_type != MSG_ACK) {
            PRINT_ERR("Save failed: %s", get_error_message(header.error_code));
        }
    }

    if (new_content) free(new_content);

    /* Unlock sentence */
    init_message_header(&header, MSG_REQUEST, OP_SS_WRITE_UNLOCK, state->username);
    strcpy(header.filename, filename);
    header.sentence_index = sentence_idx;

    send_message(ss_socket2, &header, NULL);
    response = NULL;
    recv_message(ss_socket2, &header, &response);
    if (response) free(response);

    safe_close_socket(&ss_socket2);

    if (should_save) {
        PRINT_OK("Changes saved!");
    } else {
        printf("No changes saved.\n");
    }

    return ERR_SUCCESS;
}

/**
 * execute_open
 * @brief Open a file in read-only mode using the terminal editor.
 *
 * This provides a nano-like viewing experience without locking.
 *
 * @param state Client state pointer.
 * @param filename Target filename.
 * @return ERR_SUCCESS on success or an ERR_* code on failure.
 */
int execute_open(ClientState* state, const char* filename) {
    int ss_socket;
    char ss_ip[MAX_IP] = {0};
    int ss_port = 0;
    
    int result = get_storage_server_connection_ex(state, filename, OP_READ, &ss_socket, ss_ip, &ss_port);
    if (result != ERR_SUCCESS) {
        return result;
    }

    /* Request file content */
    MessageHeader header;
    init_message_header(&header, MSG_REQUEST, OP_SS_READ, state->username);
    strcpy(header.filename, filename);

    send_message(ss_socket, &header, NULL);

    char* content = NULL;
    recv_message(ss_socket, &header, &content);
    safe_close_socket(&ss_socket);

    if (header.msg_type != MSG_RESPONSE) {
        PRINT_ERR("%s", get_error_message(header.error_code));
        if (content) free(content);
        return header.error_code;
    }

    /* Initialize editor in read-only mode */
    EditorState* E = editor_init();
    if (!E) {
        if (content) free(content);
        return ERR_FILE_OPERATION_FAILED;
    }

    enable_raw_mode();
    editor_load_content(E, content ? content : "(empty file)");
    editor_set_file_info(E, filename, -1, 0, NULL);
    E->read_only = 1;
    
    /* Enable live updates - poll for changes every 2 seconds */
    editor_enable_live_updates(E, ss_ip, ss_port, state->username);
    editor_set_status(E, "View mode (LIVE) - Ctrl+Q to quit");
    
    if (content) free(content);

    /* Run editor (read-only viewing with live updates) */
    editor_run(E);

    disable_raw_mode();
    editor_destroy(E);

    return ERR_SUCCESS;
}
