#include "common.h"
#include "client.h"

/**
 * parse_command
 * @brief Parse a user's input line into command, subcommand, and arguments.
 *
 * Supports two-level command structure (e.g., "file create", "version list").
 * Handles flags for specific commands.
 *
 * @param input Null-terminated input line to parse.
 * @param command Out buffer to receive the main command name.
 * @param subcommand Out buffer to receive the subcommand (if any).
 * @param arg1 Out buffer to receive the first argument (if any).
 * @param arg2 Out buffer to receive the second argument (if any).
 * @param flags Out parameter for parsed flags (bitmask or mode values).
 * @return 0 on success, -1 on parse error.
 */
int parse_command(const char* input, char* command, char* subcommand, char* arg1, char* arg2, int* flags) {
    command[0] = '\0';
    subcommand[0] = '\0';
    arg1[0] = '\0';
    arg2[0] = '\0';
    *flags = 0;

    char buffer[BUFFER_SIZE];
    strncpy(buffer, input, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    char* token = strtok(buffer, " \t");
    if (!token) return -1;
    
    strcpy(command, token);

    // Get subcommand for multi-level commands
    token = strtok(NULL, " \t");
    if (!token) return 0; // Command only, no subcommand
    
    strcpy(subcommand, token);

    // Handle flags for specific commands
    if (strcmp(command, "access") == 0 && strcmp(subcommand, "grant") == 0) {
        // Parse -R or -W flags
        token = strtok(NULL, " \t");
        while (token && token[0] == '-') {
            if (strcmp(token, "-R") == 0) *flags |= 0x01;
            else if (strcmp(token, "-W") == 0) *flags |= 0x02;
            token = strtok(NULL, " \t");
        }
        if (token) strcpy(arg1, token);
        token = strtok(NULL, " \t");
        if (token) strcpy(arg2, token);
        return 0;
    }

    if (strcmp(command, "access") == 0 && strcmp(subcommand, "request") == 0) {
        // Parse -R or -W flags
        token = strtok(NULL, " \t");
        while (token && token[0] == '-') {
            if (strcmp(token, "-R") == 0) *flags |= 0x01;
            else if (strcmp(token, "-W") == 0) *flags |= 0x02;
            token = strtok(NULL, " \t");
        }
        if (token) strcpy(arg1, token);
        return 0;
    }

    if (strcmp(command, "file") == 0 && strcmp(subcommand, "list") == 0) {
        // Parse -a and -l flags
        token = strtok(NULL, " \t");
        while (token && token[0] == '-') {
            if (strstr(token, "a")) *flags |= 0x01;
            if (strstr(token, "l")) *flags |= 0x02;
            token = strtok(NULL, " \t");
        }
        return 0;
    }

    // Standard argument parsing
    token = strtok(NULL, " \t");
    if (token) strcpy(arg1, token);
    
    token = strtok(NULL, " \t");
    if (token) strcpy(arg2, token);

    return 0;
}
