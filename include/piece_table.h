/**
 * piece_table.h - Piece Table Text Buffer
 *
 * Efficient text storage using the piece table data structure.
 * Supports O(1) insertions and deletions without data copying.
 */

#ifndef PIECE_TABLE_H
#define PIECE_TABLE_H

#include <pthread.h>
#include <stddef.h>

/* Buffer type: original file content or appended text */
typedef enum { PT_BUFFER_ORIGINAL, PT_BUFFER_ADD } PieceBufferType;

/* A piece references a contiguous segment in one of the buffers */
typedef struct {
  PieceBufferType buffer;
  size_t start;
  size_t length;
} Piece;

/* Initial capacity for dynamic arrays */
#define PT_INITIAL_PIECE_CAPACITY 16
#define PT_INITIAL_ADD_CAPACITY 1024

/**
 * PieceTable - Main piece table structure
 *
 * Stores text as references to two buffers:
 * - original: immutable, contains initial file content
 * - add: append-only, contains all inserted text
 */
typedef struct {
  /* Original buffer (immutable after creation) */
  char *original;
  size_t original_len;

  /* Add buffer (append-only) */
  char *add;
  size_t add_len;
  size_t add_capacity;

  /* Piece descriptors */
  Piece *pieces;
  size_t piece_count;
  size_t piece_capacity;

  /* Thread safety */
  pthread_rwlock_t lock;
} PieceTable;

/**
 * pt_create - Create a piece table from initial content
 *
 * @param content  Initial text content (can be NULL for empty)
 * @return New piece table, or NULL on allocation failure
 */
PieceTable *pt_create(const char *content);

/**
 * pt_destroy - Free all piece table resources
 *
 * @param pt  Piece table to destroy
 */
void pt_destroy(PieceTable *pt);

/**
 * pt_insert - Insert text at a character position
 *
 * @param pt    Piece table
 * @param pos   Character position (0 = start)
 * @param text  Text to insert
 * @return 0 on success, -1 on error
 */
int pt_insert(PieceTable *pt, size_t pos, const char *text);

/**
 * pt_delete - Delete a range of characters
 *
 * @param pt     Piece table
 * @param pos    Start position
 * @param len    Number of characters to delete
 * @return 0 on success, -1 on error
 */
int pt_delete(PieceTable *pt, size_t pos, size_t len);

/**
 * pt_materialize - Get the full text content
 *
 * Caller must free the returned string.
 *
 * @param pt  Piece table
 * @return Newly allocated string with full content, or NULL on error
 */
char *pt_materialize(const PieceTable *pt);

/**
 * pt_length - Get total character count
 *
 * @param pt  Piece table
 * @return Total number of characters
 */
size_t pt_length(const PieceTable *pt);

/**
 * pt_get_range - Extract a substring
 *
 * Caller must free the returned string.
 *
 * @param pt     Piece table
 * @param start  Start position
 * @param len    Number of characters
 * @return Newly allocated substring, or NULL on error
 */
char *pt_get_range(const PieceTable *pt, size_t start, size_t len);

/**
 * pt_snapshot - Create a copy of the piece table state for undo
 *
 * Only copies the piece array, not the buffers (they're immutable/append-only).
 *
 * @param pt  Piece table
 * @return Snapshot that can be restored, or NULL on error
 */
typedef struct {
  Piece *pieces;
  size_t piece_count;
  size_t add_len; /* Add buffer length at snapshot time */
} PieceTableSnapshot;

PieceTableSnapshot *pt_snapshot(const PieceTable *pt);

/**
 * pt_restore - Restore piece table to a previous snapshot
 *
 * @param pt    Piece table
 * @param snap  Snapshot to restore
 * @return 0 on success, -1 on error
 */
int pt_restore(PieceTable *pt, const PieceTableSnapshot *snap);

/**
 * pt_snapshot_destroy - Free a snapshot
 *
 * @param snap  Snapshot to destroy
 */
void pt_snapshot_destroy(PieceTableSnapshot *snap);

#endif /* PIECE_TABLE_H */
