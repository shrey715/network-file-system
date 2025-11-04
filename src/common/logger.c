#include "common.h"

/* Thread-safe logging mutex used by the logging functions. */
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * log_message
 * @brief Thread-safe logging helper that writes a timestamped message to
 *        stdout and to a per-component logfile under `logs/`.
 *
 * This function acquires an internal mutex to ensure log lines are not
 * interleaved when called from multiple threads.
 *
 * @param component Short component name (e.g., "NM", "SS", "CL") used to
 *                  name the logfile and tag the message.
 * @param level Log level string (e.g., "INFO", "ERROR").
 * @param message Null-terminated message text to log.
 */
void log_message(const char* component, const char* level, const char* message) {
    pthread_mutex_lock(&log_mutex);
    
    time_t now = time(NULL);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));
    
    // Print to console
    printf("[%s] [%s] [%s] %s\n", timestamp, component, level, message);
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
 * @brief Convenience helper to format and log a user operation event.
 *
 * Formats operation details (user, operation name, filename and status) and
 * delegates to `log_message` with an "INFO" level.
 *
 * @param component Component short name used for log file naming.
 * @param username Username performing the operation.
 * @param operation Operation name (e.g., "READ", "WRITE").
 * @param filename Target filename for the operation.
 * @param status Integer status code (ERR_SUCCESS or an ERR_* value).
 */
void log_operation(const char* component, const char* username, const char* operation, 
                   const char* filename, int status) {
    char message[512];
    snprintf(message, sizeof(message), "User=%s Operation=%s File=%s Status=%s",
             username, operation, filename, 
             status == ERR_SUCCESS ? "SUCCESS" : get_error_message(status));
    log_message(component, "INFO", message);
}
