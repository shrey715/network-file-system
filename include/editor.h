/**
 * editor.h - Terminal-based Text Editor
 *
 * Provides a nano-like editing interface for sentence-level editing.
 * Uses raw terminal mode for real-time key handling.
 */

#ifndef EDITOR_H
#define EDITOR_H

#include <termios.h>

#define EDITOR_VERSION "1.0"
#define EDITOR_TAB_STOP 4

/* Key codes */
enum EditorKey {
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  HOME_KEY,
  END_KEY,
  PAGE_UP,
  PAGE_DOWN,
  DEL_KEY
};

/* Editor mode */
typedef enum { MODE_NORMAL, MODE_INSERT, MODE_COMMAND } EditorMode;

/* Cursor position */
typedef struct {
  int row;
  int col;
} CursorPos;

/* Editor state */
typedef struct {
  /* Terminal */
  struct termios orig_termios;
  int screen_rows;
  int screen_cols;

  /* Content */
  char **lines; /* Array of line strings */
  int line_count;
  int line_capacity;

  /* Cursor */
  CursorPos cursor;
  int row_offset; /* Scroll offset */
  int col_offset;

  /* File info */
  char *filename;
  int modified;

  /* Sentence info */
  int sentence_id;
  char locked_by[64];
  int is_locked;

  /* Mode */
  EditorMode mode;
  char status_msg[256];
  int quit_requested;
} EditorState;

/**
 * editor_init - Initialize editor state
 *
 * @return New editor state, or NULL on error
 */
EditorState *editor_init(void);

/**
 * editor_destroy - Clean up editor state
 *
 * @param E  Editor state
 */
void editor_destroy(EditorState *E);

/**
 * editor_enable_raw_mode - Switch terminal to raw mode
 *
 * @param E  Editor state
 * @return 0 on success, -1 on error
 */
int editor_enable_raw_mode(EditorState *E);

/**
 * editor_disable_raw_mode - Restore terminal to cooked mode
 *
 * @param E  Editor state
 */
void editor_disable_raw_mode(EditorState *E);

/**
 * editor_load_content - Load text content for editing
 *
 * @param E        Editor state
 * @param content  Text to load
 * @return 0 on success, -1 on error
 */
int editor_load_content(EditorState *E, const char *content);

/**
 * editor_get_content - Get current editor content as string
 *
 * Caller must free returned string.
 *
 * @param E  Editor state
 * @return Content string, or NULL on error
 */
char *editor_get_content(EditorState *E);

/**
 * editor_run - Main editor loop
 *
 * Blocks until user quits (Ctrl+Q).
 *
 * @param E  Editor state
 */
void editor_run(EditorState *E);

/**
 * editor_set_status - Set status bar message
 *
 * @param E    Editor state
 * @param fmt  Printf-style format string
 */
void editor_set_status(EditorState *E, const char *fmt, ...);

/**
 * editor_set_file_info - Set filename and lock info for display
 *
 * @param E            Editor state
 * @param filename     Current filename
 * @param sentence_id  Sentence being edited
 * @param is_locked    Lock status
 * @param locked_by    User holding lock
 */
void editor_set_file_info(EditorState *E, const char *filename, int sentence_id,
                          int is_locked, const char *locked_by);

#endif /* EDITOR_H */
