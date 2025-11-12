#include "common.h"
#include "client.h"

/**
 * parse_command
 * @brief Parse a user's input line into a command and up to two arguments.
 *
 * Special-cases certain commands that include flags (e.g., VIEW and
 * ADDACCESS). Copies parsed tokens into the provided buffers. Buffers must
 * be large enough to hold the resulting strings. Modifies internal state of
 * `flags` to communicate flag semantics to callers.
 *
 * @param input Null-terminated input line to parse.
 * @param command Out buffer to receive the command name.
 * @param arg1 Out buffer to receive the first argument (if any).
 * @param arg2 Out buffer to receive the second argument (if any).
 * @param flags Out parameter for parsed flags (bitmask or mode values).
 * @return 0 on success, -1 on parse error.
 */
int parse_command(const char* input, char* command, char* arg1, char* arg2, int* flags) {
    command[0] = '\0';
    arg1[0] = '\0';
    arg2[0] = '\0';
    *flags = 0;
    
    char buffer[BUFFER_SIZE];
    strncpy(buffer, input, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';
    
    char* token = strtok(buffer, " \t");
    if (!token) return -1;
    
    strcpy(command, token);
    
    // Handle VIEW with flags
    if (strcmp(command, "VIEW") == 0) {
        token = strtok(NULL, " \t");
        if (token) {
            if (strstr(token, "a")) *flags |= 1;  // -a flag
            if (strstr(token, "l")) *flags |= 2;  // -l flag
        }
        return 0;
    }
    
    // Handle ADDACCESS with flags
    if (strcmp(command, "ADDACCESS") == 0) {
        token = strtok(NULL, " \t");
        if (!token) return -1;
        
        if (strcmp(token, "-R") == 0) {
            *flags = 1;  // Read only
        } else if (strcmp(token, "-W") == 0) {
            *flags = 2;  // Write (includes read)
        } else {
            return -1;
        }
        
        token = strtok(NULL, " \t");
        if (token) strcpy(arg1, token);
        
        token = strtok(NULL, " \t");
        if (token) strcpy(arg2, token);
        
        return 0;
    }
    
    // Handle REQUESTACCESS with flags
    if (strcmp(command, "REQUESTACCESS") == 0) {
        *flags = 0;  // Default: no flags (will be interpreted as read-only)
        
        token = strtok(NULL, " \t");
        if (!token) return -1;
        
        // Parse flags
        while (token && token[0] == '-') {
            if (strcmp(token, "-R") == 0) {
                *flags |= 0x01;  // Read flag
            } else if (strcmp(token, "-W") == 0) {
                *flags |= 0x02;  // Write flag
            }
            token = strtok(NULL, " \t");
        }
        
        // The current token should be the filename
        if (token) {
            strcpy(arg1, token);
        }
        
        return 0;
    }
    
    // Get first argument
    token = strtok(NULL, " \t");
    if (token) {
        strcpy(arg1, token);
    }
    
    // Get second argument
    token = strtok(NULL, " \t");
    if (token) {
        strcpy(arg2, token);
    }
    
    return 0;
}
