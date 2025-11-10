#include "common.h"
#include "client.h"

ClientState client_state;

int main(int argc, char* argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <nm_ip> <nm_port>\n", argv[0]);
        return 1;
    }
    
    // Initialize client state
    strcpy(client_state.nm_ip, argv[1]);
    client_state.nm_port = atoi(argv[2]);
    client_state.is_connected = 0;
    
    // Get username
    printf("Enter username: ");
    fflush(stdout);
    if (fgets(client_state.username, sizeof(client_state.username), stdin) == NULL) {
        fprintf(stderr, "Failed to read username\n");
        return 1;
    }
    client_state.username[strcspn(client_state.username, "\n")] = 0;  // Remove newline
    
    // Connect to name server
    client_state.nm_socket = connect_to_server(client_state.nm_ip, client_state.nm_port);
    if (client_state.nm_socket < 0) {
        fprintf(stderr, "Failed to connect to Name Server at %s:%d\n", 
                client_state.nm_ip, client_state.nm_port);
        return 1;
    }
    
    // Register with name server
    MessageHeader header;
    memset(&header, 0, sizeof(header));
    header.msg_type = MSG_REQUEST;
    header.op_code = OP_CONNECT_CLIENT;
    strcpy(header.username, client_state.username);
    header.data_length = strlen(client_state.username);
    
    send_message(client_state.nm_socket, &header, client_state.username);
    
    char* response;
    recv_message(client_state.nm_socket, &header, &response);
    if (header.msg_type == MSG_ACK) {
        client_state.is_connected = 1;
        printf("Connected to Name Server as '%s'\n", client_state.username);
    } else {
        fprintf(stderr, "Failed to register with Name Server\n");
        close(client_state.nm_socket);
        return 1;
    }
    if (response) free(response);
    
    // Main command loop
    char input[BUFFER_SIZE];
    printf("\nEnter commands (type 'help' for list of commands, 'quit' to exit):\n");
    
    while (1) {
        printf("> ");
        fflush(stdout);
        
        if (fgets(input, sizeof(input), stdin) == NULL) {
            break;
        }
        
        // Remove trailing newline
        input[strcspn(input, "\n")] = 0;
        
        if (strlen(input) == 0) continue;
        
        // Parse command
        char command[64], arg1[MAX_FILENAME], arg2[MAX_USERNAME];
        int flags = 0;
        
        if (strcmp(input, "quit") == 0 || strcmp(input, "exit") == 0) {
            break;
        }
        
        if (strcmp(input, "help") == 0) {
            printf("Available commands:\n");
            printf("  VIEW [-a] [-l] [-al]    - List files\n");
            printf("  READ <filename>         - Read file content\n");
            printf("  CREATE <filename>       - Create new file\n");
            printf("  WRITE <filename> <idx>  - Write to sentence at index\n");
            printf("  UNDO <filename>         - Undo last change\n");
            printf("  INFO <filename>         - Get file information\n");
            printf("  DELETE <filename>       - Delete file\n");
            printf("  STREAM <filename>       - Stream file content\n");
            printf("  LIST                    - List all users\n");
            printf("  ADDACCESS -R <file> <user> - Add read access\n");
            printf("  ADDACCESS -W <file> <user> - Add write access\n");
            printf("  REMACCESS <file> <user>    - Remove access\n");
            printf("  EXEC <filename>         - Execute file as commands\n");
            printf("  CREATEFOLDER <name>     - Create a new folder\n");
            printf("  MOVE <file> <folder>    - Move file to folder\n");
            printf("  VIEWFOLDER [folder]     - List folder contents (root if empty)\n");
            printf("  quit/exit               - Exit client\n");
            continue;
        }
        
        int result = parse_command(input, command, arg1, arg2, &flags);
        if (result < 0) {
            printf("Error: Invalid command format\n");
            continue;
        }
        
        // Execute command
        if (strcmp(command, "VIEW") == 0) {
            execute_view(&client_state, flags);
        } else if (strcmp(command, "READ") == 0) {
            execute_read(&client_state, arg1);
        } else if (strcmp(command, "CREATE") == 0) {
            execute_create(&client_state, arg1);
        } else if (strcmp(command, "WRITE") == 0) {
            int sentence_idx = atoi(arg2);
            execute_write(&client_state, arg1, sentence_idx);
        } else if (strcmp(command, "UNDO") == 0) {
            execute_undo(&client_state, arg1);
        } else if (strcmp(command, "INFO") == 0) {
            execute_info(&client_state, arg1);
        } else if (strcmp(command, "DELETE") == 0) {
            execute_delete(&client_state, arg1);
        } else if (strcmp(command, "STREAM") == 0) {
            execute_stream(&client_state, arg1);
        } else if (strcmp(command, "LIST") == 0) {
            execute_list(&client_state);
        } else if (strcmp(command, "ADDACCESS") == 0) {
            execute_addaccess(&client_state, arg1, arg2, flags & 1, flags & 2);
        } else if (strcmp(command, "REMACCESS") == 0) {
            execute_remaccess(&client_state, arg1, arg2);
        } else if (strcmp(command, "EXEC") == 0) {
            execute_exec(&client_state, arg1);
        } else if (strcmp(command, "CREATEFOLDER") == 0) {
            execute_createfolder(&client_state, arg1);
        } else if (strcmp(command, "MOVE") == 0) {
            execute_move(&client_state, arg1, arg2);
        } else if (strcmp(command, "VIEWFOLDER") == 0) {
            // arg1 contains folder name, or empty for root
            execute_viewfolder(&client_state, arg1);
        } else {
            printf("Error: Unknown command '%s'\n", command);
        }
    }
    
    // Cleanup
    close(client_state.nm_socket);
    printf("Disconnected from Name Server\n");
    
    return 0;
}
