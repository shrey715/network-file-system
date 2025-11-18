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
