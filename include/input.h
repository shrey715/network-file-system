#ifndef INPUT_H
#define INPUT_H

#include <stddef.h>

#define MAX_HISTORY 100
#define MAX_INPUT_LENGTH 1024

typedef struct {
    char* lines[MAX_HISTORY];
    int count;
    int current;
} InputHistory;

void init_history(InputHistory* hist);
void add_to_history(InputHistory* hist, const char* line);
char* read_line_with_history(InputHistory* hist, const char* prompt);
void free_history(InputHistory* hist);

#endif
