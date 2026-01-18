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
        if (header.error_code == ERR_USERNAME_TAKEN) {
            PRINT_ERR("Username '%s' is already in use. Please choose a different username.", 
                     client_state.username);
        } else {
            PRINT_ERR("Failed to register with Name Server: %s", 
                     get_error_message(header.error_code));
        }
        close(client_state.nm_socket);
        return 1;
    }
    if (response) free(response);
    
    // Initialize command history
    InputHistory history;
    init_history(&history);
    
    PRINT_INFO("\nEnter commands (type 'help' for list of commands, 'quit' to exit):");
    
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
        
        if (strcmp(input, "help") == 0 || strcmp(input, "?") == 0) {
            printf("\n");
            printf(ANSI_BOLD "Available commands:" ANSI_RESET "\n");
            printf("\n");
            printf(ANSI_CYAN "Files:" ANSI_RESET "\n");
            printf("  ls [-l]                      - List files\n");
            printf("  cat <file>                   - Display file content\n");
            printf("  touch <file>                 - Create new file\n");
            printf("  rm <file>                    - Delete file\n");
            printf("  mv <src> <dst>               - Move/rename file\n");
            printf("  mkdir <dir>                  - Create directory\n");
            printf("  info <file>                  - File metadata\n");
            printf("\n");
            printf(ANSI_CYAN "Editor:" ANSI_RESET "\n");
            printf("  open <file>                  - View file (read-only)\n");
            printf("  edit <file> <idx>            - Edit sentence\n");
            printf("  undo <file>                  - Undo last change\n");
            printf("\n");
            printf(ANSI_CYAN "Version Control:" ANSI_RESET "\n");
            printf("  commit <file> <tag>          - Create checkpoint\n");
            printf("  log <file>                   - List checkpoints\n");
            printf("  checkout <file> <tag>        - Revert to checkpoint\n");
            printf("  diff <file> <tag>            - View checkpoint\n");
            printf("\n");
            printf(ANSI_CYAN "Access Control:" ANSI_RESET "\n");
            printf("  chmod <file> <user> [r][w]   - Grant access\n");
            printf("  acl <file>                   - View access list\n");
            printf("\n");
            printf(ANSI_CYAN "Other:" ANSI_RESET "\n");
            printf("  agent <file> <prompt>        - Generate with AI\n");
            printf("\n");
            printf("quit/exit/q - Exit client\n");
            printf("Tab - Command completion\n\n");
            free(input);
            continue;
        }
        
        int result = parse_command(input, command, subcommand, arg1, arg2, &flags);
        if (result < 0) {
            PRINT_ERR("Invalid command format");
            free(input);
            continue;
        }
        
        // Execute commands - Unix-style
        
        /* File operations */
        if (strcmp(command, "ls") == 0) {
            execute_view(&client_state, flags);
        }
        else if (strcmp(command, "cat") == 0) {
            if (subcommand[0] == '\0') {
                PRINT_ERR("Usage: cat <file>");
            } else {
                execute_read(&client_state, subcommand);
            }
        }
        else if (strcmp(command, "touch") == 0) {
            if (subcommand[0] == '\0') {
                PRINT_ERR("Usage: touch <file>");
            } else {
                execute_create(&client_state, subcommand);
            }
        }
        else if (strcmp(command, "rm") == 0) {
            if (subcommand[0] == '\0') {
                PRINT_ERR("Usage: rm <file>");
            } else {
                execute_delete(&client_state, subcommand);
            }
        }
        else if (strcmp(command, "mv") == 0) {
            if (subcommand[0] == '\0' || arg1[0] == '\0') {
                PRINT_ERR("Usage: mv <src> <dst>");
            } else {
                execute_move(&client_state, subcommand, arg1);
            }
        }
        else if (strcmp(command, "mkdir") == 0) {
            if (subcommand[0] == '\0') {
                PRINT_ERR("Usage: mkdir <dir>");
            } else {
                execute_createfolder(&client_state, subcommand);
            }
        }
        else if (strcmp(command, "info") == 0) {
            if (subcommand[0] == '\0') {
                PRINT_ERR("Usage: info <file>");
            } else {
                execute_info(&client_state, subcommand);
            }
        }
        
        /* Editor */
        else if (strcmp(command, "open") == 0) {
            if (subcommand[0] == '\0') {
                PRINT_ERR("Usage: open <file>");
            } else {
                execute_open(&client_state, subcommand);
            }
        }
        else if (strcmp(command, "edit") == 0) {
            if (subcommand[0] == '\0' || arg1[0] == '\0') {
                PRINT_ERR("Usage: edit <file> <idx>");
            } else {
                int sentence_idx = atoi(arg1);
                execute_edit(&client_state, subcommand, sentence_idx);
            }
        }
        else if (strcmp(command, "undo") == 0) {
            if (subcommand[0] == '\0') {
                PRINT_ERR("Usage: undo <file>");
            } else {
                execute_undo(&client_state, subcommand);
            }
        }
        
        /* Version control (git-style) */
        else if (strcmp(command, "commit") == 0) {
            if (subcommand[0] == '\0' || arg1[0] == '\0') {
                PRINT_ERR("Usage: commit <file> <tag>");
            } else {
                execute_checkpoint(&client_state, subcommand, arg1);
            }
        }
        else if (strcmp(command, "log") == 0) {
            if (subcommand[0] == '\0') {
                PRINT_ERR("Usage: log <file>");
            } else {
                execute_listcheckpoints(&client_state, subcommand);
            }
        }
        else if (strcmp(command, "checkout") == 0) {
            if (subcommand[0] == '\0' || arg1[0] == '\0') {
                PRINT_ERR("Usage: checkout <file> <tag>");
            } else {
                execute_revert(&client_state, subcommand, arg1);
            }
        }
        else if (strcmp(command, "diff") == 0) {
            if (subcommand[0] == '\0' || arg1[0] == '\0') {
                PRINT_ERR("Usage: diff <file> <tag>");
            } else {
                execute_viewcheckpoint(&client_state, subcommand, arg1);
            }
        }
        
        /* Access control */
        else if (strcmp(command, "chmod") == 0) {
            if (subcommand[0] == '\0' || arg1[0] == '\0') {
                PRINT_ERR("Usage: chmod <file> <user> [r][w]");
            } else {
                // Default to read-only if no flags
                if (!flags) flags = 0x01;
                int read = (flags & 0x01) ? 1 : 0;
                int write = (flags & 0x02) ? 1 : 0;
                if (write) read = 1;
                execute_addaccess(&client_state, subcommand, arg1, read, write);
            }
        }
        else if (strcmp(command, "acl") == 0) {
            if (subcommand[0] == '\0') {
                PRINT_ERR("Usage: acl <file>");
            } else {
                execute_info(&client_state, subcommand);
            }
        }
        
        /* AI Agent */
        else if (strcmp(command, "agent") == 0) {
            if (subcommand[0] == '\0') {
                PRINT_ERR("Usage: agent <file> <prompt>");
            } else {
                const char* prompt_start = strstr(input, subcommand) + strlen(subcommand);
                while (*prompt_start == ' ' || *prompt_start == '\t') prompt_start++;
                if (strlen(prompt_start) > 0) {
                    execute_agent(&client_state, subcommand, prompt_start);
                } else {
                    PRINT_ERR("Usage: agent <file> <prompt>");
                }
            }
        }
        
        else {
            PRINT_ERR("Unknown command '%s'", command);
            printf("Type 'help' for available commands\n");
        }
        printf("\n");
        fflush(stdout);
        free(input);
    }
    
    // Cleanup
    free_history(&history);
    close(client_state.nm_socket);
    PRINT_INFO("Disconnected from Name Server");
    
    return 0;
}
