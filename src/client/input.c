#include "input.h"
#include "common.h"
#include <termios.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <sys/ioctl.h>

static struct termios orig_termios;
static int raw_mode_enabled = 0;

/* Command registry for tab completion */
const char* COMMANDS[] = {
    "acl", "agent", "cat", "checkout", "chmod", "commit",
    "diff", "edit", "exit", "help", "info", "log",
    "ls", "mkdir", "mv", "open", "quit", "rm", "touch", "undo"
};
const int COMMAND_COUNT = sizeof(COMMANDS) / sizeof(COMMANDS[0]);

/**
 * find_first_prefix_match
 * @brief Binary search to find index of first command matching prefix.
 *
 * @param prefix Prefix to match.
 * @param prefix_len Length of prefix.
 * @return Index of first match, or -1 if none.
 */
static int find_first_prefix_match(const char* prefix, int prefix_len) {
    int lo = 0, hi = COMMAND_COUNT - 1, result = -1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int cmp = strncmp(COMMANDS[mid], prefix, prefix_len);
        if (cmp >= 0) {
            if (cmp == 0) result = mid;
            hi = mid - 1;
        } else {
            lo = mid + 1;
        }
    }
    return result;
}

/**
 * disable_raw_mode
 * @brief Restore terminal attributes to their original state if raw mode
 *        was previously enabled. This function is idempotent.
 *
 * No parameters. Returns nothing.
 */
void disable_raw_mode(void) {
    if (raw_mode_enabled) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
        raw_mode_enabled = 0;
    }
}

/**
 * enable_raw_mode
 * @brief Put the terminal into a raw input mode suitable for single-key
 *        processing (disables canonical mode, echo, and most input
 *        processing). If stdin is not a TTY the function returns -1.
 *
 * @return 0 on success, -1 on failure or if stdin is not a terminal.
 */
int enable_raw_mode(void) {
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

/**
 * add_history
 * @brief Append a line to the input history buffer. Consecutive duplicate
 *        entries are ignored. If the history buffer is full the oldest entry
 *        is removed.
 *
 * @param hist Pointer to an InputHistory structure to update.
 * @param line Null-terminated input line to add.
 */
void add_history(InputHistory* hist, const char* line) {
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
        free(hist->lines[i]);
    }
    hist->count = 0;
    hist->current = 0;
}

/**
 * clear_line
 * @brief Clear the current terminal input line and reprint the prompt.
 *
 * @param prompt The prompt string to print after clearing the line.
 */
static void clear_line(const char* prompt) {
    // Move cursor to start of line
    printf("\r");
    // Clear from cursor to end of line
    printf("\033[K");
    // Print prompt
    printf("%s", prompt);
    fflush(stdout);
}

/**
 * redraw_line
 * @brief Re-render the prompt and current input buffer, and position the
 *        cursor at the specified logical cursor position.
 *
 * @param prompt Prompt string (may include ANSI color codes).
 * @param buffer Current input buffer to display.
 * @param cursor_pos Cursor position within buffer (0..strlen(buffer)).
 */
static void redraw_line(const char* prompt, const char* buffer, int cursor_pos) {
    // Get terminal width for handling line wrapping
    struct winsize ws;
    int term_width = 80;  // Default
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
        term_width = ws.ws_col;
    }
    
    int prompt_len = visual_strlen(prompt);
    int buf_len = strlen(buffer);
    int total_len = prompt_len + buf_len;
    
    // Calculate how many lines we're currently using
    int lines_used = (total_len + term_width - 1) / term_width;
    if (lines_used < 1) lines_used = 1;
    
    // Move cursor up to the start line if wrapped
    if (lines_used > 1) {
        printf("\033[%dA", lines_used - 1);
    }
    
    // Move to column 0 and clear from cursor to end of screen
    printf("\r\033[J");
    
    // Print prompt and buffer
    printf("%s%s", prompt, buffer);
    
    // Calculate cursor position and move there
    int cursor_total = prompt_len + cursor_pos;
    int cursor_line = cursor_total / term_width;
    int cursor_col = cursor_total % term_width;
    
    // Calculate current line (after printing)
    int current_line = total_len / term_width;
    
    // Move up from current line to cursor line
    if (current_line > cursor_line) {
        printf("\033[%dA", current_line - cursor_line);
    }
    
    // Move to correct column
    printf("\r\033[%dC", cursor_col);
    
    fflush(stdout);
}

/**
 * read_line_with_history
 * @brief Read a single input line from the user with basic line-editing
 *        and history support. If stdin is not a terminal, falls back to
 *        fgets(). This function temporarily enables raw mode to implement
 *        arrow-key navigation and then restores terminal state.
 *
 * @param prompt Prompt string to display (may include ANSI codes).
 * @param hist Pointer to an InputHistory instance used for up/down
 *             navigation and storage.
 * @return Malloc'd null-terminated string containing the entered line on
 *         success (caller must free), or NULL on EOF/error or cancel.
 */
char* read_line_with_history(const char* prompt, InputHistory* hist) {
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
                            clear_line(prompt);
                            
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
                            clear_line(prompt);
                            
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
            clear_line(prompt);
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
        
        // Handle Tab (command completion)
        if (c == '\t') {
            // Find the start of the current word
            int word_start = 0;
            for (int i = cursor_pos - 1; i >= 0; i--) {
                if (buffer[i] == ' ') {
                    word_start = i + 1;
                    break;
                }
            }
            
            // Extract prefix to match
            char prefix[MAX_INPUT_LENGTH];
            int prefix_len = cursor_pos - word_start;
            strncpy(prefix, buffer + word_start, prefix_len);
            prefix[prefix_len] = '\0';
            
            // Only complete commands (first word)
            if (word_start == 0 && prefix_len > 0) {
                // Use binary search to find first match
                int first_match = find_first_prefix_match(prefix, prefix_len);
                
                if (first_match >= 0) {
                    // Count matches by scanning forward from first match
                    int match_count = 0;
                    for (int i = first_match; i < COMMAND_COUNT && 
                         strncmp(COMMANDS[i], prefix, prefix_len) == 0; i++) {
                        match_count++;
                    }
                    
                    if (match_count == 1) {
                        // Unique match - complete it
                        const char* match = COMMANDS[first_match];
                        int add_len = strlen(match) - prefix_len;
                        if (buf_len + add_len < MAX_INPUT_LENGTH - 1) {
                            strcpy(buffer + cursor_pos, match + prefix_len);
                            strcat(buffer, " ");
                            buf_len = strlen(buffer);
                            cursor_pos = buf_len;
                            redraw_line(prompt, buffer, cursor_pos);
                        }
                    } else {
                        // Multiple matches - show them on a new line
                        printf("\n");
                        for (int i = first_match; i < COMMAND_COUNT && 
                             strncmp(COMMANDS[i], prefix, prefix_len) == 0; i++) {
                            printf("%s  ", COMMANDS[i]);
                        }
                        printf("\n");
                        // Use redraw_line for proper cursor positioning
                        redraw_line(prompt, buffer, cursor_pos);
                    }
                }
            }
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
