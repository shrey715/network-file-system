/*
 * sync_ops.c - Storage Server Synchronization Operations
 *
 * Handles full data synchronization between storage servers for recovery.
 * Uses modified timestamps to only transfer files where the source is newer.
 */

#include "common.h"
#include "storage_server.h"
#include <dirent.h>

/**
 * get_file_modified_time
 * @brief Read the modified timestamp from a file's .meta file
 * @return Modified timestamp, or 0 if not found
 */
static time_t get_file_modified_time(const char* filename) {
    char metapath[MAX_PATH];
    if (ss_build_filepath(metapath, sizeof(metapath), filename, ".meta") != ERR_SUCCESS) {
        return 0;
    }
    
    FILE* f = fopen(metapath, "r");
    if (!f) return 0;
    
    time_t modified = 0;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "modified:", 9) == 0) {
            sscanf(line, "modified:%ld", &modified);
            break;
        }
    }
    fclose(f);
    return modified;
}

/**
 * ss_start_recovery_sync
 * @brief (Receiver) Connect to active replica and pull files that are newer there.
 *
 * Phase 1: Send our file list with timestamps to the active replica.
 * Phase 2: Receive only files where the replica's version is newer.
 */
void ss_start_recovery_sync(const char *replica_ip, int replica_port) {
    log_message("SS", "INFO", "[RECOVERY] Starting Version-Based Sync...");
    
    int sock = connect_to_server(replica_ip, replica_port);
    if (sock < 0) {
        log_message("SS", "ERROR", "[RECOVERY] Failed to connect to Active Replica");
        return;
    }

    // Phase 1: Build our file manifest (filename:modified_timestamp)
    char manifest[BUFFER_SIZE * 4] = "";
    int manifest_len = 0;
    
    DIR *d = opendir(config.storage_dir);
    if (d) {
        struct dirent *dir;
        while ((dir = readdir(d)) != NULL) {
            if (dir->d_type == DT_REG) {
                // Skip .meta, .undo, .stats files - only sync main files
                if (strstr(dir->d_name, ".meta") || strstr(dir->d_name, ".undo") || 
                    strstr(dir->d_name, ".stats") || strstr(dir->d_name, ".checkpoint.")) {
                    continue;
                }
                
                time_t modified = get_file_modified_time(dir->d_name);
                int written = snprintf(manifest + manifest_len, sizeof(manifest) - manifest_len,
                                       "%s:%ld\n", dir->d_name, modified);
                if (written > 0) manifest_len += written;
            }
        }
        closedir(d);
    }

    // Send Sync Request with our manifest
    MessageHeader header;
    init_message_header(&header, MSG_REQUEST, OP_SS_SYNC, "system");
    header.data_length = strlen(manifest);
    
    if (send_message(sock, &header, manifest) < 0) {
        log_message("SS", "ERROR", "[RECOVERY] Failed to send Sync Request");
        close(sock);
        return;
    }

    // Phase 2: Receive Files Loop
    char* payload = NULL;
    int files_synced = 0;
    int files_skipped = 0;
    
    while (recv_message(sock, &header, &payload) > 0) {
        if (header.msg_type == MSG_ACK) {
            // End of Sync
            break;
        }
        
        if (header.msg_type == MSG_RESPONSE && header.op_code == OP_SS_SYNC) {
            if (payload) {
                // Payload format: "FILENAME\nCONTENT"
                char* newline = strchr(payload, '\n');
                if (newline) {
                    *newline = '\0';
                    char* filename = payload;
                    char* content = newline + 1;
                    
                    // Create/Overwrite file
                    char fullpath[MAX_PATH];
                    char* clean_filename = filename;
                    if (strncmp(filename, "./", 2) == 0) clean_filename += 2;
                    else if (filename[0] == '/') clean_filename += 1;
                    
                    construct_full_path(fullpath, sizeof(fullpath), config.storage_dir, clean_filename);
                    
                    if (write_file_content(fullpath, content) == ERR_SUCCESS) {
                         char msg[512];
                         snprintf(msg, sizeof(msg), "[RECOVERY] Synced file: %s", clean_filename);
                         log_message("SS", "INFO", msg);
                         files_synced++;
                    } else {
                         char msg[512];
                         snprintf(msg, sizeof(msg), "[RECOVERY] Failed to write file: %s", clean_filename);
                         log_message("SS", "ERROR", msg);
                    }
                }
            }
        } else if (header.msg_type == MSG_ERROR && header.error_code == ERR_FILE_EXISTS) {
            // File skipped (local is same or newer)
            files_skipped++;
        }
        
        if (payload) {
            free(payload);
            payload = NULL;
        }
    }
    
    if (payload) free(payload);
    close(sock);
    
    char final_msg[256];
    snprintf(final_msg, sizeof(final_msg), "[RECOVERY] Finished. Synced %d files, Skipped %d (already up-to-date).", 
             files_synced, files_skipped);
    log_message("SS", "INFO", final_msg);
}

/**
 * handle_ss_sync
 * @brief (Sender) Stream files to the recovering server, comparing timestamps.
 *
 * Receives the remote's file manifest, compares with local timestamps,
 * and only sends files where our version is strictly newer.
 */
void handle_ss_sync(int client_fd, MessageHeader *header, const char *payload) {
    log_message("SS", "INFO", "[RECOVERY] Received Sync Request. Comparing versions...");
    (void)header; // Suppress unused warning
    
    // Parse remote manifest from payload
    // Format: "filename1:timestamp1\nfilename2:timestamp2\n..."
    // Build a lookup table for remote file timestamps
    typedef struct {
        char filename[MAX_FILENAME];
        time_t modified;
    } RemoteFile;
    
    RemoteFile remote_files[MAX_FILES];
    int remote_count = 0;
    
    if (payload && strlen(payload) > 0) {
        char* manifest_copy = strdup(payload);
        if (manifest_copy) {
            char* line = strtok(manifest_copy, "\n");
            while (line && remote_count < MAX_FILES) {
                char fname[MAX_FILENAME];
                long mtime = 0;
                if (sscanf(line, "%255[^:]:%ld", fname, &mtime) == 2) {
                    strncpy(remote_files[remote_count].filename, fname, MAX_FILENAME - 1);
                    remote_files[remote_count].modified = (time_t)mtime;
                    remote_count++;
                }
                line = strtok(NULL, "\n");
            }
            free(manifest_copy);
        }
    }
    
    char manifest_msg[256];
    snprintf(manifest_msg, sizeof(manifest_msg), "[RECOVERY] Remote manifest has %d files", remote_count);
    log_message("SS", "INFO", manifest_msg);
    
    DIR *d;
    struct dirent *dir;
    d = opendir(config.storage_dir);
    if (!d) {
        send_simple_response(client_fd, MSG_ERROR, ERR_FILE_OPERATION_FAILED);
        return;
    }
    
    int sent_count = 0;
    int skipped_count = 0;
    
    while ((dir = readdir(d)) != NULL) {
        if (dir->d_type == DT_REG) {
            // Skip metadata files
            if (strstr(dir->d_name, ".meta") || strstr(dir->d_name, ".undo") || 
                strstr(dir->d_name, ".stats") || strstr(dir->d_name, ".checkpoint.")) {
                continue;
            }
            
            char fullpath[MAX_PATH];
            construct_full_path(fullpath, sizeof(fullpath), config.storage_dir, dir->d_name);
            
            // Get local file's modified time
            time_t local_mtime = get_file_modified_time(dir->d_name);
            
            // Check if remote already has this file with same or newer version
            int should_skip = 0;
            for (int i = 0; i < remote_count; i++) {
                if (strcmp(remote_files[i].filename, dir->d_name) == 0) {
                    if (remote_files[i].modified >= local_mtime) {
                        // Remote has same or newer version - skip
                        should_skip = 1;
                        char skip_msg[512];
                        snprintf(skip_msg, sizeof(skip_msg), 
                                 "[RECOVERY] Skipping '%s' (remote >= local)",
                                 dir->d_name);
                        log_message("SS", "DEBUG", skip_msg);
                    }
                    break;
                }
            }
            
            if (should_skip) {
                skipped_count++;
                continue;
            }
            
            char* content = read_file_content(fullpath);
            if (content) {
                // Construct payload: "FILENAME\nCONTENT"
                int payload_size = strlen(dir->d_name) + 1 + strlen(content) + 1;
                char* file_payload = malloc(payload_size);
                if (file_payload) {
                    snprintf(file_payload, payload_size, "%s\n%s", dir->d_name, content);
                    
                    MessageHeader resp;
                    init_message_header(&resp, MSG_RESPONSE, OP_SS_SYNC, "system");
                    resp.data_length = strlen(file_payload);
                    
                    send_message(client_fd, &resp, file_payload);
                    free(file_payload);
                    sent_count++;
                    
                    // Also sync the .meta file for this file
                    char metapath[MAX_PATH];
                    snprintf(metapath, sizeof(metapath), "%s/%s.meta", config.storage_dir, dir->d_name);
                    char* meta_content = read_file_content(metapath);
                    if (meta_content) {
                        char meta_filename[MAX_PATH];
                        snprintf(meta_filename, sizeof(meta_filename), "%s.meta", dir->d_name);
                        
                        int meta_payload_size = strlen(meta_filename) + 1 + strlen(meta_content) + 1;
                        char* meta_payload = malloc(meta_payload_size);
                        if (meta_payload) {
                            snprintf(meta_payload, meta_payload_size, "%s\n%s", meta_filename, meta_content);
                            resp.data_length = strlen(meta_payload);
                            send_message(client_fd, &resp, meta_payload);
                            free(meta_payload);
                        }
                        free(meta_content);
                    }
                }
                free(content);
            }
        }
    }
    closedir(d);
    
    // Send Done Signal
    send_simple_response(client_fd, MSG_ACK, ERR_SUCCESS);
    
    char msg[256];
    snprintf(msg, sizeof(msg), "[RECOVERY] Stream complete. Sent %d files, Skipped %d (up-to-date).", sent_count, skipped_count);
    log_message("SS", "INFO", msg);
}
