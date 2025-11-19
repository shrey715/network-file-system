#include "common.h"
#include "storage_server.h"
#include <signal.h>

// Global configuration
SSConfig config;

// Flag for graceful shutdown
volatile sig_atomic_t server_running = 1;

/**
 * signal_handler
 * @brief Handle shutdown signals gracefully
 */
void signal_handler(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        char msg[256];
        snprintf(msg, sizeof(msg), 
                 "Storage Server #%d received shutdown signal (%d) - shutting down gracefully", 
                 config.server_id, signum);
        log_message("SS", "WARN", msg);
        server_running = 0;
    }
}

int main(int argc, char* argv[]) {
    if (argc != 5) {
        fprintf(stderr, "Usage: %s <nm_ip> <nm_port> <client_port> <server_id>\n", argv[0]);
        return 1;
    }
    
    // Parse arguments
    strcpy(config.nm_ip, argv[1]);
    config.nm_port = atoi(argv[2]);
    config.client_port = atoi(argv[3]);
    config.server_id = atoi(argv[4]);
    
    // Set up signal handlers for graceful shutdown
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Set up storage directory
    snprintf(config.storage_dir, sizeof(config.storage_dir), 
             "data/ss_%d", config.server_id);
    create_directory(config.storage_dir);
    create_directory("logs");
    
    char startup_msg[1024];
    snprintf(startup_msg, sizeof(startup_msg), 
             "Storage Server #%d STARTING\n"
             "  Storage dir: %s\n"
             "  Client Port: %d\n"
             "  Name Server: %s:%d\n"
             config.server_id, config.storage_dir, config.client_port,
             argv[1], config.nm_port);
    log_message("SS", "INFO", startup_msg);
    
    // Register with Name Server
    int nm_socket = connect_to_server(config.nm_ip, config.nm_port);
    if (nm_socket < 0) {
        char errmsg[256];
        snprintf(errmsg, sizeof(errmsg), 
                 "Failed to connect to Name Server at %s:%d", 
                 config.nm_ip, config.nm_port);
        log_message("SS", "ERROR", errmsg);
        return 1;
    }
    
    // Send registration message
    MessageHeader header;
    memset(&header, 0, sizeof(header));
    header.msg_type = MSG_REQUEST;
    header.op_code = OP_REGISTER_SS;
    
    char payload[256];
    snprintf(payload, sizeof(payload), "%d %d %d", 
             config.server_id, config.nm_port, config.client_port);
    header.data_length = strlen(payload);
    
    send_message(nm_socket, &header, payload);
    
    // Wait for ACK
    char* response;
    recv_message(nm_socket, &header, &response);
    
    char reg_details[512];
    snprintf(reg_details, sizeof(reg_details), 
             "SS_ID=%d Client_Port=%d NM_Port=%d NM=%s:%d",
             config.server_id, config.client_port, config.nm_port, 
             config.nm_ip, config.nm_port);
    
    if (header.msg_type == MSG_ACK) {
        log_operation("SS", "INFO", "SS_REGISTER", "system", 
                     config.nm_ip, config.nm_port, reg_details, ERR_SUCCESS);
    } else {
        log_operation("SS", "ERROR", "SS_REGISTER", "system",
                     config.nm_ip, config.nm_port, reg_details, 
                     header.error_code);
        close(nm_socket);
        return 1;
    }
    if (response) free(response);
    
    // Create client listener socket
    int client_socket = create_server_socket(config.client_port);
    if (client_socket < 0) {
        char errmsg[256];
        snprintf(errmsg, sizeof(errmsg), 
                 "Failed to create client socket on port %d", 
                 config.client_port);
        log_message("SS", "ERROR", errmsg);
        close(nm_socket);
        return 1;
    }
    
    char msg[128];
    snprintf(msg, sizeof(msg), "Storage Server %d listening on port %d", 
             config.server_id, config.client_port);
    log_message("SS", "INFO", msg);

    // Initialize lock registry
    init_locked_file_registry();
    
    // Accept client connections
    while (server_running) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int* client_fd = malloc(sizeof(int));
        
        // Set socket timeout to allow checking server_running flag
        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        
        *client_fd = accept(client_socket, (struct sockaddr*)&client_addr, &addr_len);
        
        if (*client_fd >= 0) {
            // Log incoming connection
            char client_ip[MAX_IP];
            inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
            int client_port = ntohs(client_addr.sin_port);
            
            char conn_details[256];
            snprintf(conn_details, sizeof(conn_details), "New connection accepted");
            log_operation("SS", "INFO", "CLIENT_CONNECT", "unknown", 
                         client_ip, client_port, conn_details, ERR_SUCCESS);
            
            pthread_t thread;
            pthread_create(&thread, NULL, handle_client_request, client_fd);
            pthread_detach(thread);
        } else {
            free(client_fd);
            // Ignore timeout errors (EAGAIN/EWOULDBLOCK) - just check server_running again
        }
    }
    
    // Graceful shutdown
    char shutdown_msg[512];
    snprintf(shutdown_msg, sizeof(shutdown_msg),
             "Storage Server #%d SHUTTING DOWN\n"
             "  Closing client socket on port %d\n"
             "  Cleaning up resources\n"
             config.server_id, config.client_port);
    log_message("SS", "INFO", shutdown_msg);
    
    close(nm_socket);
    close(client_socket);
    cleanup_locked_file_registry();
    
    char final_msg[256];
    snprintf(final_msg, sizeof(final_msg), 
             "✓ Storage Server #%d shutdown complete", config.server_id);
    log_message("SS", "INFO", final_msg);
    
    return 0;
}
