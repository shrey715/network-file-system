#include "input.h"
#include "common.h"
#include <termios.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

static struct termios orig_termios;
static int raw_mode_enabled = 0;

static void disable_raw_mode(void) {
    if (raw_mode_enabled) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
        raw_mode_enabled = 0;
    }
}

static int enable_raw_mode(void) {
    if (!isatty(STDIN_FILENO)) {
        return -1;  // Not a terminal
    }
    
    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) {
        return -1;
    }
    
    if (!raw_mode_enabled) {
        atexit(disable_raw_mode);
    }
    
    struct termios raw = orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        return -1;
    }
    
    raw_mode_enabled = 1;
    return 0;
}

void init_history(InputHistory* hist) {
    hist->count = 0;
    hist->current = 0;
    for (int i = 0; i < MAX_HISTORY; i++) {
        hist->lines[i] = NULL;
    }
}

void add_to_history(InputHistory* hist, const char* line) {
    if (line == NULL || strlen(line) == 0) {
        return;
    }
    
    // Don't add duplicate consecutive entries
    if (hist->count > 0 && strcmp(hist->lines[hist->count - 1], line) == 0) {
        return;
    }
    
    // Remove oldest if full
    if (hist->count == MAX_HISTORY) {
        free(hist->lines[0]);
        memmove(&hist->lines[0], &hist->lines[1], 
                sizeof(char*) * (MAX_HISTORY - 1));
        hist->count--;
    }
    
    hist->lines[hist->count] = strdup(line);
    hist->count++;
    hist->current = hist->count;
}

void free_history(InputHistory* hist) {
    for (int i = 0; i < hist->count; i++) {
        if (hist->lines[i] != NULL) {
            free(hist->lines[i]);
            hist->lines[i] = NULL;
        }
    }
    hist->count = 0;
    hist->current = 0;
}

static void clear_line(const char* prompt, int buf_len) {
    (void)buf_len;  // Unused parameter
    // Move cursor to start of line
    printf("\r");
    // Clear from cursor to end of line
    printf("\033[K");
    // Print prompt
    printf("%s", prompt);
    fflush(stdout);
}

static void redraw_line(const char* prompt, const char* buffer, int cursor_pos) {
    printf("\r%s%s\033[K", prompt, buffer);
    
    // Calculate visual prompt length (excluding ANSI codes)
    int prompt_len = visual_strlen(prompt);
    
    // Move cursor to correct position
    printf("\r\033[%dC", prompt_len + cursor_pos);
    fflush(stdout);
}

char* read_line_with_history(InputHistory* hist, const char* prompt) {
    // Check if stdin is a terminal
    if (!isatty(STDIN_FILENO)) {
        // Fallback to fgets for non-terminal input. Precede with CR to ensure
        // we're at column 0 when printing the prompt in case output wasn't
        // terminated by a newline previously.
        printf("\r%s", prompt);
        fflush(stdout);

        char buffer[MAX_INPUT_LENGTH];
        if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
            return NULL;
        }
        buffer[strcspn(buffer, "\n")] = '\0';
        return strdup(buffer);
    }
    
    if (enable_raw_mode() == -1) {
        return NULL;
    }
    
    printf("\r%s", prompt);
    fflush(stdout);
    
    char buffer[MAX_INPUT_LENGTH];
    char temp_buffer[MAX_INPUT_LENGTH];
    int buf_len = 0;
    int cursor_pos = 0;
    int browsing_history = 0;
    
    buffer[0] = '\0';
    temp_buffer[0] = '\0';
    
    while (1) {
        char c;
        ssize_t nread = read(STDIN_FILENO, &c, 1);
        
        if (nread == -1 || nread == 0) {
            break;
        }
        
        // Handle escape sequences (arrow keys, etc.)
        if (c == '\033') {
            char seq[3];
            
            if (read(STDIN_FILENO, &seq[0], 1) != 1) continue;
            if (read(STDIN_FILENO, &seq[1], 1) != 1) continue;
            
            if (seq[0] == '[') {
                switch (seq[1]) {
                    case 'A':  // Up arrow
                        if (hist->current > 0) {
                            if (!browsing_history) {
                                // Save current input
                                strncpy(temp_buffer, buffer, MAX_INPUT_LENGTH - 1);
                                temp_buffer[MAX_INPUT_LENGTH - 1] = '\0';
                                browsing_history = 1;
                            }
                            
                            hist->current--;
                            clear_line(prompt, buf_len);
                            
                            strncpy(buffer, hist->lines[hist->current], MAX_INPUT_LENGTH - 1);
                            buffer[MAX_INPUT_LENGTH - 1] = '\0';
                            buf_len = strlen(buffer);
                            cursor_pos = buf_len;
                            
                            printf("%s", buffer);
                            fflush(stdout);
                        }
                        break;
                        
                    case 'B':  // Down arrow
                        if (browsing_history && hist->current < hist->count) {
                            hist->current++;
                            clear_line(prompt, buf_len);
                            
                            if (hist->current == hist->count) {
                                // Restore original input
                                strncpy(buffer, temp_buffer, MAX_INPUT_LENGTH - 1);
                                buffer[MAX_INPUT_LENGTH - 1] = '\0';
                                browsing_history = 0;
                            } else {
                                // Show next history entry
                                strncpy(buffer, hist->lines[hist->current], MAX_INPUT_LENGTH - 1);
                                buffer[MAX_INPUT_LENGTH - 1] = '\0';
                            }
                            
                            buf_len = strlen(buffer);
                            cursor_pos = buf_len;
                            
                            printf("%s", buffer);
                            fflush(stdout);
                        }
                        break;
                        
                    case 'C':  // Right arrow
                        if (cursor_pos < buf_len) {
                            printf("\033[C");
                            cursor_pos++;
                            fflush(stdout);
                        }
                        break;
                        
                    case 'D':  // Left arrow
                        if (cursor_pos > 0) {
                            printf("\033[D");
                            cursor_pos--;
                            fflush(stdout);
                        }
                        break;
                        
                    case 'H':  // Home
                        if (cursor_pos > 0) {
                            printf("\r");
                            // Use visual_strlen instead of manual counting
                            int prompt_len = visual_strlen(prompt);
                            printf("\033[%dC", prompt_len);
                            cursor_pos = 0;
                            fflush(stdout);
                        }
                        break;
                        
                    case 'F':  // End
                        if (cursor_pos < buf_len) {
                            printf("\033[%dC", buf_len - cursor_pos);
                            cursor_pos = buf_len;
                            fflush(stdout);
                        }
                        break;
                        
                    case '3':  // Delete key
                        if (read(STDIN_FILENO, &seq[2], 1) == 1 && seq[2] == '~') {
                            if (cursor_pos < buf_len) {
                                memmove(&buffer[cursor_pos], &buffer[cursor_pos + 1], 
                                        buf_len - cursor_pos);
                                buf_len--;
                                buffer[buf_len] = '\0';
                                redraw_line(prompt, buffer, cursor_pos);
                            }
                        }
                        break;
                }
            }
            continue;
        }
        
        // Handle Enter/Return
        if (c == '\r' || c == '\n') {
            printf("\n");
            break;
        }
        
        // Handle Backspace (127 or 8)
        if (c == 127 || c == '\b') {
            if (cursor_pos > 0) {
                memmove(&buffer[cursor_pos - 1], &buffer[cursor_pos], 
                        buf_len - cursor_pos + 1);
                buf_len--;
                cursor_pos--;
                buffer[buf_len] = '\0';
                redraw_line(prompt, buffer, cursor_pos);
            }
            continue;
        }
        
        // Handle Ctrl+C
        if (c == 3) {
            printf("^C\n");
            disable_raw_mode();
            return NULL;
        }
        
        // Handle Ctrl+D (EOF)
        if (c == 4) {
            if (buf_len == 0) {
                printf("\n");
                disable_raw_mode();
                return NULL;
            }
            continue;
        }
        
        // Handle Ctrl+U (clear line)
        if (c == 21) {
            clear_line(prompt, buf_len);
            buffer[0] = '\0';
            buf_len = 0;
            cursor_pos = 0;
            continue;
        }
        
        // Handle Ctrl+K (kill to end of line)
        if (c == 11) {
            buffer[cursor_pos] = '\0';
            buf_len = cursor_pos;
            printf("\033[K");
            fflush(stdout);
            continue;
        }
        
        // Handle Ctrl+A (move to start)
        if (c == 1) {
            while (cursor_pos > 0) {
                printf("\033[D");
                cursor_pos--;
            }
            fflush(stdout);
            continue;
        }
        
        // Handle Ctrl+E (move to end)
        if (c == 5) {
            while (cursor_pos < buf_len) {
                printf("\033[C");
                cursor_pos++;
            }
            fflush(stdout);
            continue;
        }
        
        // Handle printable characters
        if (isprint((unsigned char)c) && buf_len < MAX_INPUT_LENGTH - 1) {
            // Insert character at cursor position
            memmove(&buffer[cursor_pos + 1], &buffer[cursor_pos], 
                    buf_len - cursor_pos + 1);
            buffer[cursor_pos] = c;
            buf_len++;
            cursor_pos++;
            buffer[buf_len] = '\0';
            
            redraw_line(prompt, buffer, cursor_pos);
        }
    }
    
    disable_raw_mode();
    
    char* result = malloc(buf_len + 1);
    if (result != NULL) {
        strncpy(result, buffer, buf_len);
        result[buf_len] = '\0';
    }
    printf("\r");
    fflush(stdout);
    return result;
}
