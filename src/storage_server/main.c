#include "common.h"
#include "storage_server.h"
#include <signal.h>

// Global configuration
SSConfig config;

// Flag for graceful shutdown
volatile sig_atomic_t server_running = 1;

/**
 * send_heartbeats
 * @brief Background thread that periodically sends heartbeat messages to the Name Server
 *        to indicate the storage server is still active and responsive.
 *
 * This thread sends heartbeats at half the check interval to ensure the Name Server
 * receives updates well before the timeout period expires. The heartbeat includes
 * the server ID in the flags field for identification.
 *
 * @param arg Pointer to SSConfig containing Name Server connection details.
 * @return NULL (thread runs indefinitely until process terminates).
 */
void* send_heartbeats(void* arg) {
    SSConfig* config = (SSConfig*)arg;
    
    log_message("SS", "INFO", "Heartbeat thread started");
    
    while (server_running) {
        sleep(HEARTBEAT_CHECK_INTERVAL / 2);  // Send heartbeats twice as often as timeout check
        
        if (!server_running) break;  // Check again after sleep
        
        MessageHeader header;
        init_message_header(&header, MSG_REQUEST, OP_HEARTBEAT, "system");
        header.flags = config->server_id;  // Pass server ID in flags
        
        // Send heartbeat to NM (create new connection each time)
        int nm_socket = connect_to_server(config->nm_ip, config->nm_port);
        if (nm_socket > 0) {
            if (send_message(nm_socket, &header, NULL) == 0) {
                // Wait for ACK
                char* response = NULL;
                if (recv_message(nm_socket, &header, &response) > 0) {
                    if (header.msg_type == MSG_ACK) {
                        char msg[256];
                        snprintf(msg, sizeof(msg), "♥ Heartbeat sent to NM (SS #%d)", config->server_id);
                        log_message("SS", "DEBUG", msg);
                    }
                }
                if (response) free(response);
            }
            close(nm_socket);
        } else {
            char msg[256];
            snprintf(msg, sizeof(msg), 
                     "⚠ Failed to connect to NM for heartbeat (SS #%d)", 
                     config->server_id);
            log_message("SS", "WARN", msg);
        }
    }
    
    log_message("SS", "INFO", "Heartbeat thread stopping");
    return NULL;
}

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
             "  Name Server: %s:%d",
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
    char* response = NULL;
    int recv_result = recv_message(nm_socket, &header, &response);
    
    char reg_details[512];
    snprintf(reg_details, sizeof(reg_details), 
             "SS_ID=%d Client_Port=%d NM_Port=%d NM=%s:%d",
             config.server_id, config.client_port, config.nm_port, 
             config.nm_ip, config.nm_port);
    
    // Check if we received a response
    if (recv_result < 0) {
        log_message("SS", "ERROR", "Failed to receive registration response from Name Server");
        close(nm_socket);
        if (response) free(response);
        return 1;
    }
    
    // Check registration status
    if (header.msg_type != MSG_ACK || header.error_code != ERR_SUCCESS) {
        log_operation("SS", "ERROR", "SS_REGISTER", "system",
                     config.nm_ip, config.nm_port, reg_details, 
                     header.error_code);
        
        char err_msg[512];
        snprintf(err_msg, sizeof(err_msg), 
                 "✗ Registration FAILED: %s (error=%d). Storage Server shutting down.",
                 get_error_message(header.error_code), header.error_code);
        log_message("SS", "ERROR", err_msg);
        
        close(nm_socket);
        if (response) free(response);
        return 1;  // Exit cleanly on registration failure
    }
    
    // Registration successful
    log_operation("SS", "INFO", "SS_REGISTER", "system", 
                 config.nm_ip, config.nm_port, reg_details, ERR_SUCCESS);
    log_message("SS", "INFO", "✓ Successfully registered with Name Server");
    
    if (response) free(response);
    close(nm_socket);  // Close registration connection
    
    // Start heartbeat thread to keep connection alive
    pthread_t heartbeat_thread;
    if (pthread_create(&heartbeat_thread, NULL, send_heartbeats, &config) != 0) {
        log_message("SS", "ERROR", "Failed to create heartbeat thread");
        return 1;
    }
    pthread_detach(heartbeat_thread);  // Run in background
    
    char hb_msg[256];
    snprintf(hb_msg, sizeof(hb_msg), 
             "✓ Heartbeat thread started for SS #%d (interval: %d seconds)", 
             config.server_id, HEARTBEAT_CHECK_INTERVAL / 2);
    log_message("SS", "INFO", hb_msg);
    
    // Create client listener socket
    int client_socket = create_server_socket(config.client_port);
    if (client_socket < 0) {
        char errmsg[256];
        snprintf(errmsg, sizeof(errmsg), 
                 "Failed to create client socket on port %d", 
                 config.client_port);
        log_message("SS", "ERROR", errmsg);
        return 1;
    }
    
    char msg[128];
    snprintf(msg, sizeof(msg), "Storage Server %d listening on port %d", 
             config.server_id, config.client_port);
    log_message("SS", "INFO", msg);

    // Initialize lock registry
    init_locked_file_registry();
    
    // Accept client connections with periodic timeout to check server_running
    while (server_running) {
        // Use select() to check for incoming connections with timeout
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(client_socket, &readfds);
        
        struct timeval timeout;
        timeout.tv_sec = 1;  // Check server_running every 1 second
        timeout.tv_usec = 0;
        
        int select_result = select(client_socket + 1, &readfds, NULL, NULL, &timeout);
        
        if (select_result < 0) {
            if (errno == EINTR) {
                // Interrupted by signal - this is normal, just continue to check server_running
                continue;
            }
            // Other error - log and continue
            log_message("SS", "ERROR", "select() error in accept loop");
            continue;
        }
        
        if (select_result == 0) {
            // Timeout - no incoming connection, loop will re-check server_running
            continue;
        }
        
        // Have an incoming connection
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int* client_fd = malloc(sizeof(int));
        
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
        }
    }
    
    // Graceful shutdown
    char shutdown_msg[512];
    snprintf(shutdown_msg, sizeof(shutdown_msg),
             "Storage Server #%d SHUTTING DOWN\n"
             "  Closing client socket on port %d\n"
             "  Cleaning up resources",
             config.server_id, config.client_port);
    log_message("SS", "INFO", shutdown_msg);
    
    close(client_socket);
    cleanup_locked_file_registry();
    
    char final_msg[256];
    snprintf(final_msg, sizeof(final_msg), 
             "✓ Storage Server #%d shutdown complete", config.server_id);
    log_message("SS", "INFO", final_msg);
    
    return 0;
}
