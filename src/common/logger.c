#include "common.h"

/* Thread-safe logging mutex used by the logging functions. */
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Global flag to enable/disable colorized console output. Default enabled. */
int enable_colors = 1;

/**
 * log_message
 * @brief Simple timestamp + component + level + message logging.
 *        Format: [timestamp] [component] [level] message
 *
 * This function acquires an internal mutex to ensure log lines are not
 * interleaved when called from multiple threads.
 *
 * @param component Short component name (e.g., "NM", "SS", "CL") used to
 *                  name the logfile and tag the message.
 * @param level Log level string (e.g., "INFO", "ERROR", "WARN", "DEBUG").
 * @param message Null-terminated message text to log (pre-formatted).
 */
void log_message(const char* component, const char* level, const char* message) {
    pthread_mutex_lock(&log_mutex);
    
    time_t now = time(NULL);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));
    
    // Determine color based on level
    const char* level_color = ANSI_RESET;
    if (strcmp(level, "ERROR") == 0) {
        level_color = ANSI_BRIGHT_RED;
    } else if (strcmp(level, "WARN") == 0) {
        level_color = ANSI_YELLOW;
    } else if (strcmp(level, "INFO") == 0) {
        level_color = ANSI_CYAN;
    } else if (strcmp(level, "DEBUG") == 0) {
        level_color = ANSI_BRIGHT_BLACK;
    }

    // Print to console with colors (if enabled)
    if (enable_colors) {
        printf("%s[%s]%s %s[%s]%s %s[%s]%s %s\n", 
               ANSI_BRIGHT_BLACK, timestamp, ANSI_RESET,
               ANSI_BLUE, component, ANSI_RESET,
               level_color, level, ANSI_RESET,
               message);
    } else {
        printf("[%s] [%s] [%s] %s\n", timestamp, component, level, message);
    }
    fflush(stdout);
    
    // Write to log file
    char filename[128];
    snprintf(filename, sizeof(filename), "logs/%s.log", component);
    FILE* f = fopen(filename, "a");
    if (f) {
        fprintf(f, "[%s] [%s] %s\n", timestamp, level, message);
        fclose(f);
    }
    
    pthread_mutex_unlock(&log_mutex);
}

/**
 * log_operation
 * @brief Enhanced logging with operation details, user, IP, port, and error codes.
 * 
 * Format: [timestamp] [component] [level] [operation] user=X ip=X:port details | status
 * 
 * @param component Component name ("NM", "SS", "CLIENT")
 * @param level Log level ("INFO", "ERROR", "WARN", "DEBUG")
 * @param operation Operation name (e.g., "CREATE", "READ", "SS_REGISTER")
 * @param username Username performing operation (can be NULL)
 * @param ip IP address (can be NULL)
 * @param port Port number (0 if not applicable)
 * @param details Additional details (can be NULL)
 * @param error_code Error code (0 for success, or ERR_* codes)
 */
void log_operation(const char* component, const char* level, const char* operation,
                   const char* username, const char* ip, int port, 
                   const char* details, int error_code) {
    pthread_mutex_lock(&log_mutex);
    
    time_t now = time(NULL);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));
    
    // Build detailed message
    char message[2048];
    int offset = 0;
    
    // Operation
    offset += snprintf(message + offset, sizeof(message) - offset, "[%s]", operation);
    
    // Username
    if (username && username[0]) {
        offset += snprintf(message + offset, sizeof(message) - offset, " user=%s", username);
    }
    
    // IP and port
    if (ip && ip[0]) {
        if (port > 0) {
            offset += snprintf(message + offset, sizeof(message) - offset, " from=%s:%d", ip, port);
        } else {
            offset += snprintf(message + offset, sizeof(message) - offset, " from=%s", ip);
        }
    }
    
    // Details
    if (details && details[0]) {
        offset += snprintf(message + offset, sizeof(message) - offset, " | %s", details);
    }
    
    // Status
    if (error_code == 0) {
        offset += snprintf(message + offset, sizeof(message) - offset, " | SUCCESS");
    } else {
        offset += snprintf(message + offset, sizeof(message) - offset, 
                          " | FAILED (error=%d: %s)", error_code, get_error_message(error_code));
    }
    
    // Determine color based on level
    const char* level_color = ANSI_RESET;
    if (strcmp(level, "ERROR") == 0) {
        level_color = ANSI_BRIGHT_RED;
    } else if (strcmp(level, "WARN") == 0) {
        level_color = ANSI_YELLOW;
    } else if (strcmp(level, "INFO") == 0) {
        level_color = ANSI_CYAN;
    } else if (strcmp(level, "DEBUG") == 0) {
        level_color = ANSI_BRIGHT_BLACK;
    }
    
    // Print to console with colors
    if (enable_colors) {
        printf("%s[%s]%s %s[%s]%s %s[%s]%s %s\n", 
               ANSI_BRIGHT_BLACK, timestamp, ANSI_RESET,
               ANSI_BLUE, component, ANSI_RESET,
               level_color, level, ANSI_RESET,
               message);
    } else {
        printf("[%s] [%s] [%s] %s\n", timestamp, component, level, message);
    }
    fflush(stdout);
    
    // Write to log file
    char filename[128];
    snprintf(filename, sizeof(filename), "logs/%s.log", component);
    FILE* f = fopen(filename, "a");
    if (f) {
        fprintf(f, "[%s] [%s] %s\n", timestamp, level, message);
        fclose(f);
    }
    
    pthread_mutex_unlock(&log_mutex);
}
