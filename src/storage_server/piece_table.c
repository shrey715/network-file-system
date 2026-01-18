/**
 * piece_table.c - Piece Table Implementation
 *
 * Efficient text buffer using piece table data structure.
 */

#include "piece_table.h"
#include <stdlib.h>
#include <string.h>

/* Helper: ensure add buffer has capacity for additional bytes */
static int ensure_add_capacity(PieceTable* pt, size_t additional) {
    size_t needed = pt->add_len + additional;
    if (needed <= pt->add_capacity) {
        return 0;
    }

    size_t new_cap = pt->add_capacity * 2;
    while (new_cap < needed) {
        new_cap *= 2;
    }

    char* new_add = realloc(pt->add, new_cap);
    if (!new_add) {
        return -1;
    }

    pt->add = new_add;
    pt->add_capacity = new_cap;
    return 0;
}

/* Helper: ensure piece array has capacity for additional pieces */
static int ensure_piece_capacity(PieceTable* pt, size_t additional) {
    size_t needed = pt->piece_count + additional;
    if (needed <= pt->piece_capacity) {
        return 0;
    }

    size_t new_cap = pt->piece_capacity * 2;
    while (new_cap < needed) {
        new_cap *= 2;
    }

    Piece* new_pieces = realloc(pt->pieces, new_cap * sizeof(Piece));
    if (!new_pieces) {
        return -1;
    }

    pt->pieces = new_pieces;
    pt->piece_capacity = new_cap;
    return 0;
}

/* Helper: get buffer pointer for a piece */
static const char* get_buffer(const PieceTable* pt, const Piece* p) {
    return (p->buffer == PT_BUFFER_ORIGINAL) ? pt->original : pt->add;
}

PieceTable* pt_create(const char* content) {
    PieceTable* pt = calloc(1, sizeof(PieceTable));
    if (!pt) {
        return NULL;
    }

    pthread_rwlock_init(&pt->lock, NULL);

    /* Initialize add buffer */
    pt->add_capacity = PT_INITIAL_ADD_CAPACITY;
    pt->add = malloc(pt->add_capacity);
    if (!pt->add) {
        free(pt);
        return NULL;
    }
    pt->add_len = 0;

    /* Initialize piece array */
    pt->piece_capacity = PT_INITIAL_PIECE_CAPACITY;
    pt->pieces = malloc(pt->piece_capacity * sizeof(Piece));
    if (!pt->pieces) {
        free(pt->add);
        free(pt);
        return NULL;
    }
    pt->piece_count = 0;

    /* Copy original content */
    if (content && *content) {
        pt->original_len = strlen(content);
        pt->original = malloc(pt->original_len + 1);
        if (!pt->original) {
            free(pt->pieces);
            free(pt->add);
            free(pt);
            return NULL;
        }
        memcpy(pt->original, content, pt->original_len + 1);

        /* Single piece spanning the entire original */
        pt->pieces[0].buffer = PT_BUFFER_ORIGINAL;
        pt->pieces[0].start = 0;
        pt->pieces[0].length = pt->original_len;
        pt->piece_count = 1;
    } else {
        pt->original = NULL;
        pt->original_len = 0;
    }

    return pt;
}

void pt_destroy(PieceTable* pt) {
    if (!pt) return;

    pthread_rwlock_destroy(&pt->lock);
    free(pt->original);
    free(pt->add);
    free(pt->pieces);
    free(pt);
}

size_t pt_length(const PieceTable* pt) {
    if (!pt) return 0;

    size_t total = 0;
    for (size_t i = 0; i < pt->piece_count; i++) {
        total += pt->pieces[i].length;
    }
    return total;
}

char* pt_materialize(const PieceTable* pt) {
    if (!pt) return NULL;

    size_t total = pt_length(pt);
    char* result = malloc(total + 1);
    if (!result) return NULL;

    char* cursor = result;
    for (size_t i = 0; i < pt->piece_count; i++) {
        const Piece* p = &pt->pieces[i];
        const char* buf = get_buffer(pt, p);
        memcpy(cursor, buf + p->start, p->length);
        cursor += p->length;
    }
    *cursor = '\0';

    return result;
}

char* pt_get_range(const PieceTable* pt, size_t start, size_t len) {
    if (!pt || len == 0) return NULL;

    size_t total = pt_length(pt);
    if (start >= total) return NULL;
    if (start + len > total) {
        len = total - start;
    }

    char* result = malloc(len + 1);
    if (!result) return NULL;

    size_t copied = 0;
    size_t pos = 0;

    for (size_t i = 0; i < pt->piece_count && copied < len; i++) {
        const Piece* p = &pt->pieces[i];
        size_t piece_end = pos + p->length;

        if (piece_end > start) {
            size_t copy_start = (start > pos) ? (start - pos) : 0;
            size_t copy_len = p->length - copy_start;
            if (copied + copy_len > len) {
                copy_len = len - copied;
            }

            const char* buf = get_buffer(pt, p);
            memcpy(result + copied, buf + p->start + copy_start, copy_len);
            copied += copy_len;
        }
        pos = piece_end;
    }

    result[copied] = '\0';
    return result;
}

/* Helper: find piece containing position, returns piece index and offset within piece */
static int find_piece_at(const PieceTable* pt, size_t pos, size_t* piece_idx, size_t* offset) {
    size_t cur_pos = 0;

    for (size_t i = 0; i < pt->piece_count; i++) {
        size_t piece_end = cur_pos + pt->pieces[i].length;
        if (pos < piece_end) {
            *piece_idx = i;
            *offset = pos - cur_pos;
            return 0;
        }
        cur_pos = piece_end;
    }

    /* Position at end of document */
    if (pos == cur_pos) {
        *piece_idx = pt->piece_count;
        *offset = 0;
        return 0;
    }

    return -1; /* Position out of bounds */
}

int pt_insert(PieceTable* pt, size_t pos, const char* text) {
    if (!pt || !text) return -1;

    size_t text_len = strlen(text);
    if (text_len == 0) return 0;

    pthread_rwlock_wrlock(&pt->lock);

    /* Append text to add buffer */
    if (ensure_add_capacity(pt, text_len) < 0) {
        pthread_rwlock_unlock(&pt->lock);
        return -1;
    }

    size_t add_start = pt->add_len;
    memcpy(pt->add + pt->add_len, text, text_len);
    pt->add_len += text_len;

    /* Find insertion point */
    size_t piece_idx, offset;
    if (find_piece_at(pt, pos, &piece_idx, &offset) < 0) {
        pthread_rwlock_unlock(&pt->lock);
        return -1;
    }

    /* Create new piece for inserted text */
    Piece new_piece = {
        .buffer = PT_BUFFER_ADD,
        .start = add_start,
        .length = text_len
    };

    if (piece_idx >= pt->piece_count) {
        /* Insert at end */
        if (ensure_piece_capacity(pt, 1) < 0) {
            pthread_rwlock_unlock(&pt->lock);
            return -1;
        }
        pt->pieces[pt->piece_count++] = new_piece;
    } else if (offset == 0) {
        /* Insert before piece */
        if (ensure_piece_capacity(pt, 1) < 0) {
            pthread_rwlock_unlock(&pt->lock);
            return -1;
        }
        memmove(&pt->pieces[piece_idx + 1], &pt->pieces[piece_idx],
                (pt->piece_count - piece_idx) * sizeof(Piece));
        pt->pieces[piece_idx] = new_piece;
        pt->piece_count++;
    } else {
        /* Split piece and insert in middle */
        if (ensure_piece_capacity(pt, 2) < 0) {
            pthread_rwlock_unlock(&pt->lock);
            return -1;
        }

        Piece* p = &pt->pieces[piece_idx];
        Piece left = {
            .buffer = p->buffer,
            .start = p->start,
            .length = offset
        };
        Piece right = {
            .buffer = p->buffer,
            .start = p->start + offset,
            .length = p->length - offset
        };

        /* Make room for two new pieces */
        memmove(&pt->pieces[piece_idx + 3], &pt->pieces[piece_idx + 1],
                (pt->piece_count - piece_idx - 1) * sizeof(Piece));
        pt->pieces[piece_idx] = left;
        pt->pieces[piece_idx + 1] = new_piece;
        pt->pieces[piece_idx + 2] = right;
        pt->piece_count += 2;
    }

    pthread_rwlock_unlock(&pt->lock);
    return 0;
}

int pt_delete(PieceTable* pt, size_t pos, size_t len) {
    if (!pt || len == 0) return 0;

    pthread_rwlock_wrlock(&pt->lock);

    size_t total = pt_length(pt);
    if (pos >= total) {
        pthread_rwlock_unlock(&pt->lock);
        return -1;
    }
    if (pos + len > total) {
        len = total - pos;
    }

    size_t del_start = pos;
    size_t del_end = pos + len;

    /* Rebuild piece array excluding deleted range */
    size_t new_count = 0;
    size_t cur_pos = 0;

    for (size_t i = 0; i < pt->piece_count; i++) {
        Piece* p = &pt->pieces[i];
        size_t piece_start = cur_pos;
        size_t piece_end = cur_pos + p->length;

        if (piece_end <= del_start || piece_start >= del_end) {
            /* Piece entirely outside delete range - keep as is */
            pt->pieces[new_count++] = *p;
        } else if (piece_start >= del_start && piece_end <= del_end) {
            /* Piece entirely inside delete range - skip */
        } else if (piece_start < del_start && piece_end > del_end) {
            /* Delete range is inside this piece - split into two */
            Piece left = {
                .buffer = p->buffer,
                .start = p->start,
                .length = del_start - piece_start
            };
            Piece right = {
                .buffer = p->buffer,
                .start = p->start + (del_end - piece_start),
                .length = piece_end - del_end
            };
            pt->pieces[new_count++] = left;
            pt->pieces[new_count++] = right;
        } else if (piece_start < del_start) {
            /* Delete starts inside this piece */
            Piece left = {
                .buffer = p->buffer,
                .start = p->start,
                .length = del_start - piece_start
            };
            pt->pieces[new_count++] = left;
        } else {
            /* Delete ends inside this piece */
            Piece right = {
                .buffer = p->buffer,
                .start = p->start + (del_end - piece_start),
                .length = piece_end - del_end
            };
            pt->pieces[new_count++] = right;
        }

        cur_pos = piece_end;
    }

    pt->piece_count = new_count;

    pthread_rwlock_unlock(&pt->lock);
    return 0;
}

PieceTableSnapshot* pt_snapshot(const PieceTable* pt) {
    if (!pt) return NULL;

    PieceTableSnapshot* snap = malloc(sizeof(PieceTableSnapshot));
    if (!snap) return NULL;

    snap->piece_count = pt->piece_count;
    snap->add_len = pt->add_len;

    if (pt->piece_count > 0) {
        snap->pieces = malloc(pt->piece_count * sizeof(Piece));
        if (!snap->pieces) {
            free(snap);
            return NULL;
        }
        memcpy(snap->pieces, pt->pieces, pt->piece_count * sizeof(Piece));
    } else {
        snap->pieces = NULL;
    }

    return snap;
}

int pt_restore(PieceTable* pt, const PieceTableSnapshot* snap) {
    if (!pt || !snap) return -1;

    pthread_rwlock_wrlock(&pt->lock);

    /* Restore piece array */
    free(pt->pieces);
    pt->piece_count = snap->piece_count;
    pt->piece_capacity = snap->piece_count > 0 ? snap->piece_count : PT_INITIAL_PIECE_CAPACITY;

    pt->pieces = malloc(pt->piece_capacity * sizeof(Piece));
    if (!pt->pieces && pt->piece_capacity > 0) {
        pthread_rwlock_unlock(&pt->lock);
        return -1;
    }

    if (snap->piece_count > 0) {
        memcpy(pt->pieces, snap->pieces, snap->piece_count * sizeof(Piece));
    }

    /* Note: We don't truncate add buffer - old content remains but is unreferenced */
    /* This is intentional for potential redo functionality */

    pthread_rwlock_unlock(&pt->lock);
    return 0;
}

void pt_snapshot_destroy(PieceTableSnapshot* snap) {
    if (!snap) return;
    free(snap->pieces);
    free(snap);
}
