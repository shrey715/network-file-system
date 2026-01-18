/**
 * document_tests.c - Comprehensive tests for document layer
 *
 * Tests sentence parsing, locking, editing, and concurrency.
 */

#include "document.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include <unistd.h>

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Testing %s... ", #name); \
    fflush(stdout); \
    test_##name(); \
    printf("✓\n"); \
} while(0)

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        fprintf(stderr, "FAIL: %s != %s (%ld != %ld, line %d)\n", \
                #a, #b, (long)(a), (long)(b), __LINE__); \
        exit(1); \
    } \
} while(0)

#define ASSERT_STR_EQ(a, b) do { \
    if (strcmp((a), (b)) != 0) { \
        fprintf(stderr, "FAIL: '%s' != '%s' (line %d)\n", (a), (b), __LINE__); \
        exit(1); \
    } \
} while(0)

/* === Document Creation Tests === */

TEST(create_empty) {
    Document* doc = doc_create(NULL);
    assert(doc != NULL);
    ASSERT_EQ(doc_get_sentence_count(doc), 0);
    doc_destroy(doc);
}

TEST(create_single_sentence) {
    Document* doc = doc_create("Hello world.");
    assert(doc != NULL);
    ASSERT_EQ(doc_get_sentence_count(doc), 1);
    char* text = doc_get_text(doc);
    ASSERT_STR_EQ(text, "Hello world.");
    free(text);
    doc_destroy(doc);
}

TEST(create_multiple_sentences) {
    Document* doc = doc_create("First sentence. Second sentence! Third sentence?");
    assert(doc != NULL);
    ASSERT_EQ(doc_get_sentence_count(doc), 3);
    doc_destroy(doc);
}

TEST(create_with_trailing_text) {
    Document* doc = doc_create("A sentence. And more text without delimiter");
    assert(doc != NULL);
    ASSERT_EQ(doc_get_sentence_count(doc), 2);
    doc_destroy(doc);
}

/* === Sentence Parsing Tests === */

TEST(parse_period_delimiter) {
    Document* doc = doc_create("Test. Another test.");
    ASSERT_EQ(doc_get_sentence_count(doc), 2);
    
    int id = doc_get_sentence_by_index(doc, 0);
    char* s = doc_get_sentence(doc, id);
    ASSERT_STR_EQ(s, "Test.");
    free(s);
    
    doc_destroy(doc);
}

TEST(parse_exclamation_delimiter) {
    Document* doc = doc_create("Wow! Amazing!");
    ASSERT_EQ(doc_get_sentence_count(doc), 2);
    
    int id = doc_get_sentence_by_index(doc, 0);
    char* s = doc_get_sentence(doc, id);
    ASSERT_STR_EQ(s, "Wow!");
    free(s);
    
    doc_destroy(doc);
}

TEST(parse_question_delimiter) {
    Document* doc = doc_create("What? When? Where?");
    ASSERT_EQ(doc_get_sentence_count(doc), 3);
    doc_destroy(doc);
}

TEST(parse_with_whitespace) {
    Document* doc = doc_create("First.   Second.  Third.");
    ASSERT_EQ(doc_get_sentence_count(doc), 3);
    doc_destroy(doc);
}

TEST(parse_multiline) {
    Document* doc = doc_create("Line one.\nLine two.\nLine three.");
    ASSERT_EQ(doc_get_sentence_count(doc), 3);
    doc_destroy(doc);
}

/* === Sentence Locking Tests === */

TEST(lock_sentence) {
    Document* doc = doc_create("Test sentence.");
    int id = doc_get_sentence_by_index(doc, 0);
    
    int result = doc_lock_sentence(doc, id, "user1");
    ASSERT_EQ(result, 0);
    
    int is_locked;
    char locked_by[DOC_MAX_USERNAME];
    doc_get_lock_info(doc, id, &is_locked, locked_by);
    ASSERT_EQ(is_locked, 1);
    ASSERT_STR_EQ(locked_by, "user1");
    
    doc_unlock_sentence(doc, id, "user1");
    doc_destroy(doc);
}

TEST(lock_already_locked) {
    Document* doc = doc_create("Test sentence.");
    int id = doc_get_sentence_by_index(doc, 0);
    
    doc_lock_sentence(doc, id, "user1");
    int result = doc_lock_sentence(doc, id, "user2");
    ASSERT_EQ(result, -1); /* Should fail */
    
    doc_unlock_sentence(doc, id, "user1");
    doc_destroy(doc);
}

TEST(unlock_not_owner) {
    Document* doc = doc_create("Test sentence.");
    int id = doc_get_sentence_by_index(doc, 0);
    
    doc_lock_sentence(doc, id, "user1");
    int result = doc_unlock_sentence(doc, id, "user2");
    ASSERT_EQ(result, -1); /* Should fail */
    
    doc_unlock_sentence(doc, id, "user1");
    doc_destroy(doc);
}

TEST(lock_different_sentences) {
    Document* doc = doc_create("First. Second.");
    int id1 = doc_get_sentence_by_index(doc, 0);
    int id2 = doc_get_sentence_by_index(doc, 1);
    
    ASSERT_EQ(doc_lock_sentence(doc, id1, "user1"), 0);
    ASSERT_EQ(doc_lock_sentence(doc, id2, "user2"), 0);
    
    doc_unlock_sentence(doc, id1, "user1");
    doc_unlock_sentence(doc, id2, "user2");
    doc_destroy(doc);
}

/* === Sentence Editing Tests === */

TEST(edit_sentence_basic) {
    Document* doc = doc_create("Old text.");
    int id = doc_get_sentence_by_index(doc, 0);
    
    doc_lock_sentence(doc, id, "user1");
    int result = doc_edit_sentence(doc, id, "New text.", "user1");
    ASSERT_EQ(result, 0);
    
    char* text = doc_get_text(doc);
    ASSERT_STR_EQ(text, "New text.");
    free(text);
    
    doc_unlock_sentence(doc, id, "user1");
    doc_destroy(doc);
}

TEST(edit_without_lock) {
    Document* doc = doc_create("Test.");
    int id = doc_get_sentence_by_index(doc, 0);
    
    int result = doc_edit_sentence(doc, id, "Modified.", "user1");
    ASSERT_EQ(result, -1); /* Should fail */
    
    doc_destroy(doc);
}

TEST(edit_wrong_user) {
    Document* doc = doc_create("Test.");
    int id = doc_get_sentence_by_index(doc, 0);
    
    doc_lock_sentence(doc, id, "user1");
    int result = doc_edit_sentence(doc, id, "Modified.", "user2");
    ASSERT_EQ(result, -1); /* Should fail */
    
    doc_unlock_sentence(doc, id, "user1");
    doc_destroy(doc);
}

/* === Save/Load Tests === */

TEST(save_and_load) {
    const char* testfile = "/tmp/doc_test_save.txt";
    
    Document* doc = doc_create("Hello. World.");
    doc_save(doc, testfile);
    doc_destroy(doc);
    
    Document* loaded = doc_load(testfile);
    assert(loaded != NULL);
    ASSERT_EQ(doc_get_sentence_count(loaded), 2);
    
    char* text = doc_get_text(loaded);
    ASSERT_STR_EQ(text, "Hello. World.");
    free(text);
    
    doc_destroy(loaded);
    remove(testfile);
}

/* === Snapshot Tests === */

TEST(snapshot_restore) {
    Document* doc = doc_create("Original.");
    DocSnapshot* snap = doc_create_snapshot(doc);
    
    int id = doc_get_sentence_by_index(doc, 0);
    doc_lock_sentence(doc, id, "user1");
    doc_edit_sentence(doc, id, "Modified.", "user1");
    doc_unlock_sentence(doc, id, "user1");
    
    char* modified = doc_get_text(doc);
    ASSERT_STR_EQ(modified, "Modified.");
    free(modified);
    
    doc_restore_snapshot(doc, snap);
    char* restored = doc_get_text(doc);
    ASSERT_STR_EQ(restored, "Original.");
    free(restored);
    
    doc_destroy_snapshot(snap);
    doc_destroy(doc);
}

/* === Concurrent Access Tests === */

typedef struct {
    Document* doc;
    int sentence_id;
    const char* username;
    int success;
} ThreadArg;

static void* lock_thread(void* arg) {
    ThreadArg* ta = (ThreadArg*)arg;
    ta->success = (doc_lock_sentence(ta->doc, ta->sentence_id, ta->username) == 0);
    if (ta->success) {
        usleep(10000); /* Hold lock briefly */
        doc_unlock_sentence(ta->doc, ta->sentence_id, ta->username);
    }
    return NULL;
}

TEST(concurrent_lock_same_sentence) {
    Document* doc = doc_create("Test sentence.");
    int id = doc_get_sentence_by_index(doc, 0);
    
    ThreadArg arg1 = {doc, id, "user1", 0};
    ThreadArg arg2 = {doc, id, "user2", 0};
    
    pthread_t t1, t2;
    pthread_create(&t1, NULL, lock_thread, &arg1);
    pthread_create(&t2, NULL, lock_thread, &arg2);
    
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    
    /* Exactly one should succeed */
    ASSERT_EQ(arg1.success + arg2.success, 1);
    
    doc_destroy(doc);
}

TEST(concurrent_lock_different_sentences) {
    Document* doc = doc_create("First. Second.");
    int id1 = doc_get_sentence_by_index(doc, 0);
    int id2 = doc_get_sentence_by_index(doc, 1);
    
    ThreadArg arg1 = {doc, id1, "user1", 0};
    ThreadArg arg2 = {doc, id2, "user2", 0};
    
    pthread_t t1, t2;
    pthread_create(&t1, NULL, lock_thread, &arg1);
    pthread_create(&t2, NULL, lock_thread, &arg2);
    
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    
    /* Both should succeed */
    ASSERT_EQ(arg1.success, 1);
    ASSERT_EQ(arg2.success, 1);
    
    doc_destroy(doc);
}

/* === Edge Cases === */

TEST(stable_sentence_ids) {
    Document* doc = doc_create("First. Second. Third.");
    ASSERT_EQ(doc_get_sentence_count(doc), 3);
    
    /* Get sentence content before edit */
    int id = doc_get_sentence_by_index(doc, 1);
    char* before = doc_get_sentence(doc, id);
    assert(before != NULL);
    ASSERT_STR_EQ(before, "Second.");
    free(before);
    
    /* After editing, we can still access the document */
    doc_lock_sentence(doc, id, "user1");
    doc_edit_sentence(doc, id, " Modified.", "user1");
    doc_unlock_sentence(doc, id, "user1");
    
    /* Verify document still works */
    char* text = doc_get_text(doc);
    assert(text != NULL);
    assert(strlen(text) > 0);
    free(text);
    
    doc_destroy(doc);
}

TEST(empty_sentence_list) {
    Document* doc = doc_create("");
    ASSERT_EQ(doc_get_sentence_count(doc), 0);
    ASSERT_EQ(doc_get_sentence_by_index(doc, 0), -1);
    doc_destroy(doc);
}

/* === Main === */

int main(void) {
    printf("\n=== Document Layer Tests ===\n\n");
    
    printf("Creation:\n");
    RUN_TEST(create_empty);
    RUN_TEST(create_single_sentence);
    RUN_TEST(create_multiple_sentences);
    RUN_TEST(create_with_trailing_text);
    
    printf("\nParsing:\n");
    RUN_TEST(parse_period_delimiter);
    RUN_TEST(parse_exclamation_delimiter);
    RUN_TEST(parse_question_delimiter);
    RUN_TEST(parse_with_whitespace);
    RUN_TEST(parse_multiline);
    
    printf("\nLocking:\n");
    RUN_TEST(lock_sentence);
    RUN_TEST(lock_already_locked);
    RUN_TEST(unlock_not_owner);
    RUN_TEST(lock_different_sentences);
    
    printf("\nEditing:\n");
    RUN_TEST(edit_sentence_basic);
    RUN_TEST(edit_without_lock);
    RUN_TEST(edit_wrong_user);
    
    printf("\nSave/Load:\n");
    RUN_TEST(save_and_load);
    
    printf("\nSnapshot:\n");
    RUN_TEST(snapshot_restore);
    
    printf("\nConcurrency:\n");
    RUN_TEST(concurrent_lock_same_sentence);
    RUN_TEST(concurrent_lock_different_sentences);
    
    printf("\nEdge Cases:\n");
    RUN_TEST(stable_sentence_ids);
    RUN_TEST(empty_sentence_list);
    
    printf("\n=== All document layer tests passed! ===\n\n");
    return 0;
}
