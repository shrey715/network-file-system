/**
 * ss_handlers.c - Storage Server Request Handlers
 *
 * This file contains the handler functions for client requests to the
 * Storage Server. Split from sentence.c for better code organization.
 */

#include "common.h"
#include "storage_server.h"

extern SSConfig config;

/**
 * handle_ss_create
 * @brief Handler for OP_SS_CREATE operation.
 *
 * @param client_fd Client socket file descriptor.
 * @param header Request message header.
 * @param payload Owner username.
 * @return Error code from the operation.
 */
int handle_ss_create(int client_fd, MessageHeader* header, const char* payload) {
    // Construct full path from foldername and filename
    char fullpath[MAX_PATH];
    construct_full_path(fullpath, sizeof(fullpath), header->foldername, header->filename);
    
    // Log operation start
    char details[1200];
    snprintf(details, sizeof(details), "file=%s owner=%s", fullpath, payload ? payload : "unknown");
    log_message("SS", "INFO", details);
    
    int result = ss_create_file(fullpath, payload ? payload : "unknown");
    
    // Synchronous Replication
    if (result == ERR_SUCCESS) {
        ss_forward_to_replica(header, payload, "CREATE");
    }
    
    // Log completion
    if (result == ERR_SUCCESS) {
        char msg[1200];
        snprintf(msg, sizeof(msg), "[SUCCESS] File '%s' created successfully", fullpath);
        log_message("SS", "INFO", msg);
    } else {
        char msg[1200];
        snprintf(msg, sizeof(msg), "✗ File creation failed for '%s': %s", 
                 fullpath, get_error_message(result));
        log_message("SS", "ERROR", msg);
    }
    
    send_simple_response(client_fd, 
                        (result == ERR_SUCCESS) ? MSG_ACK : MSG_ERROR, 
                        result);
    return result;
}

/**
 * handle_ss_delete
 * @brief Handler for OP_SS_DELETE operation.
 */
int handle_ss_delete(int client_fd, MessageHeader* header) {
    char details[1200];
    snprintf(details, sizeof(details), "file=%s", header->filename);
    log_message("SS", "INFO", details);
    
    int result = ss_delete_file(header->filename);
    
    // Synchronous Replication
    if (result == ERR_SUCCESS) {
        ss_forward_to_replica(header, NULL, "DELETE");
    }

    if (result == ERR_SUCCESS) {
        char msg[1200];
        snprintf(msg, sizeof(msg), "[SUCCESS] File '%s' deleted successfully", header->filename);
        log_message("SS", "INFO", msg);
    } else {
        char msg[1200];
        snprintf(msg, sizeof(msg), "[ERROR] File deletion failed for '%s': %s", 
                 header->filename, get_error_message(result));
        log_message("SS", "ERROR", msg);
    }
    
    send_simple_response(client_fd, 
                        (result == ERR_SUCCESS) ? MSG_ACK : MSG_ERROR, 
                        result);
    return result;
}

/**
 * handle_ss_read
 * @brief Handler for OP_SS_READ operation.
 */
int handle_ss_read(int client_fd, MessageHeader* header) {
    char details[1200];
    snprintf(details, sizeof(details), "file=%s user=%s", header->filename, header->username);
    log_message("SS", "INFO", details);
    
    char* content = NULL;
    int result = ss_read_file(header->filename, &content);
    
    if (result == ERR_SUCCESS) {
        char msg[1200];
        snprintf(msg, sizeof(msg), "✓ File '%s' read successfully (%zu bytes)", 
                 header->filename, content ? strlen(content) : 0);
        log_message("SS", "INFO", msg);
        
        MessageHeader resp;
        memset(&resp, 0, sizeof(resp));
        resp.msg_type = MSG_RESPONSE;
        resp.error_code = ERR_SUCCESS;
        resp.data_length = strlen(content);
        send_message(client_fd, &resp, content);
        free(content);
    } else {
        send_simple_response(client_fd, MSG_ERROR, result);
    }
    return result;
}

/**
 * handle_ss_write_lock
 * @brief Handler for OP_SS_WRITE_LOCK operation.
 */
void handle_ss_write_lock(int client_fd, MessageHeader* header) {
    int result = ss_write_lock(header->filename, header->sentence_index, header->username);
    
    // Synchronous Replication
    if (result == ERR_SUCCESS) {
        ss_forward_to_replica(header, NULL, "WRITE_LOCK");
    }

    send_simple_response(client_fd, 
                        (result == ERR_SUCCESS) ? MSG_ACK : MSG_ERROR, 
                        result);
}

/**
 * handle_ss_write_word
 * @brief Handler for OP_SS_WRITE_WORD operation.
 *
 * Parses payload format: "word_index <new_word...>" and updates the word.
 */
void handle_ss_write_word(int client_fd, MessageHeader* header, const char* payload) {
    if (!payload) {
        send_simple_response(client_fd, MSG_ERROR, ERR_INVALID_WORD);
        return;
    }

    char* space_ptr = strchr(payload, ' ');
    if (!space_ptr) {
        send_simple_response(client_fd, MSG_ERROR, ERR_INVALID_WORD);
        return;
    }

    int word_idx = atoi(payload);
    space_ptr++;
    while (*space_ptr == ' ' || *space_ptr == '\t') space_ptr++;

    // Trim trailing newline/carriage return
    char* endp = space_ptr + strlen(space_ptr) - 1;
    while (endp >= space_ptr && (*endp == '\n' || *endp == '\r')) {
        *endp = '\0';
        endp--;
    }

    char* new_word = strdup(space_ptr);
    if (!new_word) {
        send_simple_response(client_fd, MSG_ERROR, ERR_FILE_OPERATION_FAILED);
        return;
    }

    int result = ss_write_word(header->filename, header->sentence_index,
                               word_idx, new_word, header->username);
    free(new_word);

    // Synchronous Replication
    if (result == ERR_SUCCESS) {
        ss_forward_to_replica(header, payload, "WRITE_WORD");
    }

    send_simple_response(client_fd, 
                        (result == ERR_SUCCESS) ? MSG_ACK : MSG_ERROR, 
                        result);
}

/**
 * handle_ss_write_unlock
 * @brief Handler for OP_SS_WRITE_UNLOCK operation.
 */
void handle_ss_write_unlock(int client_fd, MessageHeader* header) {
    int result = ss_write_unlock(header->filename, header->sentence_index, header->username);
    
    // Synchronous Replication
    if (result == ERR_SUCCESS) {
        ss_forward_to_replica(header, NULL, "WRITE_UNLOCK");
    }

    send_simple_response(client_fd, 
                        (result == ERR_SUCCESS) ? MSG_ACK : MSG_ERROR, 
                        result);
}

/**
 * handle_ss_info
 * @brief Handler for OP_INFO operation - returns detailed file information.
 */
void handle_ss_info(int client_fd, MessageHeader* header) {
    long size;
    int words, chars;
    int result = ss_get_file_info(header->filename, &size, &words, &chars);

    if (result == ERR_SUCCESS) {
        // Get basic metadata
        char metapath[MAX_PATH];
        char owner[MAX_USERNAME] = "unknown";
        long created = 0;
        long modified = 0;
        
        if (ss_build_filepath(metapath, sizeof(metapath), header->filename, ".meta") == ERR_SUCCESS) {
            FILE* mf = fopen(metapath, "r");
            if (mf) {
                char line[256];
                while (fgets(line, sizeof(line), mf)) {
                    if (strncmp(line, "owner:", 6) == 0) {
                        sscanf(line, "owner:%s", owner);
                    } else if (strncmp(line, "created:", 8) == 0) {
                        sscanf(line, "created:%ld", &created);
                    } else if (strncmp(line, "modified:", 9) == 0) {
                        sscanf(line, "modified:%ld", &modified);
                    }
                }
                fclose(mf);
            }
        }
        
        // Get lock information
        char lock_info[4096];
        int active_locks = get_file_locks(header->filename, lock_info, sizeof(lock_info));
        
        // Get statistics
        char stats_info[2048];
        get_file_stats(header->filename, stats_info, sizeof(stats_info));
        
        // Format timestamps
        char created_str[64] = "Unknown";
        char modified_str[64] = "Unknown";
        
        if (created > 0) {
            time_t created_time = (time_t)created;
            strftime(created_str, sizeof(created_str), "%Y-%m-%d %H:%M:%S", 
                    localtime(&created_time));
        }
        
        if (modified > 0) {
            time_t modified_time = (time_t)modified;
            strftime(modified_str, sizeof(modified_str), "%Y-%m-%d %H:%M:%S", 
                    localtime(&modified_time));
        }
        
        // Build comprehensive info response with ANSI colors
        char info[8192];
        snprintf(info, sizeof(info),
                "%s%sFile:%s %s\n"
                "%s%sOwner:%s %s\n"
                "%s%sCreated:%s %s\n"
                "%s%sLast Modified:%s %s\n"
                "%s%sSize:%s %ld bytes\n"
                "%s%sWords:%s %d\n"
                "%s%sChars:%s %d\n"
                "\n"
                "%s%s═══ Active Locks (%d) ═══%s\n"
                "%s"
                "\n"
                "%s%s═══ Statistics ═══%s\n"
                "%s\n",
                ANSI_BOLD, ANSI_CYAN, ANSI_RESET, header->filename,
                ANSI_BOLD, ANSI_CYAN, ANSI_RESET, owner,
                ANSI_BOLD, ANSI_CYAN, ANSI_RESET, created_str,
                ANSI_BOLD, ANSI_CYAN, ANSI_RESET, modified_str,
                ANSI_BOLD, ANSI_CYAN, ANSI_RESET, size,
                ANSI_BOLD, ANSI_CYAN, ANSI_RESET, words,
                ANSI_BOLD, ANSI_CYAN, ANSI_RESET, chars,
                ANSI_BOLD, ANSI_YELLOW, active_locks, ANSI_RESET,
                lock_info,
                ANSI_BOLD, ANSI_GREEN, ANSI_RESET,
                stats_info);
        
        MessageHeader resp;
        memset(&resp, 0, sizeof(resp));
        resp.msg_type = MSG_RESPONSE;
        resp.error_code = ERR_SUCCESS;
        resp.data_length = strlen(info);
        
        send_message(client_fd, &resp, info);
    } else {
        send_simple_response(client_fd, MSG_ERROR, result);
    }
}

/**
 * handle_ss_undo
 * @brief Handler for OP_UNDO operation.
 */
void handle_ss_undo(int client_fd, MessageHeader* header) {
    int result = ss_undo_file(header->filename);

    if (result == ERR_SUCCESS) {
        ss_forward_to_replica(header, NULL, "UNDO");
    }

    send_simple_response(client_fd, 
                        (result == ERR_SUCCESS) ? MSG_ACK : MSG_ERROR, 
                        result);
}

/**
 * handle_ss_move
 * @brief Handler for OP_SS_MOVE operation.
 */
void handle_ss_move(int client_fd, MessageHeader* header, const char* payload) {
    if (!payload) {
        send_simple_response(client_fd, MSG_ERROR, ERR_FILE_OPERATION_FAILED);
        return;
    }
    
    int result = ss_move_file(header->filename, payload);

    if (result == ERR_SUCCESS) {
        ss_forward_to_replica(header, payload, "MOVE");
    }

    send_simple_response(client_fd, 
                        (result == ERR_SUCCESS) ? MSG_ACK : MSG_ERROR, 
                        result);
}

/**
 * handle_ss_checkpoint
 * @brief Handler for OP_SS_CHECKPOINT operation.
 */
void handle_ss_checkpoint(int client_fd, MessageHeader* header) {
    int result = ss_create_checkpoint(header->filename, header->checkpoint_tag);

    if (result == ERR_SUCCESS) {
        ss_forward_to_replica(header, NULL, "CHECKPOINT");
    }

    send_simple_response(client_fd, 
                        (result == ERR_SUCCESS) ? MSG_ACK : MSG_ERROR, 
                        result);
}

/**
 * handle_ss_viewcheckpoint
 * @brief Handler for OP_SS_VIEWCHECKPOINT operation.
 */
void handle_ss_viewcheckpoint(int client_fd, MessageHeader* header) {
    char* content = NULL;
    int result = ss_view_checkpoint(header->filename, header->checkpoint_tag, &content);
    
    if (result == ERR_SUCCESS && content) {
        MessageHeader response;
        memset(&response, 0, sizeof(response));
        response.msg_type = MSG_RESPONSE;
        response.error_code = ERR_SUCCESS;
        response.data_length = strlen(content);
        send_message(client_fd, &response, content);
        free(content);
    } else {
        send_simple_response(client_fd, MSG_ERROR, result);
        if (content) free(content);
    }
}

/**
 * handle_ss_revert
 * @brief Handler for OP_SS_REVERT operation.
 */
void handle_ss_revert(int client_fd, MessageHeader* header) {
    int result = ss_revert_checkpoint(header->filename, header->checkpoint_tag);

    if (result == ERR_SUCCESS) {
        ss_forward_to_replica(header, NULL, "REVERT");
    }

    send_simple_response(client_fd, 
                        (result == ERR_SUCCESS) ? MSG_ACK : MSG_ERROR, 
                        result);
}

/**
 * handle_ss_listcheckpoints
 * @brief Handler for OP_SS_LISTCHECKPOINTS operation.
 */
void handle_ss_listcheckpoints(int client_fd, MessageHeader* header) {
    char* checkpoint_list = NULL;
    int result = ss_list_checkpoints(header->filename, &checkpoint_list);
    
    if (result == ERR_SUCCESS && checkpoint_list) {
        MessageHeader response;
        memset(&response, 0, sizeof(response));
        response.msg_type = MSG_RESPONSE;
        response.error_code = ERR_SUCCESS;
        response.data_length = strlen(checkpoint_list);
        send_message(client_fd, &response, checkpoint_list);
        free(checkpoint_list);
    } else {
        send_simple_response(client_fd, MSG_ERROR, result);
        if (checkpoint_list) free(checkpoint_list);
    }
}

/**
 * handle_client_request
 * @brief Thread entrypoint for per-client connections to the Storage Server.
 *
 * Receives requests and dispatches them to individual handler functions.
 * Maintains connection for WRITE sessions (LOCK -> WORD edits -> UNLOCK).
 *
 * @param arg Pointer to an allocated int containing the accepted socket fd.
 * @return Always returns NULL when the thread exits.
 */
void* handle_client_request(void* arg) {
    int client_fd = *(int*)arg;
    free(arg);
    
    // Get client IP and port for logging
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    char client_ip[MAX_IP] = "unknown";
    int client_port = 0;
    if (getpeername(client_fd, (struct sockaddr*)&client_addr, &addr_len) == 0) {
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        client_port = ntohs(client_addr.sin_port);
    }
    
    MessageHeader header;
    char* payload = NULL;
    int keep_alive = 1;
    
    while (keep_alive && recv_message(client_fd, &header, &payload) > 0) {
        const char* operation = "UNKNOWN";
        char details[1200];
        int result_code = ERR_SUCCESS;
        
        // Determine operation name
        switch (header.op_code) {
            case OP_SS_CREATE: operation = "CREATE"; break;
            case OP_SS_DELETE: operation = "DELETE"; break;
            case OP_SS_READ: operation = "READ"; break;
            case OP_SS_WRITE_LOCK: operation = "WRITE_LOCK"; break;
            case OP_SS_WRITE_WORD: operation = "WRITE_WORD"; break;
            case OP_SS_WRITE_UNLOCK: operation = "WRITE_UNLOCK"; break;
            case OP_STREAM: operation = "STREAM"; break;
            case OP_UNDO: operation = "UNDO"; break;
            case OP_INFO: operation = "INFO"; break;
            case OP_VIEW: operation = "VIEW"; break;
            case OP_SS_MOVE: operation = "MOVE"; break;
            case OP_SS_CHECKPOINT: operation = "CHECKPOINT"; break;
            case OP_SS_VIEWCHECKPOINT: operation = "VIEW_CHECKPOINT"; break;
            case OP_SS_REVERT: operation = "REVERT"; break;
            case OP_SS_LISTCHECKPOINTS: operation = "LIST_CHECKPOINTS"; break;
            case OP_SS_SYNC: operation = "SYNC"; break;
            case OP_SS_CHECK_MTIME: operation = "CHECK_MTIME"; break;
            case OP_EXEC: operation = "EXEC"; break;
            default: operation = "UNKNOWN"; break;
        }
        
        // Initialize default details
        if (header.filename[0]) {
            snprintf(details, sizeof(details), "file=%s", header.filename);
        } else {
            details[0] = '\0';
        }
        
        // Log request received
        log_operation("SS", "INFO", operation, header.username[0] ? header.username : "system",
                     client_ip, client_port, details, 0);
        
        switch (header.op_code) {
            case OP_SS_CREATE:
                result_code = handle_ss_create(client_fd, &header, payload);
                keep_alive = 0;
                break;
            
            case OP_SS_DELETE:
                result_code = handle_ss_delete(client_fd, &header);
                keep_alive = 0;
                break;
            
            case OP_SS_READ:
                result_code = handle_ss_read(client_fd, &header);
                keep_alive = 0;
                break;
            
            case OP_SS_SYNC:
                handle_ss_sync(client_fd, &header, payload);
                keep_alive = 0;
                break;

            case OP_SS_CHECK_MTIME: {
                time_t mtime = ss_get_file_mtime(header.filename);
                char mtime_str[32];
                snprintf(mtime_str, sizeof(mtime_str), "%ld", (long)mtime);
                
                MessageHeader resp;
                init_message_header(&resp, MSG_RESPONSE, OP_SS_CHECK_MTIME, "system");
                resp.error_code = ERR_SUCCESS;
                resp.data_length = strlen(mtime_str);
                send_message(client_fd, &resp, mtime_str);
                keep_alive = 0;
                break;
            }
            
            case OP_EXEC: {
                char* exec_content = NULL;
                int exec_result = ss_read_file(header.filename, &exec_content);
                if (exec_result == ERR_SUCCESS && exec_content) {
                    MessageHeader exec_resp;
                    memset(&exec_resp, 0, sizeof(exec_resp));
                    exec_resp.msg_type = MSG_RESPONSE;
                    exec_resp.error_code = ERR_SUCCESS;
                    exec_resp.data_length = strlen(exec_content);
                    send_message(client_fd, &exec_resp, exec_content);
                    free(exec_content);
                } else {
                    send_simple_response(client_fd, MSG_ERROR, exec_result);
                }
                keep_alive = 0;
                break;
            }
            
            case OP_SS_WRITE_LOCK:
                handle_ss_write_lock(client_fd, &header);
                break;
            
            case OP_SS_WRITE_WORD:
                handle_ss_write_word(client_fd, &header, payload);
                break;
            
            case OP_SS_WRITE_UNLOCK:
                handle_ss_write_unlock(client_fd, &header);
                keep_alive = 0;
                break;
            
            case OP_STREAM:
                result_code = ss_stream_file(client_fd, header.filename);
                keep_alive = 0;
                break;
            
            case OP_UNDO:
                handle_ss_undo(client_fd, &header);
                keep_alive = 0;
                break;
            
            case OP_INFO:
                handle_ss_info(client_fd, &header);
                keep_alive = 0;
                break;
            
            case OP_SS_MOVE:
                handle_ss_move(client_fd, &header, payload);
                keep_alive = 0;
                break;
            
            case OP_SS_CHECKPOINT:
                handle_ss_checkpoint(client_fd, &header);
                keep_alive = 0;
                break;
            
            case OP_SS_VIEWCHECKPOINT:
                handle_ss_viewcheckpoint(client_fd, &header);
                keep_alive = 0;
                break;
            
            case OP_SS_REVERT:
                handle_ss_revert(client_fd, &header);
                keep_alive = 0;
                break;
            
            case OP_SS_LISTCHECKPOINTS:
                handle_ss_listcheckpoints(client_fd, &header);
                keep_alive = 0;
                break;
            
            default:
                result_code = ERR_INVALID_COMMAND;
                snprintf(details, sizeof(details), "Invalid operation code");
                keep_alive = 0;
                break;
        }
        
        // Log the completed operation
        log_operation("SS", result_code == ERR_SUCCESS ? "INFO" : "ERROR",
                     operation, header.username[0] ? header.username : "system",
                     client_ip, client_port, details, result_code);
        
        if (payload) {
            free(payload);
            payload = NULL;
        }
    }
    
    // Log client disconnection
    char disconnect_msg[512];
    snprintf(disconnect_msg, sizeof(disconnect_msg), 
             "Client connection closed from %s:%d", client_ip, client_port);
    log_operation("SS", "INFO", "CLIENT_DISCONNECT", "system",
                 client_ip, client_port, disconnect_msg, ERR_SUCCESS);
    
    close(client_fd);
    return NULL;
}
