/**
 * editor_tests.c - Tests for terminal editor functionality
 *
 * Tests editor state management and content manipulation.
 * Note: These are unit tests for logic, not interactive terminal tests.
 */

#include "editor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Testing %s... ", #name); \
    fflush(stdout); \
    test_##name(); \
    printf("✓\n"); \
} while(0)

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        fprintf(stderr, "FAIL: %s != %s (%d != %d, line %d)\n", \
                #a, #b, (int)(a), (int)(b), __LINE__); \
        exit(1); \
    } \
} while(0)

#define ASSERT_STR_EQ(a, b) do { \
    if (strcmp((a), (b)) != 0) { \
        fprintf(stderr, "FAIL: '%s' != '%s' (line %d)\n", (a), (b), __LINE__); \
        exit(1); \
    } \
} while(0)

/* === Editor Initialization Tests === */

TEST(init_destroy) {
    EditorState* E = editor_init();
    assert(E != NULL);
    ASSERT_EQ(E->line_count, 0);
    ASSERT_EQ(E->cursor.row, 0);
    ASSERT_EQ(E->cursor.col, 0);
    ASSERT_EQ(E->modified, 0);
    editor_destroy(E);
}

/* === Content Loading Tests === */

TEST(load_empty) {
    EditorState* E = editor_init();
    editor_load_content(E, "");
    ASSERT_EQ(E->line_count, 1);
    char* content = editor_get_content(E);
    ASSERT_STR_EQ(content, "");
    free(content);
    editor_destroy(E);
}

TEST(load_single_line) {
    EditorState* E = editor_init();
    editor_load_content(E, "Hello World");
    ASSERT_EQ(E->line_count, 1);
    char* content = editor_get_content(E);
    ASSERT_STR_EQ(content, "Hello World");
    free(content);
    editor_destroy(E);
}

TEST(load_multiple_lines) {
    EditorState* E = editor_init();
    editor_load_content(E, "Line 1\nLine 2\nLine 3");
    ASSERT_EQ(E->line_count, 3);
    char* content = editor_get_content(E);
    ASSERT_STR_EQ(content, "Line 1\nLine 2\nLine 3");
    free(content);
    editor_destroy(E);
}

TEST(load_trailing_newline) {
    EditorState* E = editor_init();
    editor_load_content(E, "Line 1\nLine 2");
    ASSERT_EQ(E->line_count, 2);
    editor_destroy(E);
}

/* === Content Retrieval Tests === */

TEST(get_content_empty) {
    EditorState* E = editor_init();
    editor_load_content(E, NULL);
    char* content = editor_get_content(E);
    ASSERT_STR_EQ(content, "");
    free(content);
    editor_destroy(E);
}

TEST(get_content_multiline) {
    EditorState* E = editor_init();
    editor_load_content(E, "A\nB\nC");
    char* content = editor_get_content(E);
    ASSERT_STR_EQ(content, "A\nB\nC");
    free(content);
    editor_destroy(E);
}

/* === File Info Tests === */

TEST(set_file_info) {
    EditorState* E = editor_init();
    editor_set_file_info(E, "test.txt", 5, 1, "user1");
    ASSERT_STR_EQ(E->filename, "test.txt");
    ASSERT_EQ(E->sentence_id, 5);
    ASSERT_EQ(E->is_locked, 1);
    ASSERT_STR_EQ(E->locked_by, "user1");
    editor_destroy(E);
}

TEST(set_status) {
    EditorState* E = editor_init();
    editor_set_status(E, "Status: %d", 42);
    ASSERT_STR_EQ(E->status_msg, "Status: 42");
    editor_destroy(E);
}

/* === Cursor Tests === */

TEST(cursor_initial_position) {
    EditorState* E = editor_init();
    editor_load_content(E, "Hello\nWorld");
    ASSERT_EQ(E->cursor.row, 0);
    ASSERT_EQ(E->cursor.col, 0);
    editor_destroy(E);
}

/* === Modified Flag Tests === */

TEST(modified_flag_initial) {
    EditorState* E = editor_init();
    editor_load_content(E, "Test");
    ASSERT_EQ(E->modified, 0);
    editor_destroy(E);
}

/* === Main === */

int main(void) {
    printf("\n=== Editor Tests ===\n\n");
    
    printf("Initialization:\n");
    RUN_TEST(init_destroy);
    
    printf("\nContent Loading:\n");
    RUN_TEST(load_empty);
    RUN_TEST(load_single_line);
    RUN_TEST(load_multiple_lines);
    RUN_TEST(load_trailing_newline);
    
    printf("\nContent Retrieval:\n");
    RUN_TEST(get_content_empty);
    RUN_TEST(get_content_multiline);
    
    printf("\nFile Info:\n");
    RUN_TEST(set_file_info);
    RUN_TEST(set_status);
    
    printf("\nCursor:\n");
    RUN_TEST(cursor_initial_position);
    
    printf("\nModified Flag:\n");
    RUN_TEST(modified_flag_initial);
    
    printf("\n=== All editor tests passed! ===\n\n");
    return 0;
}
