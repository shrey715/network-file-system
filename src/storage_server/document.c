/**
 * document.c - Document Layer Implementation
 *
 * Sentence-level access to piece table with concurrent editing support.
 */

#include "document.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Sentence delimiters */
static int is_delimiter(char c) {
    return c == '.' || c == '!' || c == '?';
}

/* Parse text into sentence boundaries */
static int parse_sentences(Document* doc) {
    char* text = pt_materialize(doc->pt);
    if (!text) {
        doc->sentence_count = 0;
        return 0;
    }

    /* Free old sentences */
    if (doc->sentences) {
        for (size_t i = 0; i < doc->sentence_count; i++) {
            pthread_mutex_destroy(&doc->sentences[i].lock);
        }
        free(doc->sentences);
    }

    /* Count sentences first */
    size_t count = 0;
    const char* p = text;
    while (*p) {
        if (is_delimiter(*p)) count++;
        p++;
    }
    /* Add one for trailing text without delimiter */
    size_t len = strlen(text);
    if (len > 0 && !is_delimiter(text[len - 1])) {
        count++;
    }

    if (count == 0 && len > 0) {
        count = 1; /* Whole text is one sentence */
    }

    if (count == 0) {
        doc->sentences = NULL;
        doc->sentence_count = 0;
        free(text);
        return 0;
    }

    /* Allocate sentence array */
    doc->sentences = calloc(count, sizeof(SentenceBoundary));
    if (!doc->sentences) {
        free(text);
        return -1;
    }

    /* Parse boundaries */
    size_t idx = 0;
    size_t start = 0;
    p = text;
    size_t pos = 0;

    while (*p && idx < count) {
        if (is_delimiter(*p)) {
            /* Found sentence end */
            SentenceBoundary* sb = &doc->sentences[idx];
            sb->id = doc->next_sentence_id++;
            sb->start = start;
            sb->end = pos + 1; /* Include delimiter */
            pthread_mutex_init(&sb->lock, NULL);
            sb->locked_by[0] = '\0';
            sb->is_locked = 0;

            idx++;

            /* Skip trailing whitespace for next sentence */
            p++;
            pos++;
            while (*p && (*p == ' ' || *p == '\n' || *p == '\t' || *p == '\r')) {
                p++;
                pos++;
            }
            start = pos;
        } else {
            p++;
            pos++;
        }
    }

    /* Handle trailing text */
    if (start < len && idx < count) {
        SentenceBoundary* sb = &doc->sentences[idx];
        sb->id = doc->next_sentence_id++;
        sb->start = start;
        sb->end = len;
        pthread_mutex_init(&sb->lock, NULL);
        sb->locked_by[0] = '\0';
        sb->is_locked = 0;
        idx++;
    }

    doc->sentence_count = idx;
    free(text);
    return 0;
}

/* Find sentence by ID */
static SentenceBoundary* find_sentence(Document* doc, int sentence_id) {
    for (size_t i = 0; i < doc->sentence_count; i++) {
        if (doc->sentences[i].id == sentence_id) {
            return &doc->sentences[i];
        }
    }
    return NULL;
}

Document* doc_create(const char* content) {
    Document* doc = calloc(1, sizeof(Document));
    if (!doc) return NULL;

    pthread_rwlock_init(&doc->struct_lock, NULL);
    doc->next_sentence_id = 1;

    doc->pt = pt_create(content);
    if (!doc->pt) {
        free(doc);
        return NULL;
    }

    if (parse_sentences(doc) < 0) {
        pt_destroy(doc->pt);
        free(doc);
        return NULL;
    }

    return doc;
}

int doc_destroy(Document* doc) {
    if (!doc) return 0;

    /* Check for active locks */
    for (size_t i = 0; i < doc->sentence_count; i++) {
        if (doc->sentences[i].is_locked) {
            return -1; /* Locks held */
        }
    }

    pthread_rwlock_destroy(&doc->struct_lock);

    for (size_t i = 0; i < doc->sentence_count; i++) {
        pthread_mutex_destroy(&doc->sentences[i].lock);
    }
    free(doc->sentences);

    pt_destroy(doc->pt);
    free(doc);
    return 0;
}

char* doc_get_text(Document* doc) {
    if (!doc) return NULL;
    pthread_rwlock_rdlock(&doc->struct_lock);
    char* text = pt_materialize(doc->pt);
    pthread_rwlock_unlock(&doc->struct_lock);
    return text;
}

size_t doc_get_sentence_count(Document* doc) {
    if (!doc) return 0;
    pthread_rwlock_rdlock(&doc->struct_lock);
    size_t count = doc->sentence_count;
    pthread_rwlock_unlock(&doc->struct_lock);
    return count;
}

char* doc_get_sentence(Document* doc, int sentence_id) {
    if (!doc) return NULL;

    pthread_rwlock_rdlock(&doc->struct_lock);
    SentenceBoundary* sb = find_sentence(doc, sentence_id);
    if (!sb) {
        pthread_rwlock_unlock(&doc->struct_lock);
        return NULL;
    }

    char* text = pt_get_range(doc->pt, sb->start, sb->end - sb->start);
    pthread_rwlock_unlock(&doc->struct_lock);
    return text;
}

int doc_lock_sentence(Document* doc, int sentence_id, const char* username) {
    if (!doc || !username) return -1;

    pthread_rwlock_rdlock(&doc->struct_lock);
    SentenceBoundary* sb = find_sentence(doc, sentence_id);
    if (!sb) {
        pthread_rwlock_unlock(&doc->struct_lock);
        return -2; /* Not found */
    }

    /* Try to acquire sentence lock */
    if (pthread_mutex_trylock(&sb->lock) != 0) {
        pthread_rwlock_unlock(&doc->struct_lock);
        return -1; /* Already locked */
    }

    if (sb->is_locked && strcmp(sb->locked_by, username) != 0) {
        pthread_mutex_unlock(&sb->lock);
        pthread_rwlock_unlock(&doc->struct_lock);
        return -1; /* Locked by another user */
    }

    sb->is_locked = 1;
    strncpy(sb->locked_by, username, DOC_MAX_USERNAME - 1);
    sb->locked_by[DOC_MAX_USERNAME - 1] = '\0';

    pthread_rwlock_unlock(&doc->struct_lock);
    /* Note: sentence mutex remains locked until unlock */
    return 0;
}

int doc_unlock_sentence(Document* doc, int sentence_id, const char* username) {
    if (!doc || !username) return -1;

    pthread_rwlock_rdlock(&doc->struct_lock);
    SentenceBoundary* sb = find_sentence(doc, sentence_id);
    if (!sb) {
        pthread_rwlock_unlock(&doc->struct_lock);
        return -1;
    }

    if (!sb->is_locked || strcmp(sb->locked_by, username) != 0) {
        pthread_rwlock_unlock(&doc->struct_lock);
        return -1; /* Not locked by this user */
    }

    sb->is_locked = 0;
    sb->locked_by[0] = '\0';
    pthread_mutex_unlock(&sb->lock);

    pthread_rwlock_unlock(&doc->struct_lock);
    return 0;
}

int doc_edit_sentence(Document* doc, int sentence_id, const char* new_text, const char* username) {
    if (!doc || !new_text || !username) return -1;

    pthread_rwlock_wrlock(&doc->struct_lock);

    SentenceBoundary* sb = find_sentence(doc, sentence_id);
    if (!sb) {
        pthread_rwlock_unlock(&doc->struct_lock);
        return -1;
    }

    if (!sb->is_locked || strcmp(sb->locked_by, username) != 0) {
        pthread_rwlock_unlock(&doc->struct_lock);
        return -1; /* Must hold lock */
    }

    /* Delete old sentence and insert new */
    size_t old_len = sb->end - sb->start;
    if (pt_delete(doc->pt, sb->start, old_len) < 0) {
        pthread_rwlock_unlock(&doc->struct_lock);
        return -1;
    }

    if (pt_insert(doc->pt, sb->start, new_text) < 0) {
        pthread_rwlock_unlock(&doc->struct_lock);
        return -1;
    }

    /* Re-parse while keeping this sentence's lock */
    char* locked_by_backup = strdup(sb->locked_by);
    int locked_id = sb->id;

    parse_sentences(doc);

    /* Restore lock on the (possibly changed) sentence */
    SentenceBoundary* new_sb = find_sentence(doc, locked_id);
    if (new_sb) {
        new_sb->is_locked = 1;
        strncpy(new_sb->locked_by, locked_by_backup, DOC_MAX_USERNAME - 1);
    }
    free(locked_by_backup);

    pthread_rwlock_unlock(&doc->struct_lock);
    return 0;
}

int doc_get_sentence_by_index(Document* doc, size_t index) {
    if (!doc) return -1;

    pthread_rwlock_rdlock(&doc->struct_lock);
    if (index >= doc->sentence_count) {
        pthread_rwlock_unlock(&doc->struct_lock);
        return -1;
    }
    int id = doc->sentences[index].id;
    pthread_rwlock_unlock(&doc->struct_lock);
    return id;
}

int doc_get_lock_info(Document* doc, int sentence_id, int* is_locked, char* locked_by) {
    if (!doc) return -1;

    pthread_rwlock_rdlock(&doc->struct_lock);
    SentenceBoundary* sb = find_sentence(doc, sentence_id);
    if (!sb) {
        pthread_rwlock_unlock(&doc->struct_lock);
        return -1;
    }

    if (is_locked) *is_locked = sb->is_locked;
    if (locked_by) {
        strncpy(locked_by, sb->locked_by, DOC_MAX_USERNAME - 1);
        locked_by[DOC_MAX_USERNAME - 1] = '\0';
    }

    pthread_rwlock_unlock(&doc->struct_lock);
    return 0;
}

int doc_save(Document* doc, const char* filepath) {
    if (!doc || !filepath) return -1;

    char* text = doc_get_text(doc);
    if (!text) return -1;

    FILE* f = fopen(filepath, "w");
    if (!f) {
        free(text);
        return -1;
    }

    size_t len = strlen(text);
    size_t written = fwrite(text, 1, len, f);
    fclose(f);
    free(text);

    return (written == len) ? 0 : -1;
}

Document* doc_load(const char* filepath) {
    if (!filepath) return NULL;

    FILE* f = fopen(filepath, "r");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char* content = malloc(size + 1);
    if (!content) {
        fclose(f);
        return NULL;
    }

    size_t read_size = fread(content, 1, size, f);
    content[read_size] = '\0';
    fclose(f);

    Document* doc = doc_create(content);
    free(content);
    return doc;
}

DocSnapshot* doc_create_snapshot(Document* doc) {
    if (!doc) return NULL;

    pthread_rwlock_rdlock(&doc->struct_lock);

    DocSnapshot* snap = calloc(1, sizeof(DocSnapshot));
    if (!snap) {
        pthread_rwlock_unlock(&doc->struct_lock);
        return NULL;
    }

    snap->pt_snap = pt_snapshot(doc->pt);
    if (!snap->pt_snap) {
        free(snap);
        pthread_rwlock_unlock(&doc->struct_lock);
        return NULL;
    }

    snap->sentence_count = doc->sentence_count;
    if (doc->sentence_count > 0) {
        snap->sentences = malloc(doc->sentence_count * sizeof(SentenceBoundary));
        if (!snap->sentences) {
            pt_snapshot_destroy(snap->pt_snap);
            free(snap);
            pthread_rwlock_unlock(&doc->struct_lock);
            return NULL;
        }
        memcpy(snap->sentences, doc->sentences, doc->sentence_count * sizeof(SentenceBoundary));
    }

    pthread_rwlock_unlock(&doc->struct_lock);
    return snap;
}

int doc_restore_snapshot(Document* doc, DocSnapshot* snap) {
    if (!doc || !snap) return -1;

    pthread_rwlock_wrlock(&doc->struct_lock);

    /* Check for locks */
    for (size_t i = 0; i < doc->sentence_count; i++) {
        if (doc->sentences[i].is_locked) {
            pthread_rwlock_unlock(&doc->struct_lock);
            return -1;
        }
    }

    /* Restore piece table */
    if (pt_restore(doc->pt, snap->pt_snap) < 0) {
        pthread_rwlock_unlock(&doc->struct_lock);
        return -1;
    }

    /* Restore sentence boundaries */
    for (size_t i = 0; i < doc->sentence_count; i++) {
        pthread_mutex_destroy(&doc->sentences[i].lock);
    }
    free(doc->sentences);

    doc->sentence_count = snap->sentence_count;
    if (snap->sentence_count > 0) {
        doc->sentences = malloc(snap->sentence_count * sizeof(SentenceBoundary));
        memcpy(doc->sentences, snap->sentences, snap->sentence_count * sizeof(SentenceBoundary));
        for (size_t i = 0; i < doc->sentence_count; i++) {
            pthread_mutex_init(&doc->sentences[i].lock, NULL);
            doc->sentences[i].is_locked = 0;
            doc->sentences[i].locked_by[0] = '\0';
        }
    } else {
        doc->sentences = NULL;
    }

    pthread_rwlock_unlock(&doc->struct_lock);
    return 0;
}

void doc_destroy_snapshot(DocSnapshot* snap) {
    if (!snap) return;
    pt_snapshot_destroy(snap->pt_snap);
    free(snap->sentences);
    free(snap);
}
