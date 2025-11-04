#include "common.h"
#include "name_server.h"

// Global name server state
NameServerState ns_state;

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
    
    // Create directories
    create_directory("logs");
    create_directory("data");
    
    log_message("NM", "INFO", "Name Server starting");
    
    // Load persistent state
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
    
    // Accept connections
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        
        int* client_fd = malloc(sizeof(int));
        *client_fd = accept(server_socket, (struct sockaddr*)&client_addr, &addr_len);
        
        if (*client_fd >= 0) {
            char ip[MAX_IP];
            inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip));
            
            snprintf(msg, sizeof(msg), "New connection from %s", ip);
            log_message("NM", "INFO", msg);
            
            pthread_t thread;
            pthread_create(&thread, NULL, handle_client_connection, client_fd);
            pthread_detach(thread);
        } else {
            free(client_fd);
        }
    }
    
    close(server_socket);
    pthread_mutex_destroy(&ns_state.lock);
    return 0;
}
