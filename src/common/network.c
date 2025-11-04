#include "common.h"

/**
 * send_message
 * @brief Send a framed message over a connected socket.
 *
 * Writes the fixed-size MessageHeader first followed by the optional payload
 * bytes specified by header->data_length. The function performs blocking
 * sends and returns 0 on success.
 *
 * @param sockfd Connected socket file descriptor.
 * @param header Pointer to an initialized MessageHeader to send.
 * @param payload Optional pointer to payload data; may be NULL when
 *                header->data_length == 0.
 * @return 0 on success, -1 on error (and errno will be set by system calls).
 */
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

/**
 * recv_message
 * @brief Receive a framed message from a connected socket.
 *
 * Reads the MessageHeader using MSG_WAITALL and then allocates a buffer for
 * the payload if header->data_length > 0. The allocated buffer will be
 * null-terminated and must be freed by the caller (or set to NULL when no
 * payload exists).
 *
 * @param sockfd Connected socket file descriptor.
 * @param header Pointer to storage for the received MessageHeader.
 * @param payload Out parameter; on success points to malloc'd buffer
 *                (caller must free) or NULL when no payload.
 * @return Number of payload bytes received (>0), 0 on orderly shutdown,
 *         or negative on error.
 */
int recv_message(int sockfd, MessageHeader* header, char** payload) {
    // Initialize payload to NULL
    if (payload) {
        *payload = NULL;
    }
    
    // Receive header
    ssize_t received = recv(sockfd, header, sizeof(MessageHeader), MSG_WAITALL);
    if (received <= 0) {
        if (received < 0) perror("recv header failed");
        return received;
    }
    
    // Receive payload if exists
    if (payload && header->data_length > 0) {
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
    } else if (payload) {
        *payload = NULL;
    }
    
    return received;
}

/**
 * create_server_socket
 * @brief Create, bind and listen on a TCP server socket for the given port.
 *
 * The created socket is configured with SO_REUSEADDR and set to listen with
 * a modest backlog. The caller is responsible for closing the returned fd.
 *
 * @param port Port number to bind the server socket to (host byte order).
 * @return Listening socket fd on success, or -1 on failure.
 */
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

/**
 * connect_to_server
 * @brief Establish a TCP connection to the specified IPv4 address and port.
 *
 * @param ip Null-terminated IPv4 address string (e.g., "127.0.0.1").
 * @param port Destination port in host byte order.
 * @return Connected socket fd on success, or -1 on failure.
 */
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
