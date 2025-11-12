#include "common.h"
#include "name_server.h"

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
    
    MessageHeader header;
    char* payload = NULL;
    
    while (recv_message(client_fd, &header, &payload) > 0) {
        char response_buf[BUFFER_SIZE];
        
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
                    // Register file in NM (with folder path from header)
                    nm_register_file(header.filename, header.foldername, header.username, ss_id);
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
                            // Parse: "Size:123 Words:45 Chars:67"
                            long size = 0;
                            int words = 0, chars = 0;
                            if (sscanf(ss_response, "Size:%ld Words:%d Chars:%d", 
                                    &size, &words, &chars) == 3) {
                                // Update cached metadata in NM
                                pthread_mutex_lock(&ns_state.lock);
                                file->file_size = size;
                                file->word_count = words;
                                file->char_count = chars;
                                file->last_accessed = time(NULL);
                                pthread_mutex_unlock(&ns_state.lock);
                                save_state();
                            }
                            free(ss_response);
                        }
                    }
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
            
            case OP_CREATEFOLDER: {
                // Create a new folder
                int result = nm_create_folder(header.foldername, header.username);
                
                header.msg_type = (result == ERR_SUCCESS) ? MSG_ACK : MSG_ERROR;
                header.error_code = result;
                header.data_length = 0;
                send_message(client_fd, &header, NULL);
                
                if (result == ERR_SUCCESS) {
                    char msg[BUFFER_SIZE];
                    snprintf(msg, sizeof(msg), "User %s created folder: %s", 
                             header.username, header.foldername);
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
                    snprintf(msg, sizeof(msg), "User %s moved file %s to folder %s", 
                             header.username, header.filename, header.foldername);
                    log_message("NM", "INFO", msg);
                }
                break;
            }
            
            case OP_VIEWFOLDER: {
                // List folder contents
                char folder_contents[BUFFER_SIZE * 2];
                int result = nm_list_folder_contents(
                    header.foldername[0] ? header.foldername : NULL,
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
                    snprintf(msg, sizeof(msg), "User %s created checkpoint [%s] for file %s", 
                             header.username, header.checkpoint_tag, header.filename);
                    log_message("NM", "INFO", msg);
                }
                
                if (ss_payload) free(ss_payload);
                close(ss_socket);
                break;
            }
            
            case OP_VIEWCHECKPOINT: {
                // View checkpoint content
                FileMetadata* file = nm_find_file(header.filename);
                if (!file) {
                    header.msg_type = MSG_ERROR;
                    header.error_code = ERR_FILE_NOT_FOUND;
                    header.data_length = 0;
                    send_message(client_fd, &header, NULL);
                    break;
                }
                
                // Check read permission
                int perm_result = nm_check_permission(header.filename, header.username, 0);
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
                ss_header.op_code = OP_SS_VIEWCHECKPOINT;
                send_message(ss_socket, &ss_header, NULL);
                
                MessageHeader ss_response;
                char* ss_payload = NULL;
                recv_message(ss_socket, &ss_response, &ss_payload);
                
                send_message(client_fd, &ss_response, ss_payload);
                
                if (ss_payload) free(ss_payload);
                close(ss_socket);
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
                    snprintf(msg, sizeof(msg), "User %s reverted file %s to checkpoint [%s]", 
                             header.username, header.filename, header.checkpoint_tag);
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
                // List all checkpoints for file
                FileMetadata* file = nm_find_file(header.filename);
                if (!file) {
                    header.msg_type = MSG_ERROR;
                    header.error_code = ERR_FILE_NOT_FOUND;
                    header.data_length = 0;
                    send_message(client_fd, &header, NULL);
                    break;
                }
                
                // Check read permission
                int perm_result = nm_check_permission(header.filename, header.username, 0);
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
                ss_header.op_code = OP_SS_LISTCHECKPOINTS;
                send_message(ss_socket, &ss_header, NULL);
                
                MessageHeader ss_response;
                char* ss_payload = NULL;
                recv_message(ss_socket, &ss_response, &ss_payload);
                
                send_message(client_fd, &ss_response, ss_payload);
                
                if (ss_payload) free(ss_payload);
                close(ss_socket);
                break;
            }
            
            case OP_REQUESTACCESS: {
                // Request access to a file
                // flags field: bit 0 = read, bit 1 = write
                int read_requested = (header.flags & 0x01) ? 1 : 0;
                int write_requested = (header.flags & 0x02) ? 1 : 0;
                
                // Default to read if no flags set
                if (!read_requested && !write_requested) {
                    read_requested = 1;
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
                    snprintf(msg, sizeof(msg), "User %s requested %s access to file %s", 
                             header.username, perm_str, header.filename);
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
                    snprintf(msg, sizeof(msg), "User %s approved access request from %s for file %s", 
                             header.username, payload, header.filename);
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
                    snprintf(msg, sizeof(msg), "User %s denied access request from %s for file %s", 
                             header.username, payload, header.filename);
                    log_message("NM", "INFO", msg);
                } else {
                    header.msg_type = MSG_ERROR;
                    header.error_code = result;
                    header.data_length = 0;
                    send_message(client_fd, &header, NULL);
                }
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

/**
 * handle_ss_connection
 * @brief Thread entry point to handle connections from a Storage Server.
 *
 * Currently handles simple heartbeat messages and acknowledges them. This
 * function can be extended to process other NM<->SS control messages.
 *
 * @param arg Pointer to an allocated int containing the accepted socket fd.
 *            The function takes ownership and frees it.
 * @return Always returns NULL when the thread exits.
 */
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
