#include "common.h"
#include "name_server.h"
#include "handlers_helpers.h"

extern NameServerState ns_state;

/**
 * handle_client_connection
 * @brief Thread entry point to process messages from a connected client or
 *        storage-server registration/forwarding requests.
 *
 * This function loops receiving framed messages from the connected socket and
 * dispatches actions based on the `op_code` in the message header. It may
 * forward requests to storage servers (for create/delete) or return storage
 * server connection info for read/write operations. The function sends
 * responses back on the same socket.
 *
 * @param arg Pointer to an allocated int containing the accepted socket fd.
 *            The function takes ownership and frees it.
 * @return Always returns NULL when the thread exits.
 */
void* handle_client_connection(void* arg) {
    int client_fd = *(int*)arg;
    free(arg);
    
    // Get client IP and port for logging
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    getpeername(client_fd, (struct sockaddr*)&client_addr, &addr_len);
    char client_ip[MAX_IP];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
    int client_port = ntohs(client_addr.sin_port);
    
    MessageHeader header;
    char* payload = NULL;
    char connected_username[MAX_USERNAME] = "";  // Track connected user for cleanup
    
    while (recv_message(client_fd, &header, &payload) > 0) {
        char response_buf[BUFFER_SIZE];
        char details[1024];
        // Get operation name using helper
        const char* operation = op_name(header.op_code);
        int result_code = ERR_SUCCESS;
        
        // Initialize default details based on header content
        if (header.filename[0]) {
            snprintf(details, sizeof(details), "file=%s", header.filename);
        } else if (header.foldername[0]) {
            snprintf(details, sizeof(details), "folder=%s", header.foldername);
        } else {
            details[0] = '\0';
        }
        
        switch (header.op_code) {
            case OP_REGISTER_SS: {
                // Parse: "server_id nm_port client_port ss_ip"
                // The SS now provides its own network IP to fix the localhost bug
                int server_id, nm_port, client_port;
                char ss_provided_ip[MAX_IP] = {0};
                
                // Parse registration payload - ss_ip is optional for backward compatibility
                int parsed = sscanf(payload, "%d %d %d %63s", &server_id, &nm_port, &client_port, ss_provided_ip);
                
                char ip[MAX_IP];
                if (parsed >= 4 && ss_provided_ip[0] != '\0') {
                    // Use SS-provided IP (new behavior - fixes localhost bug)
                    strncpy(ip, ss_provided_ip, MAX_IP - 1);
                    ip[MAX_IP - 1] = '\0';
                } else {
                    // Fallback to getpeername() for backward compatibility
                    struct sockaddr_in addr;
                    socklen_t addr_len = sizeof(addr);
                    getpeername(client_fd, (struct sockaddr*)&addr, &addr_len);
                    inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
                }
                
                snprintf(details, sizeof(details), "SS_ID=%d IP=%s NM_Port=%d Client_Port=%d", 
                        server_id, ip, nm_port, client_port);
                
                // Log registration request received
                log_operation("NM", "INFO", "SS_REGISTER_REQUEST", "", ip, nm_port, details, 0);
                
                int result = nm_register_storage_server(server_id, ip, nm_port, client_port);
                result_code = result;
                
                if (result == ERR_SUCCESS) {
                    char msg[512];
                    snprintf(msg, sizeof(msg), 
                             "✓ Storage Server #%d registered | IP=%s | NM_Port=%d | Client_Port=%d", 
                             server_id, ip, nm_port, client_port);
                     log_message("NM", "INFO", msg);

                    // Check if this SS needs to Sync from an Active Replica (Stale Detection)
                    StorageServerInfo* ss = nm_find_storage_server(server_id);
                    if (ss && ss->replica_active) {
                        StorageServerInfo* replica = nm_find_storage_server(ss->replica_id);
                        if (replica && replica->is_active) {
                             char sync_payload[256];
                             snprintf(sync_payload, sizeof(sync_payload), "SYNC %s %d", replica->ip, replica->client_port);
                             
                             // Send ACK with SYNC instruction
                             header.msg_type = MSG_ACK;
                             header.error_code = ERR_SUCCESS;
                             header.data_length = strlen(sync_payload);
                             send_message(client_fd, &header, sync_payload);
                             
                             char sync_msg[512];
                             snprintf(sync_msg, sizeof(sync_msg), "[RECOVERY] Triggering SYNC for SS #%d from Replica SS #%d", server_id, replica->server_id);
                             log_message("NM", "INFO", sync_msg);
                             goto registration_done;
                        }
                    }
                }
                
                header.msg_type = (result == ERR_SUCCESS) ? MSG_ACK : MSG_ERROR;
                header.error_code = result;
                header.data_length = 0;
                send_message(client_fd, &header, NULL);

                registration_done:
                
                // Log acknowledgment sent
                log_operation("NM", result == ERR_SUCCESS ? "INFO" : "ERROR",
                             "SS_REGISTER_ACK", "", ip, nm_port, details, result);
                break;
            }
            
            case OP_CONNECT_CLIENT: {
                // Register client - payload contains username
                pthread_mutex_lock(&ns_state.lock);
                
                // Log connection request
                log_operation("NM", "INFO", "CLIENT_CONNECT_REQUEST", payload, client_ip, client_port, "Registration attempt", 0);
                
                // First, check if username is already connected (reject if so)
                int username_connected = 0;
                int existing_index = -1;
                for (int i = 0; i < ns_state.client_count; i++) {
                    if (strcmp(ns_state.clients[i].username, payload) == 0) {
                        existing_index = i;
                        if (ns_state.clients[i].is_connected) {
                            username_connected = 1;
                            break;
                        }
                    }
                }
                
                if (username_connected) {
                    pthread_mutex_unlock(&ns_state.lock);
                    result_code = ERR_USERNAME_TAKEN;
                    snprintf(details, sizeof(details), "Username '%s' already in use", payload);
                    
                    log_message("NM", "WARN", details);
                    
                    header.msg_type = MSG_ERROR;
                    header.error_code = ERR_USERNAME_TAKEN;
                    header.data_length = 0;
                    send_message(client_fd, &header, NULL);
                    break;
                }
                
                // Reuse existing disconnected entry or create new one
                ClientInfo* client = NULL;
                if (existing_index >= 0) {
                    // Reuse existing disconnected entry
                    client = &ns_state.clients[existing_index];
                    snprintf(details, sizeof(details), "✓ Client '%s' reconnected from %s:%d (reused entry)", 
                             payload, client_ip, client_port);
                } else if (ns_state.client_count < MAX_CLIENTS) {
                    // Create new entry
                    client = &ns_state.clients[ns_state.client_count];
                    strcpy(client->username, payload);
                    ns_state.client_count++;
                    snprintf(details, sizeof(details), "✓ Client '%s' registered from %s:%d", 
                             payload, client_ip, client_port);
                }
                
                if (client) {
                    strcpy(connected_username, payload);  // Track username for disconnect
                    
                    struct sockaddr_in addr;
                    socklen_t addr_len = sizeof(addr);
                    getpeername(client_fd, (struct sockaddr*)&addr, &addr_len);
                    inet_ntop(AF_INET, &addr.sin_addr, client->ip, sizeof(client->ip));
                    
                    client->is_connected = 1;
                    client->last_activity = time(NULL);
                    
                    result_code = ERR_SUCCESS;
                    log_message("NM", "INFO", details);
                } else {
                    result_code = ERR_FILE_OPERATION_FAILED;
                    snprintf(details, sizeof(details), "Failed to register client: max clients reached");
                    log_message("NM", "ERROR", details);
                }
                pthread_mutex_unlock(&ns_state.lock);
                
                if (result_code == ERR_SUCCESS) {
                    header.msg_type = MSG_ACK;
                    header.error_code = ERR_SUCCESS;
                    header.data_length = 0;
                    send_message(client_fd, &header, NULL);
                    
                    // Log successful connection
                    log_operation("NM", "INFO", "CLIENT_CONNECT_SUCCESS", payload, client_ip, client_port, "Client registered", ERR_SUCCESS);
                } else {
                    header.msg_type = MSG_ERROR;
                    header.error_code = result_code;
                    header.data_length = 0;
                    send_message(client_fd, &header, NULL);
                }
                break;
            }
            
            case OP_VIEW: {
                // List files - check flags for -a and -l
                int show_all = (header.flags & 1);  // -a flag
                int show_details = (header.flags & 2);  // -l flag
                
                response_buf[0] = '\0';
                
                pthread_mutex_lock(&ns_state.lock);
                for (int i = 0; i < ns_state.file_count; i++) {
                    FileMetadata* file = &ns_state.files[i];
                    
                    // Check permission
                    int has_access = 0;
                    for (int j = 0; j < file->acl_count; j++) {
                        if (strcmp(file->acl[j].username, header.username) == 0) {
                            has_access = 1;
                            break;
                        }
                    }
                    

                    // Filter hidden files (starting with '.') unless -a is specified
                    int is_hidden = (file->filename[0] == '.');
                    if (is_hidden && !show_all) continue;
                    
                    if (!has_access) {
                        // Normally we hide files user doesn't have access to
                        // BUT if -a is for "admin/all" it might show them
                        // Let's stick to standard behavior: only show what you have access to,
                        // unless you are admin/root (not implemented yet).
                        // Actually, previous logic was `if (!show_all && !has_access) continue;`
                        // which implies `show_all` bypasses ALC checks? That seems insecure for a client explicit flag.
                        // Let's assume standard behavior:
                        // 1. Must have access
                        // 2. If access OK, check hidden status
                        continue;
                    }
                    
                    if (show_details) {
                        // Refresh metadata from Storage Server before displaying
                        StorageServerInfo* ss = nm_find_storage_server(file->ss_id);
                        if (ss && ss->is_active) {
                            int ss_socket = connect_to_server(ss->ip, ss->client_port);
                            if (ss_socket >= 0) {
                                MessageHeader ss_header;
                                memset(&ss_header, 0, sizeof(ss_header));
                                ss_header.msg_type = MSG_REQUEST;
                                ss_header.op_code = OP_INFO;
                                strcpy(ss_header.filename, file->filename);
                                strcpy(ss_header.username, header.username);
                                ss_header.data_length = 0;
                                
                                send_message(ss_socket, &ss_header, NULL);
                                
                                char* ss_response = NULL;
                                recv_message(ss_socket, &ss_header, &ss_response);
                                close(ss_socket);
                                
                                if (ss_header.msg_type == MSG_RESPONSE && ss_response) {
                                    // Parse: "Size:123 Words:45 Chars:67"
                                    long size = 0;
                                    int words = 0, chars = 0;
                                    if (sscanf(ss_response, "Size:%ld Words:%d Chars:%d", 
                                            &size, &words, &chars) == 3) {
                                        // Update cached metadata
                                        file->file_size = size;
                                        file->word_count = words;
                                        file->char_count = chars;
                                        file->last_accessed = time(NULL);
                                    }
                                    free(ss_response);
                                }
                            }
                        }
                        
                        char line[512];
                        char time_str[32];
                        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M",
                                localtime(&file->last_accessed));
                        snprintf(line, sizeof(line), "%-20s %5d %5d %16s %s\n",
                                file->filename, file->word_count, file->char_count,
                                time_str, file->owner);
                        strcat(response_buf, line);
                    } else {
                        strcat(response_buf, file->filename);
                        strcat(response_buf, "\n");
                    }
                }
                pthread_mutex_unlock(&ns_state.lock);
                
                // Save state to persist any metadata updates
                if (show_details) {
                    save_state();
                }
                
                header.msg_type = MSG_RESPONSE;
                header.error_code = ERR_SUCCESS;
                header.data_length = strlen(response_buf);
                send_message(client_fd, &header, response_buf);
                break;
            }
            
            case OP_LIST: {
                // List all connected users only
                response_buf[0] = '\0';
                pthread_mutex_lock(&ns_state.lock);
                for (int i = 0; i < ns_state.client_count; i++) {
                    if (ns_state.clients[i].is_connected) {
                        strcat(response_buf, ns_state.clients[i].username);
                        strcat(response_buf, "\n");
                    }
                }
                pthread_mutex_unlock(&ns_state.lock);
                
                header.msg_type = MSG_RESPONSE;
                header.error_code = ERR_SUCCESS;
                header.data_length = strlen(response_buf);
                send_message(client_fd, &header, response_buf);
                break;
            }
            
            case OP_CREATE: {
                snprintf(details, sizeof(details), "file=%s folder=%s", 
                        header.filename, header.foldername[0] ? header.foldername : "/");
                
                // Log request received
                log_operation("NM", "INFO", "CREATE_REQUEST", header.username, client_ip, client_port, details, 0);
                
                // Validate filename - reject reserved extensions
                if (!is_valid_filename(header.filename)) {
                    result_code = ERR_INVALID_FILENAME;
                    log_message("NM", "ERROR", "File creation rejected: Invalid filename (reserved extension)");
                    header.msg_type = MSG_ERROR;
                    header.error_code = ERR_INVALID_FILENAME;
                    header.data_length = 0;
                    send_message(client_fd, &header, NULL);
                    break;
                }
                
                // Create file - select SS and forward request
                int ss_id = nm_select_storage_server();
                if (ss_id < 0) {
                    result_code = ERR_SS_UNAVAILABLE;
                    log_message("NM", "ERROR", "File creation failed: No storage server available");
                    header.msg_type = MSG_ERROR;
                    header.error_code = ERR_SS_UNAVAILABLE;
                    header.data_length = 0;
                    send_message(client_fd, &header, NULL);
                    break;
                }
                
                StorageServerInfo* ss = nm_find_storage_server(ss_id);
                if (!ss) {
                    result_code = ERR_SS_UNAVAILABLE;
                    header.msg_type = MSG_ERROR;
                    header.error_code = ERR_SS_UNAVAILABLE;
                    header.data_length = 0;
                    send_message(client_fd, &header, NULL);
                    break;
                }
                
                char ss_details[512];
                snprintf(ss_details, sizeof(ss_details), "Forwarding CREATE to SS #%d at %s:%d", 
                         ss_id, ss->ip, ss->client_port);
                log_message("NM", "INFO", ss_details);
                
                // Connect to SS and forward create request
                int ss_socket = connect_to_server(ss->ip, ss->client_port);
                if (ss_socket < 0) {
                    result_code = ERR_SS_UNAVAILABLE;
                    log_message("NM", "ERROR", "Failed to connect to storage server");
                    header.msg_type = MSG_ERROR;
                    header.error_code = ERR_SS_UNAVAILABLE;
                    header.data_length = 0;
                    send_message(client_fd, &header, NULL);
                    break;
                }
                
                MessageHeader ss_header = header;
                ss_header.op_code = OP_SS_CREATE;
                send_message(ss_socket, &ss_header, header.username);
                
                char* ss_response;
                recv_message(ss_socket, &ss_header, &ss_response);
                close(ss_socket);
                
                result_code = ss_header.error_code;
                if (ss_header.msg_type == MSG_ACK) {
                    // Register file in NM (with folder path from header)
                    nm_register_file(header.filename, header.foldername, header.username, ss_id);
                    char tmp[2048];
                    snprintf(tmp, sizeof(tmp), "✓ File '%s' created by '%s' on SS #%d", 
                             header.filename, header.username, ss_id);
                    log_message("NM", "INFO", tmp);
                    
                    snprintf(tmp, sizeof(tmp), "%s | SS_ID=%d", details, ss_id);
                    strncpy(details, tmp, sizeof(details) - 1);
                }
                
                send_message(client_fd, &ss_header, ss_response);
                
                // Log response sent
                log_operation("NM", result_code == ERR_SUCCESS ? "INFO" : "ERROR",
                             "CREATE_RESPONSE", header.username, client_ip, client_port, details, result_code);
                
                if (ss_response) free(ss_response);
                break;
            }
            
            case OP_DELETE: {
                snprintf(details, sizeof(details), "file=%s", header.filename);
                
                // Log request received
                log_operation("NM", "INFO", "DELETE_REQUEST", header.username, client_ip, client_port, details, 0);
                
                // Check ownership
                FileMetadata* file = nm_find_file(header.filename);
                if (!file) {
                    result_code = ERR_FILE_NOT_FOUND;
                    log_message("NM", "ERROR", "Delete failed: File not found");
                    header.msg_type = MSG_ERROR;
                    header.error_code = ERR_FILE_NOT_FOUND;
                    header.data_length = 0;
                    send_message(client_fd, &header, NULL);
                    break;
                }
                
                if (strcmp(file->owner, header.username) != 0) {
                    result_code = ERR_NOT_OWNER;
                    char msg[600];
                    snprintf(msg, sizeof(msg), "Delete denied: User '%s' not owner of '%s'", 
                             header.username, header.filename);
                    log_message("NM", "WARN", msg);
                    header.msg_type = MSG_ERROR;
                    header.error_code = ERR_NOT_OWNER;
                    header.data_length = 0;
                    send_message(client_fd, &header, NULL);
                    break;
                }
                
                // Forward to SS
                StorageServerInfo* ss = nm_find_storage_server(file->ss_id);
                if (!ss) {
                    result_code = ERR_SS_UNAVAILABLE;
                    log_message("NM", "ERROR", "Delete failed: Storage server unavailable");
                    header.msg_type = MSG_ERROR;
                    header.error_code = ERR_SS_UNAVAILABLE;
                    header.data_length = 0;
                    send_message(client_fd, &header, NULL);
                    break;
                }
                
                char ss_msg[256];
                snprintf(ss_msg, sizeof(ss_msg), "Forwarding DELETE to SS #%d at %s:%d", 
                         file->ss_id, ss->ip, ss->client_port);
                log_message("NM", "INFO", ss_msg);
                
                int ss_socket = connect_to_server(ss->ip, ss->client_port);
                if (ss_socket < 0) {
                    result_code = ERR_SS_UNAVAILABLE;
                    log_message("NM", "ERROR", "Failed to connect to storage server");
                    header.msg_type = MSG_ERROR;
                    header.error_code = ERR_SS_UNAVAILABLE;
                    header.data_length = 0;
                    send_message(client_fd, &header, NULL);
                    break;
                }
                
                MessageHeader ss_header = header;
                ss_header.op_code = OP_SS_DELETE;
                send_message(ss_socket, &ss_header, NULL);
                
                char* ss_response;
                recv_message(ss_socket, &ss_header, &ss_response);
                close(ss_socket);
                
                result_code = ss_header.error_code;
                if (ss_header.msg_type == MSG_ACK) {
                    nm_delete_file(header.filename);
                    char msg[600];
                    snprintf(msg, sizeof(msg), "✓ File '%s' deleted by '%s' from SS #%d", 
                             header.filename, header.username, file->ss_id);
                    log_message("NM", "INFO", msg);
                }
                
                send_message(client_fd, &ss_header, ss_response);
                
                // Log response sent
                log_operation("NM", result_code == ERR_SUCCESS ? "INFO" : "ERROR",
                             "DELETE_RESPONSE", header.username, client_ip, client_port, details, result_code);
                
                if (ss_response) free(ss_response);
                break;
            }
            
            case OP_READ:
            case OP_WRITE:
            case OP_STREAM:
            case OP_UNDO: {
                snprintf(details, sizeof(details), "file=%s", header.filename);
                
                // Log request received
                log_operation("NM", "INFO", operation, header.username, client_ip, client_port, details, 0);
                
                // Return SS information for direct connection
                FileMetadata* file = nm_find_file(header.filename);
                if (!file) {
                    result_code = ERR_FILE_NOT_FOUND;
                    log_message("NM", "ERROR", "Operation failed: File not found");
                    header.msg_type = MSG_ERROR;
                    header.error_code = ERR_FILE_NOT_FOUND;
                    header.data_length = 0;
                    send_message(client_fd, &header, NULL);
                    break;
                }
                
                // Check permission
                int need_write = (header.op_code == OP_WRITE || header.op_code == OP_UNDO);
                int perm_result = nm_check_permission(header.filename, header.username, need_write);
                if (perm_result != ERR_SUCCESS) {
                    result_code = perm_result;
                    char msg[600];
                    snprintf(msg, sizeof(msg), "Permission denied for '%s' on file '%s'", 
                             header.username, header.filename);
                    log_message("NM", "WARN", msg);
                    header.msg_type = MSG_ERROR;
                    header.error_code = perm_result;
                    header.data_length = 0;
                    send_message(client_fd, &header, NULL);
                    break;
                }
                
                // Find target Storage Server with Failover support
                StorageServerInfo* target_ss = NULL;
                StorageServerInfo* primary_ss = NULL;
                
                // 1. Find the Primary SS (even if inactive)
                for (int i = 0; i < ns_state.ss_count; i++) {
                    if (ns_state.storage_servers[i].server_id == file->ss_id) {
                        primary_ss = &ns_state.storage_servers[i];
                        break;
                    }
                }

                if (primary_ss && primary_ss->is_active) {
                    target_ss = primary_ss;
                } else if (primary_ss && !primary_ss->is_active) {
                    // 2. Failover: Check for active Replica
                    // Allow failover for ALL operations now that we have version-based sync
                    // When primary recovers, it will sync from replica using timestamps
                    if (primary_ss->replica_active) {
                        // Find the replica
                        for (int i = 0; i < ns_state.ss_count; i++) {
                            if (ns_state.storage_servers[i].server_id == primary_ss->replica_id &&
                                ns_state.storage_servers[i].is_active) {
                                target_ss = &ns_state.storage_servers[i];
                                
                                char alert[512];
                                snprintf(alert, sizeof(alert), "[FAILOVER] Redirecting '%s' request for '%s' to Active Replica SS #%d (Primary SS #%d is DOWN)", 
                                         op_name(header.op_code), header.filename, target_ss->server_id, primary_ss->server_id);
                                log_message("NM", "WARN", alert);
                                break;
                            }
                        }
                    }
                }

                if (!target_ss) {
                    result_code = ERR_SS_UNAVAILABLE;
                    log_message("NM", "ERROR", "Storage server unavailable (Primary and Replica both down or not found)");
                    header.msg_type = MSG_ERROR;
                    header.error_code = ERR_SS_UNAVAILABLE;
                    header.data_length = 0;
                    send_message(client_fd, &header, NULL);
                    break;
                }

                // Use target_ss for response
                StorageServerInfo* ss = target_ss;
                
                // Send SS info to client
                result_code = ERR_SUCCESS;
                char tmp[2048];
                snprintf(tmp, sizeof(tmp), "%s | SS=#%d at %s:%d", details, file->ss_id, ss->ip, ss->client_port);
                strncpy(details, tmp, sizeof(details) - 1);
                
                char msg[600];
                snprintf(msg, sizeof(msg), "Directing client '%s' to SS #%d for %s operation on '%s'", 
                         header.username, file->ss_id, operation, header.filename);
                log_message("NM", "INFO", msg);
                
                snprintf(response_buf, sizeof(response_buf), "%s:%d", ss->ip, ss->client_port);
                header.msg_type = MSG_RESPONSE;
                header.error_code = ERR_SUCCESS;
                header.data_length = strlen(response_buf);
                send_message(client_fd, &header, response_buf);
                
                // Log response sent
                log_operation("NM", "INFO", operation, header.username, client_ip, client_port, details, result_code);
                break;
            }
            
            case OP_INFO: {
                // Get file info
                FileMetadata* file = nm_find_file(header.filename);
                if (!file) {
                    header.msg_type = MSG_ERROR;
                    header.error_code = ERR_FILE_NOT_FOUND;
                    header.data_length = 0;
                    send_message(client_fd, &header, NULL);
                    break;
                }
                
                // Check permission
                int perm_result = nm_check_permission(header.filename, header.username, 0);
                if (perm_result != ERR_SUCCESS) {
                    header.msg_type = MSG_ERROR;
                    header.error_code = perm_result;
                    header.data_length = 0;
                    send_message(client_fd, &header, NULL);
                    break;
                }

                StorageServerInfo* ss = nm_find_storage_server(file->ss_id);
                if (ss && ss->is_active) {
                    // Connect to SS and request file info
                    int ss_socket = connect_to_server(ss->ip, ss->client_port);
                    if (ss_socket >= 0) {
                        MessageHeader ss_header;
                        memset(&ss_header, 0, sizeof(ss_header));
                        ss_header.msg_type = MSG_REQUEST;
                        ss_header.op_code = OP_INFO;
                        strcpy(ss_header.filename, header.filename);
                        strcpy(ss_header.username, header.username);
                        ss_header.data_length = 0;
                        
                        send_message(ss_socket, &ss_header, NULL);
                        
                        char* ss_response = NULL;
                        recv_message(ss_socket, &ss_header, &ss_response);
                        close(ss_socket);
                        
                        if (ss_header.msg_type == MSG_RESPONSE && ss_response) {
                            // Extract size/words/chars for cache update
                            long size = 0;
                            int words = 0, chars = 0;
                            
                            char* size_line = strstr(ss_response, "Size:");
                            char* words_line = strstr(ss_response, "Words:");
                            char* chars_line = strstr(ss_response, "Chars:");
                            
                            if (size_line) sscanf(size_line, "Size: %ld", &size);
                            if (words_line) sscanf(words_line, "Words: %d", &words);
                            if (chars_line) sscanf(chars_line, "Chars: %d", &chars);
                            
                            pthread_mutex_lock(&ns_state.lock);
                            file->file_size = size;
                            file->word_count = words;
                            file->char_count = chars;
                            file->last_accessed = time(NULL);
                            pthread_mutex_unlock(&ns_state.lock);
                            save_state();
                            
                            // Append ACL information as separate mini-section
                            char acl_info[2048];
                            char acl_entries[1800] = "";
                            
                            for (int i = 0; i < file->acl_count; i++) {
                                char temp[128];
                                char perms[4] = "";
                                if (file->acl[i].read_permission && file->acl[i].write_permission) {
                                    strcpy(perms, "RW");
                                } else if (file->acl[i].read_permission) {
                                    strcpy(perms, "R");
                                } else if (file->acl[i].write_permission) {
                                    strcpy(perms, "W");
                                } else {
                                    strcpy(perms, "-");
                                }
                                
                                // Use └─ for last entry, ├─ for others
                                const char* branch = (i == file->acl_count - 1) ? "└─" : "├─";
                                
                                snprintf(temp, sizeof(temp),
                                        "  %s%s%s %s%s%s (%s%s%s)\n",
                                        ANSI_MAGENTA, branch, ANSI_RESET,
                                        ANSI_BRIGHT_CYAN, file->acl[i].username, ANSI_RESET,
                                        ANSI_BRIGHT_MAGENTA, perms, ANSI_RESET);
                                strcat(acl_entries, temp);
                            }
                            
                            snprintf(acl_info, sizeof(acl_info),
                                    "%s%s═══ Access Permissions (%d) ═══%s\n"
                                    "%s",
                                    ANSI_BOLD, ANSI_MAGENTA, file->acl_count, ANSI_RESET,
                                    acl_entries);
                            
                            // Combine SS response with ACL info
                            size_t total_len = strlen(ss_response) + strlen(acl_info) + 1;
                            char* combined_response = (char*)malloc(total_len);
                            if (combined_response) {
                                strcpy(combined_response, ss_response);
                                strcat(combined_response, acl_info);
                                
                                header.msg_type = MSG_RESPONSE;
                                header.error_code = ERR_SUCCESS;
                                header.data_length = strlen(combined_response);
                                send_message(client_fd, &header, combined_response);
                                
                                free(combined_response);
                                free(ss_response);
                                break;
                            }
                            free(ss_response);
                        }
                    }
                }
                
                // Fallback - SS unavailable
                header.msg_type = MSG_ERROR;
                header.error_code = ERR_SS_UNAVAILABLE;
                header.data_length = 0;
                send_message(client_fd, &header, NULL);
                break;
            }
            
            case OP_ADDACCESS: {
                // Add access - payload: "username read write"
                FileMetadata* file = nm_find_file(header.filename);
                if (!file) {
                    header.msg_type = MSG_ERROR;
                    header.error_code = ERR_FILE_NOT_FOUND;
                    header.data_length = 0;
                    send_message(client_fd, &header, NULL);
                    break;
                }
                
                // Check ownership
                if (strcmp(file->owner, header.username) != 0) {
                    header.msg_type = MSG_ERROR;
                    header.error_code = ERR_NOT_OWNER;
                    header.data_length = 0;
                    send_message(client_fd, &header, NULL);
                    break;
                }
                
                char target_user[MAX_USERNAME];
                int read, write;
                sscanf(payload, "%s %d %d", target_user, &read, &write);
                
                int result = nm_add_access(header.filename, target_user, read, write);
                
                header.msg_type = (result == ERR_SUCCESS) ? MSG_ACK : MSG_ERROR;
                header.error_code = result;
                header.data_length = 0;
                send_message(client_fd, &header, NULL);
                break;
            }
            
            case OP_REMACCESS: {
                // Remove access
                FileMetadata* file = nm_find_file(header.filename);
                if (!file) {
                    header.msg_type = MSG_ERROR;
                    header.error_code = ERR_FILE_NOT_FOUND;
                    header.data_length = 0;
                    send_message(client_fd, &header, NULL);
                    break;
                }
                
                // Check ownership
                if (strcmp(file->owner, header.username) != 0) {
                    header.msg_type = MSG_ERROR;
                    header.error_code = ERR_NOT_OWNER;
                    header.data_length = 0;
                    send_message(client_fd, &header, NULL);
                    break;
                }
                
                int result = nm_remove_access(header.filename, payload);
                
                header.msg_type = (result == ERR_SUCCESS) ? MSG_ACK : MSG_ERROR;
                header.error_code = result;
                header.data_length = 0;
                send_message(client_fd, &header, NULL);
                break;
            }
            
            case OP_CREATEFOLDER: {
                // Create a new folder
                int result = nm_create_folder(header.foldername, header.username);
                
                header.msg_type = (result == ERR_SUCCESS) ? MSG_ACK : MSG_ERROR;
                header.error_code = result;
                header.data_length = 0;
                send_message(client_fd, &header, NULL);
                
                if (result == ERR_SUCCESS) {
                    char msg[BUFFER_SIZE];
                    snprintf(msg, sizeof(msg), "Created folder '%s'", header.foldername);
                    log_message("NM", "INFO", msg);
                }
                break;
            }
            
            case OP_MOVE: {
                // Move file to different folder
                FileMetadata* file = nm_find_file(header.filename);
                if (!file) {
                    header.msg_type = MSG_ERROR;
                    header.error_code = ERR_FILE_NOT_FOUND;
                    header.data_length = 0;
                    send_message(client_fd, &header, NULL);
                    break;
                }
                
                // Check if user has write permission on the file
                int perm_result = nm_check_permission(header.filename, header.username, 1);
                if (perm_result != ERR_SUCCESS) {
                    header.msg_type = MSG_ERROR;
                    header.error_code = perm_result;
                    header.data_length = 0;
                    send_message(client_fd, &header, NULL);
                    break;
                }
                
                // Construct the new full path for the file
                char new_fullpath[MAX_PATH];
                if (header.foldername[0]) {
                    snprintf(new_fullpath, sizeof(new_fullpath), "%s/%s", 
                             header.foldername, header.filename);
                } else {
                    strncpy(new_fullpath, header.filename, sizeof(new_fullpath) - 1);
                    new_fullpath[sizeof(new_fullpath) - 1] = '\0';
                }
                
                // First, move the file physically on the storage server
                StorageServerInfo* ss = nm_find_storage_server(file->ss_id);
                if (!ss || !ss->is_active) {
                    header.msg_type = MSG_ERROR;
                    header.error_code = ERR_SS_UNAVAILABLE;
                    header.data_length = 0;
                    send_message(client_fd, &header, NULL);
                    break;
                }
                
                int ss_socket = connect_to_server(ss->ip, ss->client_port);
                if (ss_socket < 0) {
                    header.msg_type = MSG_ERROR;
                    header.error_code = ERR_SS_UNAVAILABLE;
                    header.data_length = 0;
                    send_message(client_fd, &header, NULL);
                    break;
                }
                
                // Construct the current full path
                char old_fullpath[MAX_PATH];
                if (file->folder_path[0]) {
                    int written = snprintf(old_fullpath, sizeof(old_fullpath), "%s/%s", 
                                          file->folder_path, header.filename);
                    if (written >= (int)sizeof(old_fullpath)) {
                        header.msg_type = MSG_ERROR;
                        header.error_code = ERR_INVALID_PATH;
                        header.data_length = 0;
                        send_message(client_fd, &header, NULL);
                        close(ss_socket);
                        break;
                    }
                } else {
                    strncpy(old_fullpath, header.filename, sizeof(old_fullpath) - 1);
                    old_fullpath[sizeof(old_fullpath) - 1] = '\0';
                }
                
                MessageHeader ss_header;
                memset(&ss_header, 0, sizeof(ss_header));
                ss_header.msg_type = MSG_REQUEST;
                ss_header.op_code = OP_SS_MOVE;
                strcpy(ss_header.filename, old_fullpath);
                ss_header.data_length = strlen(new_fullpath);
                
                send_message(ss_socket, &ss_header, new_fullpath);
                
                char* ss_response;
                recv_message(ss_socket, &ss_header, &ss_response);
                close(ss_socket);
                
                if (ss_response) free(ss_response);
                
                // If storage server move succeeded, update name server metadata
                int result = ERR_FILE_OPERATION_FAILED;
                if (ss_header.msg_type == MSG_ACK) {
                    result = nm_move_file(header.filename, header.foldername);
                } else {
                    result = ss_header.error_code;
                }
                
                header.msg_type = (result == ERR_SUCCESS) ? MSG_ACK : MSG_ERROR;
                header.error_code = result;
                header.data_length = 0;
                send_message(client_fd, &header, NULL);
                
                if (result == ERR_SUCCESS) {
                    char msg[BUFFER_SIZE];
                    snprintf(msg, sizeof(msg), 
                             "Moved file '%s' to folder '%s'", 
                             header.filename, header.foldername);
                    log_message("NM", "INFO", msg);
                }
                break;
            }
            
            case OP_VIEWFOLDER: {
                // Normalize folder name by removing trailing slash
                char normalized_folder[MAX_FOLDERNAME];
                if (header.foldername[0]) {
                    strncpy(normalized_folder, header.foldername, MAX_FOLDERNAME - 1);
                    normalized_folder[MAX_FOLDERNAME - 1] = '\0';
                    
                    // Remove trailing slash
                    size_t len = strlen(normalized_folder);
                    if (len > 0 && normalized_folder[len - 1] == '/') {
                        normalized_folder[len - 1] = '\0';
                    }
                }
                
                // List folder contents
                char folder_contents[BUFFER_SIZE * 2];
                int result = nm_list_folder_contents(
                    header.foldername[0] ? normalized_folder : NULL,
                    header.username,
                    folder_contents,
                    sizeof(folder_contents)
                );
                
                if (result == ERR_SUCCESS) {
                    header.msg_type = MSG_RESPONSE;
                    header.error_code = ERR_SUCCESS;
                    header.data_length = strlen(folder_contents);
                    send_message(client_fd, &header, folder_contents);
                } else {
                    header.msg_type = MSG_ERROR;
                    header.error_code = result;
                    header.data_length = 0;
                    send_message(client_fd, &header, NULL);
                }
                break;
            }
            
            case OP_EXEC: {
                // EXEC operation - executes file content as bash script on Name Server
                snprintf(details, sizeof(details), "file=%s user=%s", header.filename, header.username);
                log_operation("NM", "INFO", "EXEC_REQUEST", header.username, client_ip, client_port, details, 0);
                
                // Find file in registry
                FileMetadata* file = nm_find_file(header.filename);
                if (!file) {
                    result_code = ERR_FILE_NOT_FOUND;
                    log_message("NM", "ERROR", "EXEC failed: File not found");
                    header.msg_type = MSG_ERROR;
                    header.error_code = ERR_FILE_NOT_FOUND;
                    header.data_length = 0;
                    send_message(client_fd, &header, NULL);
                    break;
                }
                
                // Check read permission (user needs read access to execute)
                int perm_result = nm_check_permission(header.filename, header.username, 0);
                if (perm_result != ERR_SUCCESS) {
                    result_code = perm_result;
                    char msg[600];
                    snprintf(msg, sizeof(msg), "EXEC denied: User '%s' lacks read permission on '%s'", 
                             header.username, header.filename);
                    log_message("NM", "WARN", msg);
                    header.msg_type = MSG_ERROR;
                    header.error_code = perm_result;
                    header.data_length = 0;
                    send_message(client_fd, &header, NULL);
                    break;
                }
                
                // Find storage server hosting the file
                StorageServerInfo* ss = nm_find_storage_server(file->ss_id);
                if (!ss || !ss->is_active) {
                    result_code = ERR_SS_UNAVAILABLE;
                    log_message("NM", "ERROR", "EXEC failed: Storage server unavailable");
                    header.msg_type = MSG_ERROR;
                    header.error_code = ERR_SS_UNAVAILABLE;
                    header.data_length = 0;
                    send_message(client_fd, &header, NULL);
                    break;
                }
                
                // Fetch file content from Storage Server
                char fetch_msg[512];
                snprintf(fetch_msg, sizeof(fetch_msg), "Fetching '%s' from SS #%d for execution", 
                         header.filename, file->ss_id);
                log_message("NM", "INFO", fetch_msg);
                
                int ss_socket = connect_to_server(ss->ip, ss->client_port);
                if (ss_socket < 0) {
                    result_code = ERR_SS_UNAVAILABLE;
                    log_message("NM", "ERROR", "EXEC failed: Cannot connect to storage server");
                    header.msg_type = MSG_ERROR;
                    header.error_code = ERR_SS_UNAVAILABLE;
                    header.data_length = 0;
                    send_message(client_fd, &header, NULL);
                    break;
                }
                
                MessageHeader ss_header = header;
                ss_header.op_code = OP_SS_READ;
                send_message(ss_socket, &ss_header, NULL);
                
                char* file_content;
                recv_message(ss_socket, &ss_header, &file_content);
                close(ss_socket);
                
                if (ss_header.msg_type != MSG_RESPONSE || !file_content) {
                    result_code = ERR_FILE_OPERATION_FAILED;
                    log_message("NM", "ERROR", "EXEC failed: Could not read file from storage server");
                    header.msg_type = MSG_ERROR;
                    header.error_code = ERR_FILE_OPERATION_FAILED;
                    header.data_length = 0;
                    send_message(client_fd, &header, NULL);
                    if (file_content) free(file_content);
                    break;
                }
                
                // Log script content size
                char content_msg[512];
                snprintf(content_msg, sizeof(content_msg), 
                         "⚙ Executing bash script | File: '%s' | User: '%s' | Size: %zu bytes | ON NAME SERVER", 
                         header.filename, header.username, strlen(file_content));
                log_message("NM", "INFO", content_msg);
                
                // Execute file content as bash script on Name Server
                // Security note: popen executes in shell context - assumes trusted scripts
                FILE* pipe = popen(file_content, "r");
                if (!pipe) {
                    result_code = ERR_FILE_OPERATION_FAILED;
                    char err_msg[512];
                    snprintf(err_msg, sizeof(err_msg), 
                             "EXEC failed: popen error for '%s' (errno: %d)", 
                             header.filename, errno);
                    log_message("NM", "ERROR", err_msg);
                    
                    header.msg_type = MSG_ERROR;
                    header.error_code = ERR_FILE_OPERATION_FAILED;
                    header.data_length = 0;
                    send_message(client_fd, &header, NULL);
                    free(file_content);
                    break;
                }
                
                // Capture output from script execution
                char output[BUFFER_SIZE * 4];
                memset(output, 0, sizeof(output));
                size_t total = 0;
                
                while (total < sizeof(output) - 1 && 
                       fgets(output + total, sizeof(output) - total, pipe) != NULL) {
                    total = strlen(output);
                }
                
                int exit_status = pclose(pipe);
                int exit_code = WEXITSTATUS(exit_status);
                
                // Log execution completion
                char exec_result_msg[512];
                snprintf(exec_result_msg, sizeof(exec_result_msg), 
                         "✓ EXEC completed | File: '%s' | User: '%s' | Exit code: %d | Output size: %zu bytes", 
                         header.filename, header.username, exit_code, strlen(output));
                log_message("NM", exit_code == 0 ? "INFO" : "WARN", exec_result_msg);
                
                // Send output back to client
                result_code = ERR_SUCCESS;
                header.msg_type = MSG_RESPONSE;
                header.error_code = ERR_SUCCESS;
                header.data_length = strlen(output);
                send_message(client_fd, &header, output);
                
                // Log response sent
                char response_details[1024];
                snprintf(response_details, sizeof(response_details), 
                         "file=%s exit_code=%d output_bytes=%zu", 
                         header.filename, exit_code, strlen(output));
                log_operation("NM", "INFO", "EXEC_RESPONSE", header.username, 
                             client_ip, client_port, response_details, ERR_SUCCESS);
                
                free(file_content);
                break;
            }
            
            case OP_CHECKPOINT: {
                // Create checkpoint for file
                FileMetadata* file = nm_find_file(header.filename);
                if (!file) {
                    header.msg_type = MSG_ERROR;
                    header.error_code = ERR_FILE_NOT_FOUND;
                    header.data_length = 0;
                    send_message(client_fd, &header, NULL);
                    break;
                }
                
                // Check write permission (checkpoints require write access)
                int perm_result = nm_check_permission(header.filename, header.username, 1);
                if (perm_result != ERR_SUCCESS) {
                    header.msg_type = MSG_ERROR;
                    header.error_code = perm_result;
                    header.data_length = 0;
                    send_message(client_fd, &header, NULL);
                    break;
                }
                
                // Forward to storage server
                StorageServerInfo* ss = nm_find_storage_server(file->ss_id);
                if (!ss) {
                    header.msg_type = MSG_ERROR;
                    header.error_code = ERR_SS_UNAVAILABLE;
                    header.data_length = 0;
                    send_message(client_fd, &header, NULL);
                    break;
                }
                
                int ss_socket = socket(AF_INET, SOCK_STREAM, 0);
                struct sockaddr_in ss_addr;
                ss_addr.sin_family = AF_INET;
                ss_addr.sin_port = htons(ss->client_port);
                inet_pton(AF_INET, ss->ip, &ss_addr.sin_addr);
                
                if (connect(ss_socket, (struct sockaddr*)&ss_addr, sizeof(ss_addr)) < 0) {
                    header.msg_type = MSG_ERROR;
                    header.error_code = ERR_SS_UNAVAILABLE;
                    header.data_length = 0;
                    send_message(client_fd, &header, NULL);
                    close(ss_socket);
                    break;
                }
                
                MessageHeader ss_header = header;
                ss_header.op_code = OP_SS_CHECKPOINT;
                send_message(ss_socket, &ss_header, NULL);
                
                MessageHeader ss_response;
                char* ss_payload = NULL;
                recv_message(ss_socket, &ss_response, &ss_payload);
                
                send_message(client_fd, &ss_response, ss_payload);
                
                if (ss_response.error_code == ERR_SUCCESS) {
                    char msg[BUFFER_SIZE];
                    snprintf(msg, sizeof(msg), 
                             "Created checkpoint '%s' for file '%s'", 
                             header.checkpoint_tag, header.filename);
                    log_message("NM", "INFO", msg);
                }
                
                if (ss_payload) free(ss_payload);
                close(ss_socket);
                break;
            }
            
            case OP_VIEWCHECKPOINT: {
                // View checkpoint content - uses forward helper for read operation
                result_code = forward_to_ss(client_fd, &header, OP_SS_VIEWCHECKPOINT, 0);
                break;
            }
            
            case OP_REVERT: {
                // Revert file to checkpoint
                FileMetadata* file = nm_find_file(header.filename);
                if (!file) {
                    header.msg_type = MSG_ERROR;
                    header.error_code = ERR_FILE_NOT_FOUND;
                    header.data_length = 0;
                    send_message(client_fd, &header, NULL);
                    break;
                }
                
                // Check write permission
                int perm_result = nm_check_permission(header.filename, header.username, 1);
                if (perm_result != ERR_SUCCESS) {
                    header.msg_type = MSG_ERROR;
                    header.error_code = perm_result;
                    header.data_length = 0;
                    send_message(client_fd, &header, NULL);
                    break;
                }
                
                // Forward to storage server
                StorageServerInfo* ss = nm_find_storage_server(file->ss_id);
                if (!ss) {
                    header.msg_type = MSG_ERROR;
                    header.error_code = ERR_SS_UNAVAILABLE;
                    header.data_length = 0;
                    send_message(client_fd, &header, NULL);
                    break;
                }
                
                int ss_socket = socket(AF_INET, SOCK_STREAM, 0);
                struct sockaddr_in ss_addr;
                ss_addr.sin_family = AF_INET;
                ss_addr.sin_port = htons(ss->client_port);
                inet_pton(AF_INET, ss->ip, &ss_addr.sin_addr);
                
                if (connect(ss_socket, (struct sockaddr*)&ss_addr, sizeof(ss_addr)) < 0) {
                    header.msg_type = MSG_ERROR;
                    header.error_code = ERR_SS_UNAVAILABLE;
                    header.data_length = 0;
                    send_message(client_fd, &header, NULL);
                    close(ss_socket);
                    break;
                }
                
                MessageHeader ss_header = header;
                ss_header.op_code = OP_SS_REVERT;
                send_message(ss_socket, &ss_header, NULL);
                
                MessageHeader ss_response;
                char* ss_payload = NULL;
                recv_message(ss_socket, &ss_response, &ss_payload);
                
                send_message(client_fd, &ss_response, ss_payload);
                
                if (ss_response.error_code == ERR_SUCCESS) {
                    char msg[BUFFER_SIZE];
                    snprintf(msg, sizeof(msg), 
                             "Reverted file '%s' to checkpoint '%s'", 
                             header.filename, header.checkpoint_tag);
                    log_message("NM", "INFO", msg);
                    
                    // Update file metadata (size, word count, etc.) after revert
                    file->last_accessed = time(NULL);
                    save_state();
                }
                
                if (ss_payload) free(ss_payload);
                close(ss_socket);
                break;
            }
            
            case OP_LISTCHECKPOINTS: {
                // List all checkpoints - uses forward helper for read operation
                result_code = forward_to_ss(client_fd, &header, OP_SS_LISTCHECKPOINTS, 0);
                break;
            }
            
            case OP_REQUESTACCESS: {
                // Request access to a file
                // flags field: bit 0 = read, bit 1 = write
                int read_flag = (header.flags & 0x01) ? 1 : 0;
                int write_flag = (header.flags & 0x02) ? 1 : 0;
                
                // Determine requested permissions:
                // - If -W is present (regardless of -R), request both read and write
                // - If only -R is present, request only read
                // - If no flags, default to read only
                int read_requested, write_requested;
                if (write_flag) {
                    // -W flag present: grant both read and write
                    read_requested = 1;
                    write_requested = 1;
                } else if (read_flag) {
                    // Only -R flag present: grant only read
                    read_requested = 1;
                    write_requested = 0;
                } else {
                    // No flags: default to read only
                    read_requested = 1;
                    write_requested = 0;
                }
                
                // First check what access they currently have
                FileMetadata* file = nm_find_file(header.filename);
                int current_read = 0, current_write = 0;
                if (file) {
                    // Check if owner
                    if (strcmp(file->owner, header.username) == 0) {
                        current_read = 1;
                        current_write = 1;
                    } else {
                        // Check ACL
                        for (int i = 0; i < file->acl_count; i++) {
                            if (strcmp(file->acl[i].username, header.username) == 0) {
                                current_read = file->acl[i].read_permission;
                                current_write = file->acl[i].write_permission;
                                break;
                            }
                        }
                    }
                }
                
                int result = nm_request_access(header.filename, header.username, read_requested, write_requested);
                
                if (result == ERR_SUCCESS) {
                    header.msg_type = MSG_ACK;
                    header.error_code = ERR_SUCCESS;
                    header.data_length = 0;
                    send_message(client_fd, &header, NULL);
                    
                    char perm_str[32];
                    if (read_requested && write_requested) {
                        strcpy(perm_str, "read+write");
                    } else if (write_requested) {
                        strcpy(perm_str, "write");
                    } else {
                        strcpy(perm_str, "read");
                    }
                    
                    char msg[BUFFER_SIZE];
                    snprintf(msg, sizeof(msg), 
                             "Requested %s access to file '%s'", 
                             perm_str, header.filename);
                    log_message("NM", "INFO", msg);
                } else if (result == ERR_ALREADY_HAS_ACCESS) {
                    // Send back what access they have in the flags field
                    header.msg_type = MSG_ERROR;
                    header.error_code = result;
                    header.flags = (current_read ? 0x01 : 0) | (current_write ? 0x02 : 0);
                    header.data_length = 0;
                    send_message(client_fd, &header, NULL);
                } else {
                    header.msg_type = MSG_ERROR;
                    header.error_code = result;
                    header.data_length = 0;
                    send_message(client_fd, &header, NULL);
                }
                break;
            }
            
            case OP_VIEWREQUESTS: {
                // View pending requests for a file (owner only)
                char request_list[BUFFER_SIZE * 2];
                int result = nm_view_requests(header.filename, header.username, 
                                             request_list, sizeof(request_list));
                
                if (result == ERR_SUCCESS) {
                    header.msg_type = MSG_RESPONSE;
                    header.error_code = ERR_SUCCESS;
                    header.data_length = strlen(request_list);
                    send_message(client_fd, &header, request_list);
                } else {
                    header.msg_type = MSG_ERROR;
                    header.error_code = result;
                    header.data_length = 0;
                    send_message(client_fd, &header, NULL);
                }
                break;
            }
            
            case OP_APPROVEREQUEST: {
                // Approve an access request (owner only)
                // Payload contains the username to approve
                if (!payload) {
                    header.msg_type = MSG_ERROR;
                    header.error_code = ERR_FILE_OPERATION_FAILED;
                    header.data_length = 0;
                    send_message(client_fd, &header, NULL);
                    break;
                }
                
                int result = nm_approve_request(header.filename, header.username, payload);
                
                if (result == ERR_SUCCESS) {
                    header.msg_type = MSG_ACK;
                    header.error_code = ERR_SUCCESS;
                    header.data_length = 0;
                    send_message(client_fd, &header, NULL);
                    
                    char msg[BUFFER_SIZE];
                    snprintf(msg, sizeof(msg), 
                             "Approved access request from '%s' for file '%s'", 
                             payload, header.filename);
                    log_message("NM", "INFO", msg);
                } else {
                    header.msg_type = MSG_ERROR;
                    header.error_code = result;
                    header.data_length = 0;
                    send_message(client_fd, &header, NULL);
                }
                break;
            }
            
            case OP_DENYREQUEST: {
                // Deny an access request (owner only)
                // Payload contains the username to deny
                if (!payload) {
                    header.msg_type = MSG_ERROR;
                    header.error_code = ERR_FILE_OPERATION_FAILED;
                    header.data_length = 0;
                    send_message(client_fd, &header, NULL);
                    break;
                }
                
                int result = nm_deny_request(header.filename, header.username, payload);
                
                if (result == ERR_SUCCESS) {
                    header.msg_type = MSG_ACK;
                    header.error_code = ERR_SUCCESS;
                    header.data_length = 0;
                    send_message(client_fd, &header, NULL);
                    
                    char msg[BUFFER_SIZE];
                    snprintf(msg, sizeof(msg), 
                             "Denied access request from '%s' for file '%s'", 
                             payload, header.filename);
                    log_message("NM", "INFO", msg);
                } else {
                    header.msg_type = MSG_ERROR;
                    header.error_code = result;
                    header.data_length = 0;
                    send_message(client_fd, &header, NULL);
                }
                break;
            }
            
            case OP_DISCONNECT: {
                // Mark user as disconnected
                pthread_mutex_lock(&ns_state.lock);
                for (int i = 0; i < ns_state.client_count; i++) {
                    if (strcmp(ns_state.clients[i].username, header.username) == 0) {
                        ns_state.clients[i].is_connected = 0;
                        
                        char msg[256];
                        snprintf(msg, sizeof(msg), "User '%s' disconnected", header.username);
                        log_message("NM", "INFO", msg);
                        break;
                    }
                }
                pthread_mutex_unlock(&ns_state.lock);
                
                header.msg_type = MSG_ACK;
                header.error_code = ERR_SUCCESS;
                header.data_length = 0;
                send_message(client_fd, &header, NULL);
                break;
            }
            
            case OP_HEARTBEAT: {
                // Storage server heartbeat - update last_heartbeat timestamp
                int ss_id = header.flags;  // Server ID passed in flags field
                
                char payload[256] = "";

                pthread_mutex_lock(&ns_state.lock);
                int found = 0;
                for (int i = 0; i < ns_state.ss_count; i++) {
                    if (ns_state.storage_servers[i].server_id == ss_id) {
                        // Check if server was previously inactive (Recovery Detect)
                        if (!ns_state.storage_servers[i].is_active) {
                            ns_state.storage_servers[i].is_active = 1;
                            
                            char rec_msg[256];
                            snprintf(rec_msg, sizeof(rec_msg), 
                                     "✓ Heartbeat RESUMED from Storage Server #%d | IP=%s | Sync state: ACTIVE", 
                                     ss_id, ns_state.storage_servers[i].ip);
                            log_message("NM", "INFO", rec_msg);
                        }
                        
                        ns_state.storage_servers[i].last_heartbeat = time(NULL);
                        found = 1;

                        // Check and append Replica Info if active
                        if (ns_state.storage_servers[i].replica_active) {
                             int replica_id = ns_state.storage_servers[i].replica_id;
                             for (int j = 0; j < ns_state.ss_count; j++) {
                                 if (ns_state.storage_servers[j].server_id == replica_id &&
                                     ns_state.storage_servers[j].is_active) {
                                     snprintf(payload, sizeof(payload), "REPLICA %s %d", 
                                              ns_state.storage_servers[j].ip, 
                                              ns_state.storage_servers[j].client_port);
                                     break;
                                 }
                             }
                        }
                        break;
                    }
                }
                pthread_mutex_unlock(&ns_state.lock);
                
                // Send acknowledgment with payload (if any)
                header.msg_type = MSG_ACK;
                header.error_code = found ? ERR_SUCCESS : ERR_SS_UNAVAILABLE;
                header.data_length = strlen(payload);
                send_message(client_fd, &header, payload);
                
                result_code = header.error_code;
                snprintf(details, sizeof(details), "SS_ID=%d", ss_id);
                break;
            }
            
            default:
                header.msg_type = MSG_ERROR;
                header.error_code = ERR_INVALID_COMMAND;
                header.data_length = 0;
                send_message(client_fd, &header, NULL);
                result_code = ERR_INVALID_COMMAND;
                snprintf(details, sizeof(details), "Invalid operation code");
                break;
        }
        
        // Log the completed operation (SKIP healthy heartbeats to avoid spam)
        if (header.op_code != OP_HEARTBEAT || result_code != ERR_SUCCESS) {
            log_operation("NM", result_code == ERR_SUCCESS ? "INFO" : "ERROR",
                         operation, header.username[0] ? header.username : connected_username, 
                         client_ip, client_port, details, result_code);
        }
        
        if (payload) {
            free(payload);
            payload = NULL;
        }
    }
    
    // Mark user as disconnected when connection closes
    if (connected_username[0] != '\0') {
        pthread_mutex_lock(&ns_state.lock);
        for (int i = 0; i < ns_state.client_count; i++) {
            if (strcmp(ns_state.clients[i].username, connected_username) == 0) {
                ns_state.clients[i].is_connected = 0;
                
                char msg[256];
                snprintf(msg, sizeof(msg), "User '%s' connection closed", connected_username);
                log_message("NM", "INFO", msg);
                break;
            }
        }
        pthread_mutex_unlock(&ns_state.lock);
    }
    
    close(client_fd);
    return NULL;
}

/**
 * handle_ss_connection
 * @brief Thread entry point to handle connections from a Storage Server.
 *
 * This function handles the NM-side connection to a Storage Server for control
 * messages like heartbeats. It tracks which SS is connected and logs when the
 * connection is lost, marking the SS as inactive.
 *
 * @param arg Pointer to an allocated int containing the accepted socket fd.
 *            The function takes ownership and frees it.
 * @return Always returns NULL when the thread exits.
 */
void* handle_ss_connection(void* arg) {
    int ss_fd = *(int*)arg;
    free(arg);
    
    // Get SS IP and port for tracking
    struct sockaddr_in ss_addr;
    socklen_t addr_len = sizeof(ss_addr);
    char ss_ip[MAX_IP] = "unknown";
    int ss_port = 0;
    
    if (getpeername(ss_fd, (struct sockaddr*)&ss_addr, &addr_len) == 0) {
        inet_ntop(AF_INET, &ss_addr.sin_addr, ss_ip, sizeof(ss_ip));
        ss_port = ntohs(ss_addr.sin_port);
    }
    
    // Find which SS this is by matching IP
    int ss_id = -1;
    char ss_info[256] = "";
    
    pthread_mutex_lock(&ns_state.lock);
    for (int i = 0; i < ns_state.ss_count; i++) {
        if (strcmp(ns_state.storage_servers[i].ip, ss_ip) == 0) {
            ss_id = ns_state.storage_servers[i].server_id;
            snprintf(ss_info, sizeof(ss_info), "SS #%d at %s:%d", 
                     ss_id, ss_ip, ns_state.storage_servers[i].client_port);
            break;
        }
    }
    pthread_mutex_unlock(&ns_state.lock);
    
    // Log SS connection established
    if (ss_id >= 0) {
        char msg[512];
        snprintf(msg, sizeof(msg), "Control connection established with %s", ss_info);
        log_operation("NM", "INFO", "SS_CONTROL_CONNECT", "system", 
                     ss_ip, ss_port, msg, ERR_SUCCESS);
    }
    
    // Handle heartbeats and other SS messages
    MessageHeader header;
    char* payload = NULL;
    
    while (recv_message(ss_fd, &header, &payload) > 0) {
        if (header.op_code == OP_HEARTBEAT) {
            // Update last heartbeat time
            pthread_mutex_lock(&ns_state.lock);
            for (int i = 0; i < ns_state.ss_count; i++) {
                if (ns_state.storage_servers[i].server_id == ss_id) {
                    ns_state.storage_servers[i].last_heartbeat = time(NULL);
                    
                    /* char hb_msg[256];
                    snprintf(hb_msg, sizeof(hb_msg), "Heartbeat received from SS #%d", ss_id);
                    log_operation("NM", "DEBUG", "SS_HEARTBEAT", "system",
                                 ss_ip, ss_port, hb_msg, ERR_SUCCESS); */
                    break;
                }
            }
            pthread_mutex_unlock(&ns_state.lock);
            
            header.msg_type = MSG_ACK;
            send_message(ss_fd, &header, NULL);
        }
        
        if (payload) {
            free(payload);
            payload = NULL;
        }
    }
    
    // Storage Server disconnected - log it and mark as inactive
    if (ss_id >= 0) {
        pthread_mutex_lock(&ns_state.lock);
        for (int i = 0; i < ns_state.ss_count; i++) {
            if (ns_state.storage_servers[i].server_id == ss_id) {
                int was_active = ns_state.storage_servers[i].is_active;
                ns_state.storage_servers[i].is_active = 0;
                
                if (was_active) {
                    char msg[512];
                    snprintf(msg, sizeof(msg), 
                             "✗ Storage Server #%d connection LOST | IP=%s | Client_Port=%d", 
                             ss_id, ss_ip, ns_state.storage_servers[i].client_port);
                    log_message("NM", "WARN", msg);
                    
                    // Also log detailed operation
                    char details[512];
                    snprintf(details, sizeof(details), 
                             "SS_ID=%d IP=%s Client_Port=%d | %d files affected", 
                             ss_id, ss_ip, ns_state.storage_servers[i].client_port,
                             ns_state.storage_servers[i].file_count);
                    log_operation("NM", "WARN", "SS_DISCONNECT", "system", 
                                 ss_ip, ss_port, details, ERR_SUCCESS);
                }
                break;
            }
        }
        pthread_mutex_unlock(&ns_state.lock);
    } else {
        // Unknown SS disconnected
        char msg[512];
        snprintf(msg, sizeof(msg), "Unknown Storage Server disconnected from %s:%d", 
                 ss_ip, ss_port);
        log_message("NM", "WARN", msg);
    }
    
    close(ss_fd);
    return NULL;
}
