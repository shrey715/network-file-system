#include "common.h"
#include "storage_server.h"

// Global configuration
SSConfig config;

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
    
    // Set up storage directory
    snprintf(config.storage_dir, sizeof(config.storage_dir), 
             "data/ss_%d", config.server_id);
    create_directory(config.storage_dir);
    create_directory("logs");
    
    log_message("SS", "INFO", "Storage Server starting");
    
    // Register with Name Server
    int nm_socket = connect_to_server(config.nm_ip, config.nm_port);
    if (nm_socket < 0) {
        log_message("SS", "ERROR", "Failed to connect to Name Server");
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
    if (header.msg_type == MSG_ACK) {
        log_message("SS", "INFO", "Registered with Name Server");
    } else {
        log_message("SS", "ERROR", "Registration failed");
        close(nm_socket);
        return 1;
    }
    if (response) free(response);
    
    // Create client listener socket
    int client_socket = create_server_socket(config.client_port);
    if (client_socket < 0) {
        log_message("SS", "ERROR", "Failed to create client socket");
        close(nm_socket);
        return 1;
    }
    
    char msg[128];
    snprintf(msg, sizeof(msg), "Storage Server %d listening on port %d", 
             config.server_id, config.client_port);
    log_message("SS", "INFO", msg);
    
    // Accept client connections
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int* client_fd = malloc(sizeof(int));
        *client_fd = accept(client_socket, (struct sockaddr*)&client_addr, &addr_len);
        
        if (*client_fd >= 0) {
            pthread_t thread;
            pthread_create(&thread, NULL, handle_client_request, client_fd);
            pthread_detach(thread);
        } else {
            free(client_fd);
        }
    }
    
    close(nm_socket);
    close(client_socket);
    return 0;
}
