/**
 * document.h - Document with Sentence Layer
 *
 * Provides sentence-level access to text stored in a piece table.
 * Supports concurrent editing with per-sentence locking.
 */

#ifndef DOCUMENT_H
#define DOCUMENT_H

#include "piece_table.h"
#include <pthread.h>

#define DOC_MAX_USERNAME 64
#define DOC_MAX_SENTENCES 1000

/**
 * SentenceBoundary - Describes a sentence's location and lock state
 */
typedef struct {
  int id;       /* Stable ID (survives re-parsing) */
  size_t start; /* Start offset in document */
  size_t end;   /* End offset (exclusive) */
  pthread_mutex_t lock;
  char locked_by[DOC_MAX_USERNAME];
  int is_locked;
} SentenceBoundary;

/**
 * Document - Text document with sentence-level access
 */
typedef struct {
  PieceTable *pt;              /* Underlying text storage */
  SentenceBoundary *sentences; /* Sentence boundary array */
  size_t sentence_count;
  int next_sentence_id;         /* For generating stable IDs */
  pthread_rwlock_t struct_lock; /* Protects sentence array structure */
} Document;

/**
 * doc_create - Create document from text content
 *
 * @param content  Initial text (can be NULL for empty)
 * @return New document, or NULL on error
 */
Document *doc_create(const char *content);

/**
 * doc_destroy - Free document resources
 *
 * Fails if any sentences are locked.
 *
 * @param doc  Document to destroy
 * @return 0 on success, -1 if locks held
 */
int doc_destroy(Document *doc);

/**
 * doc_get_text - Get full document text
 *
 * Caller must free returned string.
 *
 * @param doc  Document
 * @return Newly allocated string, or NULL on error
 */
char *doc_get_text(Document *doc);

/**
 * doc_get_sentence_count - Get number of sentences
 *
 * @param doc  Document
 * @return Sentence count
 */
size_t doc_get_sentence_count(Document *doc);

/**
 * doc_get_sentence - Get text of a specific sentence
 *
 * Caller must free returned string.
 *
 * @param doc          Document
 * @param sentence_id  Stable sentence ID
 * @return Sentence text, or NULL if not found
 */
char *doc_get_sentence(Document *doc, int sentence_id);

/**
 * doc_lock_sentence - Acquire edit lock on a sentence
 *
 * @param doc          Document
 * @param sentence_id  Stable sentence ID
 * @param username     User requesting lock
 * @return 0 on success, -1 if locked by another, -2 if not found
 */
int doc_lock_sentence(Document *doc, int sentence_id, const char *username);

/**
 * doc_unlock_sentence - Release edit lock
 *
 * @param doc          Document
 * @param sentence_id  Stable sentence ID
 * @param username     User releasing lock
 * @return 0 on success, -1 on error
 */
int doc_unlock_sentence(Document *doc, int sentence_id, const char *username);

/**
 * doc_edit_sentence - Replace sentence content
 *
 * Must hold lock on the sentence. Automatically re-parses boundaries.
 *
 * @param doc          Document
 * @param sentence_id  Stable sentence ID
 * @param new_text     New sentence text
 * @param username     User making edit (must hold lock)
 * @return 0 on success, -1 on error
 */
int doc_edit_sentence(Document *doc, int sentence_id, const char *new_text,
                      const char *username);

/**
 * doc_insert_sentence - Insert new sentence at position
 *
 * @param doc        Document
 * @param after_id   Insert after this sentence ID (-1 for beginning)
 * @param text       Text for new sentence (should end with delimiter)
 * @param username   User performing insert
 * @return New sentence ID, or -1 on error
 */
int doc_insert_sentence(Document *doc, int after_id, const char *text,
                        const char *username);

/**
 * doc_delete_sentence - Delete a sentence
 *
 * Must hold lock on the sentence.
 *
 * @param doc          Document
 * @param sentence_id  Stable sentence ID
 * @param username     User deleting (must hold lock)
 * @return 0 on success, -1 on error
 */
int doc_delete_sentence(Document *doc, int sentence_id, const char *username);

/**
 * doc_save - Write document to file
 *
 * @param doc       Document
 * @param filepath  Path to save to
 * @return 0 on success, -1 on error
 */
int doc_save(Document *doc, const char *filepath);

/**
 * doc_load - Load document from file
 *
 * @param filepath  Path to load from
 * @return New document, or NULL on error
 */
Document *doc_load(const char *filepath);

/**
 * doc_create_snapshot - Save state for undo
 *
 * @param doc  Document
 * @return Snapshot handle, or NULL on error
 */
typedef struct {
  PieceTableSnapshot *pt_snap;
  SentenceBoundary *sentences;
  size_t sentence_count;
} DocSnapshot;

DocSnapshot *doc_create_snapshot(Document *doc);

/**
 * doc_restore_snapshot - Restore from snapshot
 *
 * Fails if any sentences are locked.
 *
 * @param doc   Document
 * @param snap  Snapshot to restore
 * @return 0 on success, -1 on error
 */
int doc_restore_snapshot(Document *doc, DocSnapshot *snap);

/**
 * doc_destroy_snapshot - Free snapshot resources
 *
 * @param snap  Snapshot to destroy
 */
void doc_destroy_snapshot(DocSnapshot *snap);

/**
 * doc_get_sentence_by_index - Get sentence ID by 0-based index
 *
 * @param doc    Document
 * @param index  0-based index
 * @return Sentence ID, or -1 if out of bounds
 */
int doc_get_sentence_by_index(Document *doc, size_t index);

/**
 * doc_get_lock_info - Get lock information for display
 *
 * @param doc          Document
 * @param sentence_id  Sentence to query
 * @param is_locked    Output: 1 if locked, 0 if not
 * @param locked_by    Output: username if locked (buffer size DOC_MAX_USERNAME)
 * @return 0 on success, -1 if not found
 */
int doc_get_lock_info(Document *doc, int sentence_id, int *is_locked,
                      char *locked_by);

#endif /* DOCUMENT_H */
