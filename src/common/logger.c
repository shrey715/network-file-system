#include "common.h"

// Thread-safe logging
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

// General log message
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

// Log an operation with details
void log_operation(const char* component, const char* username, const char* operation, 
                   const char* filename, int status) {
    char message[512];
    snprintf(message, sizeof(message), "User=%s Operation=%s File=%s Status=%s",
             username, operation, filename, 
             status == ERR_SUCCESS ? "SUCCESS" : get_error_message(status));
    log_message(component, "INFO", message);
}
