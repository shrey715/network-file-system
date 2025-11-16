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
        char command[64], subcommand[64], arg1[MAX_FILENAME], arg2[MAX_USERNAME];
        int flags = 0;
        
        if (strcmp(input, "quit") == 0 || strcmp(input, "exit") == 0) {
            break;
        }
        
        if (strcmp(input, "help") == 0) {
            printf("\nAvailable commands:\n");
            printf("\nFile System:\n");
            printf("  file create <filename>         - Create new file\n");
            printf("  file delete <filename>         - Delete file\n");
            printf("  file read <filename>           - Read file content\n");
            printf("  file info <filename>           - Get file information\n");
            printf("  file list [-a] [-l]            - List files\n");
            printf("  file move <file> <folder>      - Move file to folder\n");
            printf("  file stream <filename>         - Stream file content\n");
            printf("  file exec <filename>           - Execute file as commands\n");
            
            printf("\nEdit System:\n");
            printf("  edit <filename> <idx>        - Edit sentence at index\n");
            printf("  edit undo <filename>         - Undo last change\n");
            
            printf("\nFolder System:\n");
            printf("  folder create <name>         - Create new folder\n");
            printf("  folder view [path]           - List folder contents\n");
            
            printf("\nVersion Control:\n");
            printf("  version create <file> <tag>  - Create checkpoint\n");
            printf("  version view <file> <tag>    - View checkpoint content\n");
            printf("  version revert <file> <tag>  - Revert to checkpoint\n");
            printf("  version list <file>          - List all checkpoints\n");
            
            printf("\nAccess Control:\n");
            printf("  access grant <file> <user> [-R|-W]   - Grant access\n");
            printf("  access revoke <file> <user>          - Revoke access\n");
            printf("  access request <file> [-R] [-W]      - Request access\n");
            printf("  access requests <file>               - View requests (owner)\n");
            printf("  access approve <file> <user>         - Approve request (owner)\n");
            printf("  access deny <file> <user>            - Deny request (owner)\n");
            
            printf("\nUser System:\n");
            printf("  user list                    - List all users\n");
            printf("\nquit/exit - Exit client\n\n");
            continue;
        }
        
        int result = parse_command(input, command, subcommand, arg1, arg2, &flags);
        if (result < 0) {
            printf("Error: Invalid command format\n");
            continue;
        }
        
        // Execute commands
        if (strcmp(command, "file") == 0) {
            if (strcmp(subcommand, "create") == 0) {
                execute_create(&client_state, arg1);
            } else if (strcmp(subcommand, "delete") == 0) {
                execute_delete(&client_state, arg1);
            } else if (strcmp(subcommand, "read") == 0) {
                execute_read(&client_state, arg1);
            } else if (strcmp(subcommand, "info") == 0) {
                execute_info(&client_state, arg1);
            } else if (strcmp(subcommand, "list") == 0) {
                execute_view(&client_state, flags);
            } else if (strcmp(subcommand, "move") == 0) {
                execute_move(&client_state, arg1, arg2);
            } else if (strcmp(subcommand, "stream") == 0) {
                execute_stream(&client_state, arg1);
            } else if (strcmp(subcommand, "exec") == 0) {
                execute_exec(&client_state, arg1);
            } else {
                printf("Error: Unknown file subcommand '%s'\n", subcommand);
            }
        }
        else if (strcmp(command, "edit") == 0) {
            if (strcmp(subcommand, "undo") == 0) {
                execute_undo(&client_state, arg1);
            } else {
                // Subcommand is actually the filename, arg1 is the index
                int sentence_idx = atoi(arg1);
                execute_write(&client_state, subcommand, sentence_idx);
            }
        }
        else if (strcmp(command, "folder") == 0) {
            if (strcmp(subcommand, "create") == 0) {
                execute_createfolder(&client_state, arg1);
            } else if (strcmp(subcommand, "view") == 0) {
                execute_viewfolder(&client_state, arg1);
            } else {
                printf("Error: Unknown folder subcommand '%s'\n", subcommand);
            }
        }
        else if (strcmp(command, "version") == 0) {
            if (strcmp(subcommand, "create") == 0) {
                if (!arg1[0] || !arg2[0]) {
                    printf("Error: version create requires <filename> <tag>\n");
                } else {
                    execute_checkpoint(&client_state, arg1, arg2);
                }
            } else if (strcmp(subcommand, "view") == 0) {
                if (!arg1[0] || !arg2[0]) {
                    printf("Error: version view requires <filename> <tag>\n");
                } else {
                    execute_viewcheckpoint(&client_state, arg1, arg2);
                }
            } else if (strcmp(subcommand, "revert") == 0) {
                if (!arg1[0] || !arg2[0]) {
                    printf("Error: version revert requires <filename> <tag>\n");
                } else {
                    execute_revert(&client_state, arg1, arg2);
                }
            } else if (strcmp(subcommand, "list") == 0) {
                if (!arg1[0]) {
                    printf("Error: version list requires <filename>\n");
                } else {
                    execute_listcheckpoints(&client_state, arg1);
                }
            } else {
                printf("Error: Unknown version subcommand '%s'\n", subcommand);
            }
        }
        else if (strcmp(command, "access") == 0) {
            if (strcmp(subcommand, "grant") == 0) {
                // flags: 0x01 = read, 0x02 = write
                if (!flags) flags = 0x01; // Default to read-only
                execute_addaccess(&client_state, arg1, arg2, flags & 0x01, flags & 0x02);
            } else if (strcmp(subcommand, "revoke") == 0) {
                execute_remaccess(&client_state, arg1, arg2);
            } else if (strcmp(subcommand, "request") == 0) {
                execute_requestaccess(&client_state, arg1, flags);
            } else if (strcmp(subcommand, "requests") == 0) {
                execute_viewrequests(&client_state, arg1);
            } else if (strcmp(subcommand, "approve") == 0) {
                execute_approverequest(&client_state, arg1, arg2);
            } else if (strcmp(subcommand, "deny") == 0) {
                execute_denyrequest(&client_state, arg1, arg2);
            } else {
                printf("Error: Unknown access subcommand '%s'\n", subcommand);
            }
        }
        else if (strcmp(command, "user") == 0) {
            if (strcmp(subcommand, "list") == 0) {
                execute_list(&client_state);
            } else {
                printf("Error: Unknown user subcommand '%s'\n", subcommand);
            }
        }
        else {
            printf("Error: Unknown command '%s'\n", command);
            printf("Type 'help' for available commands\n");
        }
    }
    
    // Cleanup
    close(client_state.nm_socket);
    printf("Disconnected from Name Server\n");
    
    return 0;
}
