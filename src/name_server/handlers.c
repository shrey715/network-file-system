#include "common.h"
#include "name_server.h"

extern NameServerState ns_state;

// Handle client/SS connection
void* handle_client_connection(void* arg) {
    int client_fd = *(int*)arg;
    free(arg);
    
    MessageHeader header;
    char* payload = NULL;
    
    while (recv_message(client_fd, &header, &payload) > 0) {
        char response_buf[BUFFER_SIZE];
        char* response_payload = NULL;
        
        log_operation("NM", header.username, 
                     header.op_code == OP_VIEW ? "VIEW" :
                     header.op_code == OP_READ ? "READ" :
                     header.op_code == OP_CREATE ? "CREATE" :
                     header.op_code == OP_WRITE ? "WRITE" :
                     header.op_code == OP_DELETE ? "DELETE" : "OTHER",
                     header.filename, 0);
        
        switch (header.op_code) {
            case OP_REGISTER_SS: {
                // Parse: "server_id nm_port client_port"
                int server_id, nm_port, client_port;
                sscanf(payload, "%d %d %d", &server_id, &nm_port, &client_port);
                
                // Get client IP
                struct sockaddr_in addr;
                socklen_t addr_len = sizeof(addr);
                getpeername(client_fd, (struct sockaddr*)&addr, &addr_len);
                char ip[MAX_IP];
                inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
                
                int result = nm_register_storage_server(server_id, ip, nm_port, client_port);
                
                header.msg_type = (result == ERR_SUCCESS) ? MSG_ACK : MSG_ERROR;
                header.error_code = result;
                header.data_length = 0;
                send_message(client_fd, &header, NULL);
                break;
            }
            
            case OP_CONNECT_CLIENT: {
                // Register client - payload contains username
                pthread_mutex_lock(&ns_state.lock);
                if (ns_state.client_count < MAX_CLIENTS) {
                    ClientInfo* client = &ns_state.clients[ns_state.client_count];
                    strcpy(client->username, payload);
                    
                    struct sockaddr_in addr;
                    socklen_t addr_len = sizeof(addr);
                    getpeername(client_fd, (struct sockaddr*)&addr, &addr_len);
                    inet_ntop(AF_INET, &addr.sin_addr, client->ip, sizeof(client->ip));
                    
                    client->is_connected = 1;
                    client->last_activity = time(NULL);
                    ns_state.client_count++;
                }
                pthread_mutex_unlock(&ns_state.lock);
                
                header.msg_type = MSG_ACK;
                header.error_code = ERR_SUCCESS;
                header.data_length = 0;
                send_message(client_fd, &header, NULL);
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
                    
                    if (!show_all && !has_access) continue;
                    
                    if (show_details) {
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
                
                header.msg_type = MSG_RESPONSE;
                header.error_code = ERR_SUCCESS;
                header.data_length = strlen(response_buf);
                send_message(client_fd, &header, response_buf);
                break;
            }
            
            case OP_LIST: {
                // List all users
                response_buf[0] = '\0';
                pthread_mutex_lock(&ns_state.lock);
                for (int i = 0; i < ns_state.client_count; i++) {
                    strcat(response_buf, ns_state.clients[i].username);
                    strcat(response_buf, "\n");
                }
                pthread_mutex_unlock(&ns_state.lock);
                
                header.msg_type = MSG_RESPONSE;
                header.error_code = ERR_SUCCESS;
                header.data_length = strlen(response_buf);
                send_message(client_fd, &header, response_buf);
                break;
            }
            
            case OP_CREATE: {
                // Create file - select SS and forward request
                int ss_id = nm_select_storage_server();
                if (ss_id < 0) {
                    header.msg_type = MSG_ERROR;
                    header.error_code = ERR_SS_UNAVAILABLE;
                    header.data_length = 0;
                    send_message(client_fd, &header, NULL);
                    break;
                }
                
                StorageServerInfo* ss = nm_find_storage_server(ss_id);
                if (!ss) {
                    header.msg_type = MSG_ERROR;
                    header.error_code = ERR_SS_UNAVAILABLE;
                    header.data_length = 0;
                    send_message(client_fd, &header, NULL);
                    break;
                }
                
                // Connect to SS and forward create request
                int ss_socket = connect_to_server(ss->ip, ss->client_port);
                if (ss_socket < 0) {
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
                
                if (ss_header.msg_type == MSG_ACK) {
                    // Register file in NM
                    nm_register_file(header.filename, header.username, ss_id);
                }
                
                send_message(client_fd, &ss_header, ss_response);
                if (ss_response) free(ss_response);
                break;
            }
            
            case OP_DELETE: {
                // Check ownership
                FileMetadata* file = nm_find_file(header.filename);
                if (!file) {
                    header.msg_type = MSG_ERROR;
                    header.error_code = ERR_FILE_NOT_FOUND;
                    header.data_length = 0;
                    send_message(client_fd, &header, NULL);
                    break;
                }
                
                if (strcmp(file->owner, header.username) != 0) {
                    header.msg_type = MSG_ERROR;
                    header.error_code = ERR_NOT_OWNER;
                    header.data_length = 0;
                    send_message(client_fd, &header, NULL);
                    break;
                }
                
                // Forward to SS
                StorageServerInfo* ss = nm_find_storage_server(file->ss_id);
                if (!ss) {
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
                
                MessageHeader ss_header = header;
                ss_header.op_code = OP_SS_DELETE;
                send_message(ss_socket, &ss_header, NULL);
                
                char* ss_response;
                recv_message(ss_socket, &ss_header, &ss_response);
                close(ss_socket);
                
                if (ss_header.msg_type == MSG_ACK) {
                    nm_delete_file(header.filename);
                }
                
                send_message(client_fd, &ss_header, ss_response);
                if (ss_response) free(ss_response);
                break;
            }
            
            case OP_READ:
            case OP_WRITE:
            case OP_STREAM:
            case OP_UNDO: {
                // Return SS information for direct connection
                FileMetadata* file = nm_find_file(header.filename);
                if (!file) {
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
                    header.msg_type = MSG_ERROR;
                    header.error_code = perm_result;
                    header.data_length = 0;
                    send_message(client_fd, &header, NULL);
                    break;
                }
                
                StorageServerInfo* ss = nm_find_storage_server(file->ss_id);
                if (!ss) {
                    header.msg_type = MSG_ERROR;
                    header.error_code = ERR_SS_UNAVAILABLE;
                    header.data_length = 0;
                    send_message(client_fd, &header, NULL);
                    break;
                }
                
                // Send SS info to client
                snprintf(response_buf, sizeof(response_buf), "%s:%d", ss->ip, ss->client_port);
                header.msg_type = MSG_RESPONSE;
                header.error_code = ERR_SUCCESS;
                header.data_length = strlen(response_buf);
                send_message(client_fd, &header, response_buf);
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
                
                // Format info response
                char time_str[64];
                strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S",
                        localtime(&file->created_time));
                snprintf(response_buf, sizeof(response_buf),
                        "File: %s\nOwner: %s\nCreated: %s\n",
                        file->filename, file->owner, time_str);
                
                strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S",
                        localtime(&file->last_modified));
                char temp[256];
                snprintf(temp, sizeof(temp), "Last Modified: %s\n", time_str);
                strcat(response_buf, temp);
                
                snprintf(temp, sizeof(temp), "Size: %ld bytes\nWords: %d\nChars: %d\n",
                        file->file_size, file->word_count, file->char_count);
                strcat(response_buf, temp);
                
                strcat(response_buf, "Access:\n");
                for (int i = 0; i < file->acl_count; i++) {
                    snprintf(temp, sizeof(temp), "  %s (%s%s)\n",
                            file->acl[i].username,
                            file->acl[i].read_permission ? "R" : "",
                            file->acl[i].write_permission ? "W" : "");
                    strcat(response_buf, temp);
                }
                
                header.msg_type = MSG_RESPONSE;
                header.error_code = ERR_SUCCESS;
                header.data_length = strlen(response_buf);
                send_message(client_fd, &header, response_buf);
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
            
            case OP_EXEC: {
                // Execute file - fetch content from SS and execute
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
                if (!ss) {
                    header.msg_type = MSG_ERROR;
                    header.error_code = ERR_SS_UNAVAILABLE;
                    header.data_length = 0;
                    send_message(client_fd, &header, NULL);
                    break;
                }
                
                // Fetch file content from SS
                int ss_socket = connect_to_server(ss->ip, ss->client_port);
                if (ss_socket < 0) {
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
                    header.msg_type = MSG_ERROR;
                    header.error_code = ERR_FILE_OPERATION_FAILED;
                    header.data_length = 0;
                    send_message(client_fd, &header, NULL);
                    if (file_content) free(file_content);
                    break;
                }
                
                // Execute commands and capture output
                FILE* pipe = popen(file_content, "r");
                if (!pipe) {
                    header.msg_type = MSG_ERROR;
                    header.error_code = ERR_FILE_OPERATION_FAILED;
                    header.data_length = 0;
                    send_message(client_fd, &header, NULL);
                    free(file_content);
                    break;
                }
                
                char output[BUFFER_SIZE * 4];
                size_t total = 0;
                while (fgets(output + total, sizeof(output) - total, pipe) != NULL) {
                    total = strlen(output);
                }
                pclose(pipe);
                
                header.msg_type = MSG_RESPONSE;
                header.error_code = ERR_SUCCESS;
                header.data_length = strlen(output);
                send_message(client_fd, &header, output);
                
                free(file_content);
                break;
            }
            
            default:
                header.msg_type = MSG_ERROR;
                header.error_code = ERR_INVALID_COMMAND;
                header.data_length = 0;
                send_message(client_fd, &header, NULL);
                break;
        }
        
        if (payload) {
            free(payload);
            payload = NULL;
        }
    }
    
    close(client_fd);
    return NULL;
}

// Handle storage server connection (for ongoing communication)
void* handle_ss_connection(void* arg) {
    int ss_fd = *(int*)arg;
    free(arg);
    
    // Handle heartbeats and other SS messages
    MessageHeader header;
    char* payload = NULL;
    
    while (recv_message(ss_fd, &header, &payload) > 0) {
        if (header.op_code == OP_HEARTBEAT) {
            // Update last heartbeat time
            // Implementation depends on how you track SS connections
            header.msg_type = MSG_ACK;
            send_message(ss_fd, &header, NULL);
        }
        
        if (payload) {
            free(payload);
            payload = NULL;
        }
    }
    
    close(ss_fd);
    return NULL;
}
