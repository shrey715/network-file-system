#include "common.h"
#include "client.h"
#include "input.h"

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
    PRINT_INFO("Enter username:");
    if (fgets(client_state.username, sizeof(client_state.username), stdin) == NULL) {
        fprintf(stderr, "Failed to read username\n");
        return 1;
    }
    client_state.username[strcspn(client_state.username, "\n")] = 0;  // Remove newline
    
    // Connect to name server
    client_state.nm_socket = connect_to_server(client_state.nm_ip, client_state.nm_port);
    if (client_state.nm_socket < 0) {
        PRINT_ERR("Failed to connect to Name Server at %s:%d", client_state.nm_ip, client_state.nm_port);
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
        PRINT_OK("Connected to Name Server as '%s'", client_state.username);
    } else {
        PRINT_ERR("Failed to register with Name Server");
        close(client_state.nm_socket);
        return 1;
    }
    if (response) free(response);
    
    // Initialize command history
    InputHistory history;
    init_history(&history);
    
    PRINT_INFO("\nEnter commands (type 'help' for list of commands, 'quit' to exit):");
    printf("Use Up/Down arrows for command history\n\n");
    
    // Main command loop
    while (1) {
        char* input = read_line_with_history(&history, ANSI_BRIGHT_BLUE "> " ANSI_RESET);
        
        if (input == NULL) {
            printf("\n");
            break;  // EOF or Ctrl+C
        }
        
        // Skip empty lines
        if (strlen(input) == 0) {
            free(input);
            continue;
        }
        
        // Add non-empty commands to history
        add_to_history(&history, input);
        
        // Parse command
        char command[64], subcommand[64], arg1[MAX_FILENAME], arg2[MAX_USERNAME];
        int flags = 0;
        
        if (strcmp(input, "quit") == 0 || strcmp(input, "exit") == 0) {
            free(input);
            break;
        }
        
        if (strcmp(input, "help") == 0) {
            printf("\n");
            printf(ANSI_BOLD "Available commands:" ANSI_RESET "\n");
            printf("\n");
            printf(ANSI_CYAN "File System:" ANSI_RESET "\n");
            printf("  file create <filename>         - Create new file\n");
            printf("  file delete <filename>         - Delete file\n");
            printf("  file read <filename>           - Read file content\n");
            printf("  file info <filename>           - Get file information\n");
            printf("  file list [-a] [-l]            - List files\n");
            printf("  file move <file> <folder>      - Move file to folder\n");
            printf("  file stream <filename>         - Stream file content\n");
            printf("  file exec <filename>           - Execute file as commands\n");
            printf("\n");
            printf(ANSI_CYAN "Edit System:" ANSI_RESET "\n");
            printf("  edit <filename> <idx>        - Edit sentence at index\n");
            printf("  edit undo <filename>         - Undo last change\n");
            printf("\n");
            printf(ANSI_CYAN "Folder System:" ANSI_RESET "\n");
            printf("  folder create <name>         - Create new folder\n");
            printf("  folder view [path]           - List folder contents\n");
            printf("\n");
            printf(ANSI_CYAN "Version Control:" ANSI_RESET "\n");
            printf("  version create <file> <tag>  - Create checkpoint\n");
            printf("  version view <file> <tag>    - View checkpoint content\n");
            printf("  version revert <file> <tag>  - Revert to checkpoint\n");
            printf("  version list <file>          - List all checkpoints\n");
            printf("\n");
            printf(ANSI_CYAN "Access Control:" ANSI_RESET "\n");
            printf("  access grant <file> <user> [-R|-W]   - Grant access\n");
            printf("  access revoke <file> <user>          - Revoke access\n");
            printf("  access request <file> [-R] [-W]      - Request access\n");
            printf("  access requests <file>               - View requests (owner)\n");
            printf("  access approve <file> <user>         - Approve request (owner)\n");
            printf("  access deny <file> <user>            - Deny request (owner)\n");
            printf("\n");
            printf(ANSI_CYAN "User System:" ANSI_RESET "\n");
            printf("  user list                    - List all users\n");
            printf("\nquit/exit - Exit client\n\n");
            free(input);
            continue;
        }
        
        int result = parse_command(input, command, subcommand, arg1, arg2, &flags);
        if (result < 0) {
            PRINT_ERR("Invalid command format");
            free(input);
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
                PRINT_ERR("Unknown file subcommand '%s'", subcommand);
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
                PRINT_ERR("Unknown folder subcommand '%s'", subcommand);
            }
        }
        else if (strcmp(command, "version") == 0) {
            if (strcmp(subcommand, "create") == 0) {
                if (!arg1[0] || !arg2[0]) {
                    PRINT_ERR("version create requires <filename> <tag>");
                } else {
                    execute_checkpoint(&client_state, arg1, arg2);
                }
            } else if (strcmp(subcommand, "view") == 0) {
                if (!arg1[0] || !arg2[0]) {
                    PRINT_ERR("version view requires <filename> <tag>");
                } else {
                    execute_viewcheckpoint(&client_state, arg1, arg2);
                }
            } else if (strcmp(subcommand, "revert") == 0) {
                if (!arg1[0] || !arg2[0]) {
                    PRINT_ERR("version revert requires <filename> <tag>");
                } else {
                    execute_revert(&client_state, arg1, arg2);
                }
            } else if (strcmp(subcommand, "list") == 0) {
                if (!arg1[0]) {
                    PRINT_ERR("version list requires <filename>");
                } else {
                    execute_listcheckpoints(&client_state, arg1);
                }
            } else {
                PRINT_ERR("Unknown version subcommand '%s'", subcommand);
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
                PRINT_ERR("Unknown access subcommand '%s'", subcommand);
            }
        }
        else if (strcmp(command, "user") == 0) {
            if (strcmp(subcommand, "list") == 0) {
                execute_list(&client_state);
            } else {
                PRINT_ERR("Unknown user subcommand '%s'", subcommand);
            }
        }
        else {
            PRINT_ERR("Unknown command '%s'", command);
            printf("Type 'help' for available commands\n");
        }
        
        free(input);
    }
    
    // Cleanup
    free_history(&history);
    close(client_state.nm_socket);
    PRINT_INFO("Disconnected from Name Server");
    
    return 0;
}
