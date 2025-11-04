#include "common.h"

// Send a complete message (header + payload)
int send_message(int sockfd, MessageHeader* header, const char* payload) {
    // Send header first
    ssize_t sent = send(sockfd, header, sizeof(MessageHeader), 0);
    if (sent != sizeof(MessageHeader)) {
        perror("send header failed");
        return -1;
    }
    
    // Send payload if exists
    if (header->data_length > 0 && payload != NULL) {
        sent = send(sockfd, payload, header->data_length, 0);
        if (sent != header->data_length) {
            perror("send payload failed");
            return -1;
        }
    }
    
    return 0;
}

// Receive a complete message (header + payload)
int recv_message(int sockfd, MessageHeader* header, char** payload) {
    // Receive header
    ssize_t received = recv(sockfd, header, sizeof(MessageHeader), MSG_WAITALL);
    if (received <= 0) {
        if (received < 0) perror("recv header failed");
        return received;
    }
    
    // Receive payload if exists
    if (header->data_length > 0) {
        *payload = (char*)malloc(header->data_length + 1);
        if (*payload == NULL) {
            perror("malloc failed");
            return -1;
        }
        
        received = recv(sockfd, *payload, header->data_length, MSG_WAITALL);
        if (received != header->data_length) {
            perror("recv payload failed");
            free(*payload);
            *payload = NULL;
            return -1;
        }
        
        (*payload)[header->data_length] = '\0';
    } else {
        *payload = NULL;
    }
    
    return received;
}

// Create a server socket and bind to port
int create_server_socket(int port) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket creation failed");
        return -1;
    }
    
    // Set socket options
    int opt = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt failed");
        close(sockfd);
        return -1;
    }
    
    // Bind to address
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);
    
    if (bind(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind failed");
        close(sockfd);
        return -1;
    }
    
    // Listen for connections
    if (listen(sockfd, 10) < 0) {
        perror("listen failed");
        close(sockfd);
        return -1;
    }
    
    return sockfd;
}

// Connect to a server
int connect_to_server(const char* ip, int port) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket creation failed");
        return -1;
    }
    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, ip, &server_addr.sin_addr) <= 0) {
        perror("invalid address");
        close(sockfd);
        return -1;
    }
    
    if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("connection failed");
        close(sockfd);
        return -1;
    }
    
    return sockfd;
}
