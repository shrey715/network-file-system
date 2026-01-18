/**
 * piece_table_tests.c - Comprehensive tests for piece table
 *
 * Tests all piece table operations including edge cases.
 */

#include "piece_table.h"
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
        fprintf(stderr, "FAIL: %s != %s (line %d)\n", #a, #b, __LINE__); \
        exit(1); \
    } \
} while(0)

#define ASSERT_STR_EQ(a, b) do { \
    if (strcmp((a), (b)) != 0) { \
        fprintf(stderr, "FAIL: '%s' != '%s' (line %d)\n", (a), (b), __LINE__); \
        exit(1); \
    } \
} while(0)

/* === Basic Creation Tests === */

TEST(create_empty) {
    PieceTable* pt = pt_create(NULL);
    assert(pt != NULL);
    ASSERT_EQ(pt_length(pt), 0);
    char* text = pt_materialize(pt);
    ASSERT_STR_EQ(text, "");
    free(text);
    pt_destroy(pt);
}

TEST(create_with_content) {
    PieceTable* pt = pt_create("Hello, World!");
    assert(pt != NULL);
    ASSERT_EQ(pt_length(pt), 13);
    char* text = pt_materialize(pt);
    ASSERT_STR_EQ(text, "Hello, World!");
    free(text);
    pt_destroy(pt);
}

TEST(create_with_empty_string) {
    PieceTable* pt = pt_create("");
    assert(pt != NULL);
    ASSERT_EQ(pt_length(pt), 0);
    pt_destroy(pt);
}

/* === Insert Tests === */

TEST(insert_at_start) {
    PieceTable* pt = pt_create("World");
    pt_insert(pt, 0, "Hello ");
    char* text = pt_materialize(pt);
    ASSERT_STR_EQ(text, "Hello World");
    ASSERT_EQ(pt_length(pt), 11);
    free(text);
    pt_destroy(pt);
}

TEST(insert_at_end) {
    PieceTable* pt = pt_create("Hello");
    pt_insert(pt, 5, " World");
    char* text = pt_materialize(pt);
    ASSERT_STR_EQ(text, "Hello World");
    free(text);
    pt_destroy(pt);
}

TEST(insert_in_middle) {
    PieceTable* pt = pt_create("Helo");
    pt_insert(pt, 2, "l");
    char* text = pt_materialize(pt);
    ASSERT_STR_EQ(text, "Hello");
    free(text);
    pt_destroy(pt);
}

TEST(insert_into_empty) {
    PieceTable* pt = pt_create(NULL);
    pt_insert(pt, 0, "Hello");
    char* text = pt_materialize(pt);
    ASSERT_STR_EQ(text, "Hello");
    free(text);
    pt_destroy(pt);
}

TEST(multiple_inserts) {
    PieceTable* pt = pt_create("AC");
    pt_insert(pt, 1, "B");
    pt_insert(pt, 3, "D");
    pt_insert(pt, 0, "_");
    char* text = pt_materialize(pt);
    ASSERT_STR_EQ(text, "_ABCD");
    free(text);
    pt_destroy(pt);
}

TEST(insert_multiline) {
    PieceTable* pt = pt_create("Line1\nLine3");
    pt_insert(pt, 6, "Line2\n");
    char* text = pt_materialize(pt);
    ASSERT_STR_EQ(text, "Line1\nLine2\nLine3");
    free(text);
    pt_destroy(pt);
}

/* === Delete Tests === */

TEST(delete_from_start) {
    PieceTable* pt = pt_create("Hello World");
    pt_delete(pt, 0, 6);
    char* text = pt_materialize(pt);
    ASSERT_STR_EQ(text, "World");
    free(text);
    pt_destroy(pt);
}

TEST(delete_from_end) {
    PieceTable* pt = pt_create("Hello World");
    pt_delete(pt, 6, 5);
    char* text = pt_materialize(pt);
    ASSERT_STR_EQ(text, "Hello ");
    free(text);
    pt_destroy(pt);
}

TEST(delete_from_middle) {
    PieceTable* pt = pt_create("Helllo");
    pt_delete(pt, 3, 1);
    char* text = pt_materialize(pt);
    ASSERT_STR_EQ(text, "Hello");
    free(text);
    pt_destroy(pt);
}

TEST(delete_all) {
    PieceTable* pt = pt_create("Hello");
    pt_delete(pt, 0, 5);
    ASSERT_EQ(pt_length(pt), 0);
    char* text = pt_materialize(pt);
    ASSERT_STR_EQ(text, "");
    free(text);
    pt_destroy(pt);
}

TEST(delete_spanning_pieces) {
    PieceTable* pt = pt_create("Hello");
    pt_insert(pt, 5, " World");
    pt_delete(pt, 3, 5); /* Delete "lo Wo" */
    char* text = pt_materialize(pt);
    ASSERT_STR_EQ(text, "Helrld");
    free(text);
    pt_destroy(pt);
}

/* === Range Tests === */

TEST(get_range_start) {
    PieceTable* pt = pt_create("Hello World");
    char* range = pt_get_range(pt, 0, 5);
    ASSERT_STR_EQ(range, "Hello");
    free(range);
    pt_destroy(pt);
}

TEST(get_range_middle) {
    PieceTable* pt = pt_create("Hello World");
    char* range = pt_get_range(pt, 6, 5);
    ASSERT_STR_EQ(range, "World");
    free(range);
    pt_destroy(pt);
}

TEST(get_range_spanning) {
    PieceTable* pt = pt_create("Hello");
    pt_insert(pt, 5, " World");
    char* range = pt_get_range(pt, 3, 5);
    ASSERT_STR_EQ(range, "lo Wo");
    free(range);
    pt_destroy(pt);
}

/* === Snapshot Tests === */

TEST(snapshot_and_restore) {
    PieceTable* pt = pt_create("Hello");
    PieceTableSnapshot* snap = pt_snapshot(pt);
    
    pt_insert(pt, 5, " World");
    char* modified = pt_materialize(pt);
    ASSERT_STR_EQ(modified, "Hello World");
    free(modified);
    
    pt_restore(pt, snap);
    char* restored = pt_materialize(pt);
    ASSERT_STR_EQ(restored, "Hello");
    free(restored);
    
    pt_snapshot_destroy(snap);
    pt_destroy(pt);
}

TEST(multiple_snapshots) {
    PieceTable* pt = pt_create("A");
    PieceTableSnapshot* snap1 = pt_snapshot(pt);
    
    pt_insert(pt, 1, "B");
    PieceTableSnapshot* snap2 = pt_snapshot(pt);
    
    pt_insert(pt, 2, "C");
    char* text = pt_materialize(pt);
    ASSERT_STR_EQ(text, "ABC");
    free(text);
    
    pt_restore(pt, snap2);
    text = pt_materialize(pt);
    ASSERT_STR_EQ(text, "AB");
    free(text);
    
    pt_restore(pt, snap1);
    text = pt_materialize(pt);
    ASSERT_STR_EQ(text, "A");
    free(text);
    
    pt_snapshot_destroy(snap1);
    pt_snapshot_destroy(snap2);
    pt_destroy(pt);
}

/* === Edge Cases === */

TEST(insert_empty_string) {
    PieceTable* pt = pt_create("Hello");
    int result = pt_insert(pt, 2, "");
    ASSERT_EQ(result, 0);
    ASSERT_EQ(pt_length(pt), 5);
    pt_destroy(pt);
}

TEST(delete_zero_length) {
    PieceTable* pt = pt_create("Hello");
    int result = pt_delete(pt, 2, 0);
    ASSERT_EQ(result, 0);
    ASSERT_EQ(pt_length(pt), 5);
    pt_destroy(pt);
}

TEST(delete_past_end) {
    PieceTable* pt = pt_create("Hello");
    pt_delete(pt, 3, 100); /* Should clamp to actual length */
    char* text = pt_materialize(pt);
    ASSERT_STR_EQ(text, "Hel");
    free(text);
    pt_destroy(pt);
}

TEST(large_content) {
    char* large = malloc(10001);
    for (int i = 0; i < 10000; i++) large[i] = 'x';
    large[10000] = '\0';
    
    PieceTable* pt = pt_create(large);
    ASSERT_EQ(pt_length(pt), 10000);
    
    pt_insert(pt, 5000, "MIDDLE");
    ASSERT_EQ(pt_length(pt), 10006);
    
    char* text = pt_materialize(pt);
    ASSERT_EQ(strlen(text), 10006);
    free(text);
    
    free(large);
    pt_destroy(pt);
}

TEST(many_small_inserts) {
    PieceTable* pt = pt_create("");
    for (int i = 0; i < 100; i++) {
        pt_insert(pt, pt_length(pt), "x");
    }
    ASSERT_EQ(pt_length(pt), 100);
    pt_destroy(pt);
}

/* === Main === */

int main(void) {
    printf("\n=== Piece Table Tests ===\n\n");
    
    printf("Creation:\n");
    RUN_TEST(create_empty);
    RUN_TEST(create_with_content);
    RUN_TEST(create_with_empty_string);
    
    printf("\nInsert:\n");
    RUN_TEST(insert_at_start);
    RUN_TEST(insert_at_end);
    RUN_TEST(insert_in_middle);
    RUN_TEST(insert_into_empty);
    RUN_TEST(multiple_inserts);
    RUN_TEST(insert_multiline);
    
    printf("\nDelete:\n");
    RUN_TEST(delete_from_start);
    RUN_TEST(delete_from_end);
    RUN_TEST(delete_from_middle);
    RUN_TEST(delete_all);
    RUN_TEST(delete_spanning_pieces);
    
    printf("\nRange:\n");
    RUN_TEST(get_range_start);
    RUN_TEST(get_range_middle);
    RUN_TEST(get_range_spanning);
    
    printf("\nSnapshot:\n");
    RUN_TEST(snapshot_and_restore);
    RUN_TEST(multiple_snapshots);
    
    printf("\nEdge Cases:\n");
    RUN_TEST(insert_empty_string);
    RUN_TEST(delete_zero_length);
    RUN_TEST(delete_past_end);
    RUN_TEST(large_content);
    RUN_TEST(many_small_inserts);
    
    printf("\n=== All piece table tests passed! ===\n\n");
    return 0;
}
