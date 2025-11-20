#include "common.h"
#include "name_server.h"

// Global name server state
NameServerState ns_state;

/**
 * monitor_storage_servers
 * @brief Background thread that periodically checks storage server heartbeats
 *        and marks servers as inactive if they fail to report within the timeout.
 *
 * This thread wakes up every HEARTBEAT_CHECK_INTERVAL seconds and iterates
 * through all registered storage servers. If a server is marked active but
 * hasn't sent a heartbeat within HEARTBEAT_TIMEOUT seconds, it is marked as
 * inactive and a warning is logged.
 *
 * @param arg Unused thread argument (required by pthread_create signature).
 * @return NULL (thread runs indefinitely).
 */
void* monitor_storage_servers(void* arg) {
    (void)arg;  // Suppress unused parameter warning
    
    log_message("NM", "INFO", "Storage server monitoring thread started");
    
    while (1) {
        sleep(HEARTBEAT_CHECK_INTERVAL);
        
        pthread_mutex_lock(&ns_state.lock);
        time_t now = time(NULL);
        
        for (int i = 0; i < ns_state.ss_count; i++) {
            StorageServerInfo* ss = &ns_state.storage_servers[i];
            if (ss->is_active && (now - ss->last_heartbeat > HEARTBEAT_TIMEOUT)) {
                ss->is_active = 0;
                
                char msg[512];
                snprintf(msg, sizeof(msg),
                         "✗ Storage Server #%d connection LOST (timeout) | IP=%s | Client_Port=%d | Last_Heartbeat=%ld seconds ago",
                         ss->server_id, ss->ip, ss->client_port, (long)(now - ss->last_heartbeat));
                log_message("NM", "WARN", msg);
            }
        }
        pthread_mutex_unlock(&ns_state.lock);
    }
    
    return NULL;
}

/**
 * main (Name Server)
 *
 * Start the central Name Server which listens for client and storage server
 * connections. The Name Server maintains the file registry and routes client
 * requests to appropriate storage servers.
 *
 * Usage: ./name_server <port>
 */
int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return 1;
    }
    
    int port = atoi(argv[1]);
    
    // Initialize state
    memset(&ns_state, 0, sizeof(ns_state));
    pthread_mutex_init(&ns_state.lock, NULL);
    
    // Initialize efficient search structures
    ns_state.file_trie_root = trie_create_node();
    ns_state.file_cache = cache_create(LRU_CACHE_SIZE);
    
    if (!ns_state.file_trie_root || !ns_state.file_cache) {
        log_message("NM", "ERROR", "Failed to initialize Trie and LRU cache structures");
        return 1;
    }
    
    log_message("NM", "INFO", "Initialized Trie and LRU cache for efficient file search");
    
    // Create directories
    create_directory("logs");
    create_directory("data");
    
    log_message("NM", "INFO", "Name Server starting");
    
    // Load persistent state (will rebuild Trie from loaded files)
    load_state();
    
    // Create server socket
    int server_socket = create_server_socket(port);
    if (server_socket < 0) {
        log_message("NM", "ERROR", "Failed to create server socket");
        return 1;
    }
    
    char msg[128];
    snprintf(msg, sizeof(msg), "Name Server listening on port %d", port);
    log_message("NM", "INFO", msg);
    
    // Start the storage server monitoring thread
    pthread_t monitor_thread;
    if (pthread_create(&monitor_thread, NULL, monitor_storage_servers, NULL) != 0) {
        log_message("NM", "ERROR", "Failed to create storage server monitoring thread");
        close(server_socket);
        return 1;
    }
    pthread_detach(monitor_thread);  // Run in the background
    
    // Accept connections
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        
        int* client_fd = malloc(sizeof(int));
        *client_fd = accept(server_socket, (struct sockaddr*)&client_addr, &addr_len);
        
        if (*client_fd >= 0) {
            char ip[MAX_IP];
            inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip));
            
            snprintf(msg, sizeof(msg), "New connection from %s (fd %d)", ip, *client_fd);
            log_message("NM", "INFO", msg);
            
            pthread_t thread;
            pthread_create(&thread, NULL, handle_client_connection, client_fd);
            pthread_detach(thread);
        } else {
            free(client_fd);
        }
    }
    
    close(server_socket);
    
    // Cleanup search structures
    if (ns_state.file_cache) {
        cache_print_stats(ns_state.file_cache);
        cache_free(ns_state.file_cache);
    }
    if (ns_state.file_trie_root) {
        trie_free(ns_state.file_trie_root);
    }
    
    pthread_mutex_destroy(&ns_state.lock);
    return 0;
}
