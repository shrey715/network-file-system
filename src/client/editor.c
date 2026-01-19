/**
 * editor.c - Terminal-based Text Editor
 *
 * Nano-like editing interface using raw terminal mode.
 */

#include "editor.h"
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <time.h>

/* ANSI escape codes */
#define ESC "\x1b"
#define CLEAR_SCREEN ESC "[2J"
#define CURSOR_HOME ESC "[H"
#define CURSOR_HIDE ESC "[?25l"
#define CURSOR_SHOW ESC "[?25h"
#define ALT_SCREEN_ON ESC "[?1049h"
#define ALT_SCREEN_OFF ESC "[?1049l"
#define INVERT ESC "[7m"
#define RESET ESC "[0m"
#define BOLD ESC "[1m"
#define DIM ESC "[2m"
#define CYAN ESC "[36m"
#define YELLOW ESC "[33m"
#define GREEN ESC "[32m"

/* Control key macro */
#define CTRL_KEY(k) ((k) & 0x1f)

/* Append buffer for efficient screen writes */
typedef struct {
    char* buf;
    int len;
    int cap;
} AppendBuf;

static void ab_init(AppendBuf* ab) {
    ab->buf = NULL;
    ab->len = 0;
    ab->cap = 0;
}

static void ab_append(AppendBuf* ab, const char* s, int len) {
    if (ab->len + len >= ab->cap) {
        int new_cap = ab->cap ? ab->cap * 2 : 256;
        while (new_cap < ab->len + len + 1) new_cap *= 2;
        ab->buf = realloc(ab->buf, new_cap);
        ab->cap = new_cap;
    }
    memcpy(ab->buf + ab->len, s, len);
    ab->len += len;
    ab->buf[ab->len] = '\0';
}

static void ab_free(AppendBuf* ab) {
    free(ab->buf);
    ab->buf = NULL;
    ab->len = ab->cap = 0;
}

/* Get window size */
static int get_window_size(int* rows, int* cols) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        return -1;
    }
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
}

EditorState* editor_init(void) {
    EditorState* E = calloc(1, sizeof(EditorState));
    if (!E) return NULL;

    E->lines = NULL;
    E->line_count = 0;
    E->line_capacity = 0;
    E->cursor.row = 0;
    E->cursor.col = 0;
    E->row_offset = 0;
    E->sub_row_offset = 0;
    E->col_offset = 0;
    E->filename = NULL;
    E->modified = 0;
    E->sentence_id = -1;
    E->is_locked = 0;
    E->locked_by[0] = '\0';
    E->mode = MODE_INSERT;
    E->status_msg[0] = '\0';
    E->quit_requested = 0;
    E->save_requested = 0;
    E->read_only = 0;

    if (get_window_size(&E->screen_rows, &E->screen_cols) == -1) {
        E->screen_rows = 24;
        E->screen_cols = 80;
    }
    E->screen_rows -= 2; /* Reserve for status bar and help line */

    return E;
}

void editor_destroy(EditorState* E) {
    if (!E) return;
    for (int i = 0; i < E->line_count; i++) {
        free(E->lines[i]);
    }
    free(E->lines);
    free(E->filename);
    free(E);
}



/* Add a line to the editor */
static void editor_append_line(EditorState* E, const char* s, int len) {
    if (E->line_count >= E->line_capacity) {
        E->line_capacity = E->line_capacity ? E->line_capacity * 2 : 16;
        E->lines = realloc(E->lines, E->line_capacity * sizeof(char*));
    }
    E->lines[E->line_count] = malloc(len + 1);
    memcpy(E->lines[E->line_count], s, len);
    E->lines[E->line_count][len] = '\0';
    E->line_count++;
}

int editor_load_content(EditorState* E, const char* content) {
    if (!E) return -1;

    /* Clear existing lines */
    for (int i = 0; i < E->line_count; i++) {
        free(E->lines[i]);
    }
    free(E->lines);
    E->lines = NULL;
    E->line_count = 0;
    E->line_capacity = 0;

    if (!content || !*content) {
        editor_append_line(E, "", 0);
        return 0;
    }

    /* Split by newlines */
    const char* line_start = content;
    const char* p = content;
    while (*p) {
        if (*p == '\n') {
            editor_append_line(E, line_start, p - line_start);
            line_start = p + 1;
        }
        p++;
    }
    /* Last line (may not end with newline) */
    if (p > line_start || E->line_count == 0) {
        editor_append_line(E, line_start, p - line_start);
    }

    E->cursor.row = 0;
    E->cursor.col = 0;
    E->modified = 0;
    return 0;
}

char* editor_get_content(EditorState* E) {
    if (!E) return NULL;

    /* Calculate total size */
    size_t total = 0;
    for (int i = 0; i < E->line_count; i++) {
        total += strlen(E->lines[i]) + 1; /* +1 for newline */
    }

    char* content = malloc(total + 1);
    if (!content) return NULL;

    char* p = content;
    for (int i = 0; i < E->line_count; i++) {
        size_t len = strlen(E->lines[i]);
        memcpy(p, E->lines[i], len);
        p += len;
        if (i < E->line_count - 1) {
            *p++ = '\n';
        }
    }
    *p = '\0';

    return content;
}

void editor_set_status(EditorState* E, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(E->status_msg, sizeof(E->status_msg), fmt, ap);
    va_end(ap);
}

void editor_set_file_info(EditorState* E, const char* filename, int sentence_id,
                          int is_locked, const char* locked_by) {
    free(E->filename);
    E->filename = filename ? strdup(filename) : NULL;
    E->sentence_id = sentence_id;
    E->is_locked = is_locked;
    if (locked_by) {
        strncpy(E->locked_by, locked_by, sizeof(E->locked_by) - 1);
        E->locked_by[sizeof(E->locked_by) - 1] = '\0';
    } else {
        E->locked_by[0] = '\0';
    }
}

/* Read a key from terminal */
static int editor_read_key(void) {
    int nread;
    char c;

    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) return -1;
    }

    if (c == '\x1b') {
        char seq[3];
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
                if (seq[2] == '~') {
                    switch (seq[1]) {
                        case '1': return HOME_KEY;
                        case '3': return DEL_KEY;
                        case '4': return END_KEY;
                        case '5': return PAGE_UP;
                        case '6': return PAGE_DOWN;
                        case '7': return HOME_KEY;
                        case '8': return END_KEY;
                    }
                }
            } else {
                switch (seq[1]) {
                    case 'A': return ARROW_UP;
                    case 'B': return ARROW_DOWN;
                    case 'C': return ARROW_RIGHT;
                    case 'D': return ARROW_LEFT;
                    case 'H': return HOME_KEY;
                    case 'F': return END_KEY;
                }
            }
        } else if (seq[0] == 'O') {
            switch (seq[1]) {
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
            }
        }
        return '\x1b';
    }
    return c;
}

/* Move cursor */
static void editor_move_cursor(EditorState* E, int key) {
    char* row = (E->cursor.row < E->line_count) ? E->lines[E->cursor.row] : NULL;
    int rowlen = row ? strlen(row) : 0;

    switch (key) {
        case ARROW_LEFT:
            if (E->cursor.col > 0) {
                E->cursor.col--;
            } else if (E->cursor.row > 0) {
                E->cursor.row--;
                E->cursor.col = strlen(E->lines[E->cursor.row]);
            }
            break;
        case ARROW_RIGHT:
            if (E->cursor.col < rowlen) {
                E->cursor.col++;
            } else if (E->cursor.row < E->line_count - 1) {
                E->cursor.row++;
                E->cursor.col = 0;
            }
            break;
        case ARROW_UP:
            if (E->cursor.col >= E->screen_cols) {
                E->cursor.col -= E->screen_cols;
            } else if (E->cursor.row > 0) {
                E->cursor.row--;
                if (E->cursor.row < E->line_count) {
                    E->cursor.col = strlen(E->lines[E->cursor.row]);
                }
            }
            break;
        case ARROW_DOWN:
            if (row) {
                 int max_sub = (rowlen == 0) ? 0 : (rowlen - 1) / E->screen_cols;
                 int curr_sub = E->cursor.col / E->screen_cols;
                 
                 if (curr_sub < max_sub) {
                     E->cursor.col += E->screen_cols;
                 } else if (E->cursor.row < E->line_count - 1) {
                     E->cursor.row++;
                     E->cursor.col = 0;
                 }
            }
            break;
    }

    /* Snap cursor to line length */
    row = (E->cursor.row < E->line_count) ? E->lines[E->cursor.row] : NULL;
    rowlen = row ? strlen(row) : 0;
    if (E->cursor.col > rowlen) E->cursor.col = rowlen;
}

/* Insert character */
static void editor_insert_char(EditorState* E, int c) {
    if (E->cursor.row >= E->line_count) return;

    char* row = E->lines[E->cursor.row];
    int len = strlen(row);
    char* new_row = malloc(len + 2);
    memcpy(new_row, row, E->cursor.col);
    new_row[E->cursor.col] = c;
    memcpy(new_row + E->cursor.col + 1, row + E->cursor.col, len - E->cursor.col + 1);
    free(E->lines[E->cursor.row]);
    E->lines[E->cursor.row] = new_row;
    E->cursor.col++;
    E->modified = 1;
}

/* Delete character (backspace) */
static void editor_delete_char(EditorState* E) {
    if (E->cursor.row >= E->line_count) return;
    if (E->cursor.col == 0 && E->cursor.row == 0) return;

    if (E->cursor.col > 0) {
        char* row = E->lines[E->cursor.row];
        int len = strlen(row);
        memmove(row + E->cursor.col - 1, row + E->cursor.col, len - E->cursor.col + 1);
        E->cursor.col--;
        E->modified = 1;
    } else {
        /* Join with previous line */
        int prev_len = strlen(E->lines[E->cursor.row - 1]);
        char* curr = E->lines[E->cursor.row];
        int curr_len = strlen(curr);
        char* combined = malloc(prev_len + curr_len + 1);
        memcpy(combined, E->lines[E->cursor.row - 1], prev_len);
        memcpy(combined + prev_len, curr, curr_len + 1);
        free(E->lines[E->cursor.row - 1]);
        free(E->lines[E->cursor.row]);
        E->lines[E->cursor.row - 1] = combined;
        memmove(&E->lines[E->cursor.row], &E->lines[E->cursor.row + 1],
                (E->line_count - E->cursor.row - 1) * sizeof(char*));
        E->line_count--;
        E->cursor.row--;
        E->cursor.col = prev_len;
        E->modified = 1;
    }
}

/* Insert newline */
static void editor_insert_newline(EditorState* E) {
    if (E->line_count >= E->line_capacity) {
        E->line_capacity = E->line_capacity ? E->line_capacity * 2 : 16;
        E->lines = realloc(E->lines, E->line_capacity * sizeof(char*));
    }

    char* row = E->lines[E->cursor.row];
    char* new_line = strdup(row + E->cursor.col);
    row[E->cursor.col] = '\0';
    E->lines[E->cursor.row] = realloc(row, E->cursor.col + 1);

    memmove(&E->lines[E->cursor.row + 2], &E->lines[E->cursor.row + 1],
            (E->line_count - E->cursor.row - 1) * sizeof(char*));
    E->lines[E->cursor.row + 1] = new_line;
    E->line_count++;
    E->cursor.row++;
    E->cursor.col = 0;
    E->modified = 1;
}

/* Scroll to keep cursor visible */
static void editor_scroll(EditorState* E) {
    if (E->cursor.row < 0) E->cursor.row = 0;
    if (E->cursor.row >= E->line_count) E->cursor.row = E->line_count - 1;

    // 1. Initial Check: If cursor is above the top visible line, scroll up
    if (E->cursor.row < E->row_offset) {
        E->row_offset = E->cursor.row;
        E->sub_row_offset = 0;
    } else if (E->cursor.row == E->row_offset) {
        // Cursor is on the top line. Ensure sub-row is visible.
        int cursor_sub_row = E->cursor.col / E->screen_cols;
        if (cursor_sub_row < E->sub_row_offset) {
            E->sub_row_offset = cursor_sub_row;
        }
    }

    // 2. Check if cursor is below the bottom visible part
    // We simulate rendering to see if the cursor falls within the screen.
    // If not, we scroll down (advance row_offset/sub_row_offset) until it fits.
    
    while (1) {
        int screen_y = 0;
        int current_row = E->row_offset;
        int current_sub = E->sub_row_offset;
        int cursor_is_visible = 0;

        // Simulate drawing screen rows
        while (screen_y < E->screen_rows && current_row < E->line_count) {
            char* line = E->lines[current_row];
            int len = strlen(line);
            int total_subs = (len == 0) ? 1 : (len + E->screen_cols - 1) / E->screen_cols;
            
            // Check if we hit the cursor row
            if (current_row == E->cursor.row) {
                int cursor_sub = E->cursor.col / E->screen_cols;
                
                // Cursor must be on or after the current visible sub-row
                if (cursor_sub >= current_sub) {
                    // Check if the specific sub-row of the cursor fits on screen
                    int v_offset = cursor_sub - current_sub;
                    if (screen_y + v_offset < E->screen_rows) {
                        cursor_is_visible = 1;
                    }
                }
                break; // Found the row, stop simulation
            }

            // Advance visible area by number of sub-rows remaining in this line
            int remaining_subs = total_subs - current_sub;
            screen_y += remaining_subs;
            
            // Move to next line
            current_row++;
            current_sub = 0;
        }

        if (cursor_is_visible) break;

        // Cursor not visible, scroll down one visual line
        char* line = E->lines[E->row_offset];
        int len = strlen(line);
        int total_top_subs = (len == 0) ? 1 : (len + E->screen_cols - 1) / E->screen_cols;

        E->sub_row_offset++;
        if (E->sub_row_offset >= total_top_subs) {
            E->row_offset++;
            E->sub_row_offset = 0;
        }
        
        // Safety: If we scrolled past cursor (shouldn't happen), reset strict
        if (E->row_offset > E->cursor.row) {
             E->row_offset = E->cursor.row;
             E->sub_row_offset = E->cursor.col / E->screen_cols; 
             // Align so cursor is exactly at top to be safe, though this might be aggressive
             // But actually above logic should catch it before passing.
             break;
        }
    }
}

/* Draw screen */
static void editor_draw(EditorState* E) {
    editor_scroll(E);

    AppendBuf ab;
    ab_init(&ab);

    ab_append(&ab, CURSOR_HIDE, 6);
    ab_append(&ab, CURSOR_HOME, 3);

    /* Draw lines with word wrapping */
    int screen_y = 0;
    int file_line = E->row_offset;
    int line_sub_row = E->sub_row_offset;  // Start exactly where scroll says
    
    while (screen_y < E->screen_rows) {
        if (file_line < E->line_count) {
            char* line = E->lines[file_line];
            int line_len = strlen(line);
            
            if (line_len == 0) {
                // Empty line
                ab_append(&ab, ESC "[K", 3);
                ab_append(&ab, "\r\n", 2);
                screen_y++;
                file_line++;
                line_sub_row = 0;
            } else {
                // Calculate which portion of the line to show
                int start_col = line_sub_row * E->screen_cols;
                
                if (start_col < line_len) {
                    int remaining = line_len - start_col;
                    int to_draw = (remaining > E->screen_cols) ? E->screen_cols : remaining;
                    ab_append(&ab, line + start_col, to_draw);
                }
                
                ab_append(&ab, ESC "[K", 3);
                ab_append(&ab, "\r\n", 2);
                screen_y++;
                
                // Check if more sub-rows needed for this line
                int next_start = (line_sub_row + 1) * E->screen_cols;
                if (next_start < line_len) {
                    line_sub_row++;
                } else {
                    file_line++;
                    line_sub_row = 0;
                }
            }
        } else {
            ab_append(&ab, DIM "~" RESET, strlen(DIM) + 1 + strlen(RESET));
            ab_append(&ab, ESC "[K", 3);
            ab_append(&ab, "\r\n", 2);
            screen_y++;
            file_line++;
        }
    }

    /* Status bar */
    ab_append(&ab, INVERT, strlen(INVERT));
    char status[256];
    char rstatus[80];
    int slen = snprintf(status, sizeof(status), " %.40s%s | Sentence %d",
                        E->filename ? E->filename : "[New]",
                        E->modified ? " [+]" : "",
                        E->sentence_id);
    int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d ",
                        E->cursor.row + 1, E->line_count);
    if (slen > E->screen_cols) slen = E->screen_cols;
    ab_append(&ab, status, slen);
    while (slen < E->screen_cols - rlen) {
        ab_append(&ab, " ", 1);
        slen++;
    }
    ab_append(&ab, rstatus, rlen);
    ab_append(&ab, RESET "\r\n", strlen(RESET) + 2);

    /* Help line */
    ab_append(&ab, CYAN, strlen(CYAN));
    const char* help = E->read_only ? "^Q Quit" : "^S Save | ^Q Quit | ^Z Undo";
    ab_append(&ab, help, strlen(help));
    ab_append(&ab, ESC "[K" RESET, 3 + strlen(RESET));

    /* Move cursor */
    char buf[32];
    int len = snprintf(buf, sizeof(buf), ESC "[%d;%dH",
                       E->cursor.row - E->row_offset + 1,
                       E->cursor.col - E->col_offset + 1);
    ab_append(&ab, buf, len);
    ab_append(&ab, CURSOR_SHOW, 6);

    write(STDOUT_FILENO, ab.buf, ab.len);
    ab_free(&ab);
}

/* Process keypress */
static void editor_process_key(EditorState* E) {
    int c = editor_read_key();
    if (c == -1) return;

    switch (c) {
        case CTRL_KEY('q'):
            if (E->modified) {
                editor_set_status(E, "Unsaved changes! Press Ctrl+Q again to quit.");
                static int quit_times = 1;
                if (quit_times > 0) {
                    quit_times--;
                    return;
                }
            }
            E->quit_requested = 1;
            break;

        case CTRL_KEY('s'):
            if (E->read_only) {
                editor_set_status(E, "Read-only mode - cannot save");
            } else {
                E->save_requested = 1;
                E->quit_requested = 1;
                editor_set_status(E, "Saving...");
            }
            break;

        case CTRL_KEY('z'):
            editor_set_status(E, "Undo (not implemented in standalone mode)");
            break;

        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            editor_move_cursor(E, c);
            break;

        case HOME_KEY:
            E->cursor.col = 0;
            break;

        case END_KEY:
            if (E->cursor.row < E->line_count) {
                E->cursor.col = strlen(E->lines[E->cursor.row]);
            }
            break;

        case PAGE_UP:
        case PAGE_DOWN:
            {
                int times = E->screen_rows;
                while (times--) {
                    editor_move_cursor(E, c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
                }
            }
            break;

        case '\r': /* Enter */
            if (!E->read_only) editor_insert_newline(E);
            break;

        case 127: /* Backspace */
        case CTRL_KEY('h'):
            if (!E->read_only) editor_delete_char(E);
            break;

        case DEL_KEY:
            if (!E->read_only) {
                editor_move_cursor(E, ARROW_RIGHT);
                editor_delete_char(E);
            }
            break;

        case CTRL_KEY('l'):
        case '\x1b':
            break;

        default:
            if (!iscntrl(c) && !E->read_only) {
                editor_insert_char(E, c);
            }
            break;
    }
}

/**
 * editor_enable_live_updates - Enable polling for file changes
 */
void editor_enable_live_updates(EditorState* E, const char* ss_ip, int ss_port,
                                const char* username) {
    if (!E) return;
    E->live_updates_enabled = 1;
    strncpy(E->ss_ip, ss_ip, sizeof(E->ss_ip) - 1);
    E->ss_ip[sizeof(E->ss_ip) - 1] = '\0';
    E->ss_port = ss_port;
    strncpy(E->username, username, sizeof(E->username) - 1);
    E->username[sizeof(E->username) - 1] = '\0';
    E->last_mtime = 0;
}

/**
 * editor_poll_updates - Check if file has been modified and refresh if so
 * @return 1 if content was refreshed, 0 otherwise
 */
static int editor_poll_updates(EditorState* E) {
    if (!E || !E->live_updates_enabled || !E->filename) return 0;
    
    // Connect to storage server
    int sock = connect_to_server(E->ss_ip, E->ss_port);
    if (sock < 0) return 0;
    
    // Request mtime check
    MessageHeader header;
    init_message_header(&header, MSG_REQUEST, OP_SS_CHECK_MTIME, E->username);
    strncpy(header.filename, E->filename, MAX_FILENAME - 1);
    send_message(sock, &header, NULL);
    
    char* payload = NULL;
    recv_message(sock, &header, &payload);
    close(sock);
    
    if (header.msg_type != MSG_RESPONSE || !payload) {
        if (payload) free(payload);
        return 0;
    }
    
    long remote_mtime = atol(payload);
    free(payload);
    
    // First poll - just record the mtime
    if (E->last_mtime == 0) {
        E->last_mtime = remote_mtime;
        return 0;
    }
    
    // No change
    if (remote_mtime <= E->last_mtime) {
        return 0;
    }
    
    // File changed! Fetch new content
    E->last_mtime = remote_mtime;
    
    sock = connect_to_server(E->ss_ip, E->ss_port);
    if (sock < 0) return 0;
    
    init_message_header(&header, MSG_REQUEST, OP_SS_READ, E->username);
    strncpy(header.filename, E->filename, MAX_FILENAME - 1);
    send_message(sock, &header, NULL);
    
    char* content = NULL;
    recv_message(sock, &header, &content);
    close(sock);
    
    if (header.msg_type == MSG_RESPONSE && content) {
        // Clear and reload content
        for (int i = 0; i < E->line_count; i++) {
            free(E->lines[i]);
        }
        E->line_count = 0;
        
        editor_load_content(E, content);
        editor_set_status(E, "[LIVE] Content updated by another user");
        free(content);
        return 1;
    }
    
    if (content) free(content);
    return 0;
}

/**
 * editor_run_with_live_updates - Main loop with periodic polling
 */
void editor_run(EditorState* E) {
    if (!E) return;

    /* Enter alternate screen buffer (like nano/vim) */
    write(STDOUT_FILENO, ALT_SCREEN_ON, strlen(ALT_SCREEN_ON));
    write(STDOUT_FILENO, CLEAR_SCREEN CURSOR_HOME, strlen(CLEAR_SCREEN CURSOR_HOME));

    time_t last_poll = time(NULL);
    
    while (!E->quit_requested) {
        editor_draw(E);
        
        // Use select() to wait for input with timeout for live updates
        if (E->live_updates_enabled) {
            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(STDIN_FILENO, &readfds);
            
            struct timeval tv;
            tv.tv_sec = 0;
            tv.tv_usec = 100000; // 100ms timeout
            
            int ready = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &tv);
            
            if (ready > 0 && FD_ISSET(STDIN_FILENO, &readfds)) {
                editor_process_key(E);
            }
            
            // Poll for updates every 2 seconds
            time_t now = time(NULL);
            if (now - last_poll >= 2) {
                editor_poll_updates(E);
                last_poll = now;
            }
        } else {
            editor_process_key(E);
        }
    }

    /* Exit alternate screen buffer - restores original terminal content */
    write(STDOUT_FILENO, ALT_SCREEN_OFF, strlen(ALT_SCREEN_OFF));
}
