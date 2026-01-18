#ifndef INPUT_H
#define INPUT_H

#include <stddef.h>

#define MAX_HISTORY 100
#define MAX_INPUT_LENGTH 1024

typedef struct {
  char *lines[MAX_HISTORY];
  int count;
  int current;
} InputHistory;

/* Available commands for tab completion */
extern const char *COMMANDS[];

/* History management */
void init_history(InputHistory *hist);
void add_history(InputHistory *hist, const char *line);
void free_history(InputHistory *hist);

/* Terminal control */
int enable_raw_mode(void);
void disable_raw_mode(void);

/* Line reading */
char *read_line_with_history(const char *prompt, InputHistory *hist);

#endif
