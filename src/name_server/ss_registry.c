/*
 * ss_registry.c - Storage Server Registry Management
 * 
 * Handles registration, lookup, and selection of storage servers.
 * Extracted from file_registry.c for better modularity.
 */

#include "common.h"
#include "name_server.h"

extern NameServerState ns_state;

/**
 * nm_register_storage_server
 * @brief Register or update a Storage Server's information in the Name Server.
 *
 * Prevents duplicate active server IDs and duplicate client ports by rejecting
 * registration attempts when conflicts exist. Allows re-registration of
 * inactive servers. Marks the server active and records heartbeat time.
 *
 * @param server_id Numeric identifier for the storage server.
 * @param ip Null-terminated IPv4 address string for the storage server.
 * @param nm_port Port number the storage server uses to contact NM (unused by
 *                some workflows but stored for completeness).
 * @param client_port Port number clients should use to contact the storage
 *                    server for data operations.
 * @return ERR_SUCCESS on success, ERR_SS_EXISTS if an active server with the
 *         same ID or port already exists, or ERR_FILE_OPERATION_FAILED if
 *         registry capacity is exhausted.
 */
int nm_register_storage_server(int server_id, const char* ip, int nm_port, int client_port) {
    pthread_mutex_lock(&ns_state.lock);
    
    StorageServerInfo* existing_ss = NULL;
    
    // Check for duplicate ID or port conflicts
    for (int i = 0; i < ns_state.ss_count; i++) {
        if (ns_state.storage_servers[i].server_id == server_id) {
            existing_ss = &ns_state.storage_servers[i];
        }
        
        // Check if another ACTIVE server is using this client port
        if (ns_state.storage_servers[i].is_active && 
            ns_state.storage_servers[i].client_port == client_port &&
            ns_state.storage_servers[i].server_id != server_id) {
            pthread_mutex_unlock(&ns_state.lock);
            
            char err_msg[512];
            snprintf(err_msg, sizeof(err_msg), 
                     "✗ Registration REJECTED: Client port %d is already in use by Storage Server #%d (IP=%s)",
                     client_port, ns_state.storage_servers[i].server_id, ns_state.storage_servers[i].ip);
            log_message("NM", "ERROR", err_msg);
            return ERR_SS_EXISTS;
        }
    }
    
    if (existing_ss) {
        if (existing_ss->is_active) {
            // An active server with this ID already exists. Reject the request.
            pthread_mutex_unlock(&ns_state.lock);
            char err_msg[512];
            snprintf(err_msg, sizeof(err_msg), 
                     "✗ Registration REJECTED: Storage Server ID %d is already in use (IP=%s, Port=%d)",
                     server_id, existing_ss->ip, existing_ss->client_port);
            log_message("NM", "ERROR", err_msg);
            return ERR_SS_EXISTS;
        } else {
            // The server is re-registering. Update its information.
            strcpy(existing_ss->ip, ip);
            existing_ss->nm_port = nm_port;
            existing_ss->client_port = client_port;
            existing_ss->is_active = 1;
            existing_ss->last_heartbeat = time(NULL);
            pthread_mutex_unlock(&ns_state.lock);
            
            char msg[512];
            snprintf(msg, sizeof(msg), 
                     "✓ Re-registered Storage Server #%d | IP=%s | Client_Port=%d",
                     server_id, ip, client_port);
            log_message("NM", "INFO", msg);
            return ERR_SUCCESS;
        }
    } else {
        // No server with this ID exists. Register it as a new server.
        if (ns_state.ss_count >= MAX_STORAGE_SERVERS) {
            pthread_mutex_unlock(&ns_state.lock);
            log_message("NM", "ERROR", "✗ Registration FAILED: Maximum storage server capacity reached");
            return ERR_FILE_OPERATION_FAILED;
        }
        
        StorageServerInfo* ss = &ns_state.storage_servers[ns_state.ss_count];
        ss->server_id = server_id;
        strcpy(ss->ip, ip);
        ss->nm_port = nm_port;
        ss->client_port = client_port;
        ss->is_active = 1;
        ss->last_heartbeat = time(NULL);
        ss->files = NULL;
        ss->file_count = 0;
        
        // Replica Pairing Logic
        // Strategy: Odd IDs are Primary, Even IDs (ID+1) are Replicas (and vice versa)
        int partner_id = (server_id % 2 != 0) ? (server_id + 1) : (server_id - 1);
        ss->replica_id = partner_id;
        ss->replica_active = 0; // Default until we find partner

        // Check if partner exists and link them
        for (int i = 0; i < ns_state.ss_count; i++) {
            if (ns_state.storage_servers[i].server_id == partner_id) {
                // Partner found! Link them reciprocally.
                ns_state.storage_servers[i].replica_id = server_id;
                ns_state.storage_servers[i].replica_active = 1;

                ss->replica_active = ns_state.storage_servers[i].is_active;

                char link_msg[256];
                snprintf(link_msg, sizeof(link_msg), 
                         "[LINK] Paired SS #%d with Replica SS #%d", server_id, partner_id);
                log_message("NM", "INFO", link_msg);
                break;
            }
        }
        
        ns_state.ss_count++;
        pthread_mutex_unlock(&ns_state.lock);
        
        char msg[512];
        snprintf(msg, sizeof(msg), 
                 "[NEW] Registered NEW Storage Server #%d | IP=%s | Client_Port=%d",
                 server_id, ip, client_port);
        log_message("NM", "INFO", msg);
        
        return ERR_SUCCESS;
    }
}

/**
 * nm_find_storage_server
 * @brief Return a pointer to a registered, active Storage Server by id.
 *
 * This function searches the in-memory storage server list and returns a
 * pointer to the StorageServerInfo if it exists and is active.
 *
 * @param ss_id Storage server id to look up.
 * @return Pointer to StorageServerInfo or NULL if not found/active.
 */
StorageServerInfo* nm_find_storage_server(int ss_id) {
    for (int i = 0; i < ns_state.ss_count; i++) {
        if (ns_state.storage_servers[i].server_id == ss_id && 
            ns_state.storage_servers[i].is_active) {
            return &ns_state.storage_servers[i];
        }
    }
    return NULL;
}

/**
 * nm_select_storage_server
 * @brief Choose an active storage server using a simple round-robin policy.
 *
 * Returns the server id of the selected storage server, or -1 if no active
 * storage servers are available.
 *
 * @return Storage server id >= 0 when successful, -1 on failure.
 */
int nm_select_storage_server(void) {
    static int last_selected = 0;
    
    pthread_mutex_lock(&ns_state.lock);
    
    if (ns_state.ss_count == 0) {
        pthread_mutex_unlock(&ns_state.lock);
        return -1;
    }
    
    // Find next active server
    for (int i = 0; i < ns_state.ss_count; i++) {
        int idx = (last_selected + i) % ns_state.ss_count;
        if (ns_state.storage_servers[idx].is_active) {
            last_selected = (idx + 1) % ns_state.ss_count;
            int ss_id = ns_state.storage_servers[idx].server_id;
            pthread_mutex_unlock(&ns_state.lock);
            return ss_id;
        }
    }
    
    pthread_mutex_unlock(&ns_state.lock);
    return -1;
}
