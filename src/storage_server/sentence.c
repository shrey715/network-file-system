#include "common.h"
#include "storage_server.h"

extern SSConfig config;

/**
 * parse_sentences
 * @brief Split raw text into Sentence structures using . ! ? as delimiters.
 *
 * Allocates an array of Sentence structs (via *sentences) and copies each
 * sentence text into separately malloc'd buffers. Each Sentence lock is
 * initialized. Caller is responsible for freeing via `free_sentences`.
 *
 * @param text Null-terminated text to parse.
 * @param sentences Out parameter set to malloc'd array of Sentence.
 * @return Number of sentences parsed (>=0). Returns 0 and sets *sentences
 *         to NULL for empty input.
 */
int parse_sentences(const char* text, Sentence** sentences) {
    if (!text || strlen(text) == 0) {
        *sentences = NULL;
        return 0;
    }
    
    int count = 0;
    int capacity = 10;
    *sentences = (Sentence*)malloc(sizeof(Sentence) * capacity);
    
    const char* start = text;
    const char* p = text;
    
    while (*p) {
        // Check for sentence delimiters
        if (*p == '.' || *p == '!' || *p == '?') {
            // Found delimiter - extract sentence
            int len = p - start + 1;
            
            // Resize array if needed
            if (count >= capacity) {
                capacity *= 2;
                *sentences = (Sentence*)realloc(*sentences, sizeof(Sentence) * capacity);
            }
            
            // Allocate and copy sentence text
            (*sentences)[count].text = (char*)malloc(len + 1);
            strncpy((*sentences)[count].text, start, len);
            (*sentences)[count].text[len] = '\0';
            
            // Capture trailing whitespace after the delimiter (spaces, tabs, newlines)
            p++; // move past the delimiter
            const char* ws_start = p;
            while (*p && (*p == ' ' || *p == '\n' || *p == '\t' || *p == '\r')) p++;
            int ws_len = p - ws_start;
            if (ws_len > 0) {
                (*sentences)[count].trailing_ws = (char*)malloc(ws_len + 1);
                strncpy((*sentences)[count].trailing_ws, ws_start, ws_len);
                (*sentences)[count].trailing_ws[ws_len] = '\0';
            } else {
                (*sentences)[count].trailing_ws = strdup("");
            }

            // Initialize lock
            pthread_mutex_init(&(*sentences)[count].lock, NULL);
            (*sentences)[count].locked_by[0] = '\0';
            (*sentences)[count].is_locked = 0;

            count++;

            // Set next sentence start
            start = p;
        } else {
            p++;
        }
    }
    
    // Handle remaining text (incomplete sentence)
    if (start < p && *start != '\0') {
        if (count >= capacity) {
            capacity++;
            *sentences = (Sentence*)realloc(*sentences, sizeof(Sentence) * capacity);
        }
        
        (*sentences)[count].text = strdup(start);
        // No trailing whitespace for the last (incomplete) sentence
        (*sentences)[count].trailing_ws = strdup("");
        pthread_mutex_init(&(*sentences)[count].lock, NULL);
        (*sentences)[count].locked_by[0] = '\0';
        (*sentences)[count].is_locked = 0;
        count++;
    }
    
    return count;
}

/**
 * parse_sentences_to_list
 * @brief Parse text into a linked list of SentenceNode structures.
 * 
 * Creates a linked list where each node represents a sentence. This allows
 * for efficient locking and editing without needing to search by content.
 * 
 * @param text Null-terminated text to parse.
 * @param count Output parameter for the number of sentences parsed.
 * @return Pointer to the head of the linked list, or NULL if empty.
 */
SentenceNode* parse_sentences_to_list(const char* text, int* count) {
    if (!text || strlen(text) == 0) {
        if (count) *count = 0;
        return NULL;
    }
    
    SentenceNode* head = NULL;
    SentenceNode* tail = NULL;
    int sentence_count = 0;
    
    const char* start = text;
    const char* p = text;
    
    while (*p) {
        // Check for sentence delimiters
        if (*p == '.' || *p == '!' || *p == '?') {
            // Found delimiter - extract sentence
            int len = p - start + 1;
            
            // Create new node
            SentenceNode* node = (SentenceNode*)malloc(sizeof(SentenceNode));
            if (!node) {
                // Cleanup on error
                free_sentence_list(head);
                if (count) *count = 0;
                return NULL;
            }
            
            // Allocate and copy sentence text
            node->text = (char*)malloc(len + 1);
            strncpy(node->text, start, len);
            node->text[len] = '\0';
            
            // Capture trailing whitespace after the delimiter
            p++; // move past the delimiter
            const char* ws_start = p;
            while (*p && (*p == ' ' || *p == '\n' || *p == '\t' || *p == '\r')) p++;
            int ws_len = p - ws_start;
            if (ws_len > 0) {
                node->trailing_ws = (char*)malloc(ws_len + 1);
                strncpy(node->trailing_ws, ws_start, ws_len);
                node->trailing_ws[ws_len] = '\0';
            } else {
                node->trailing_ws = strdup("");
            }
            
            // Initialize lock
            pthread_mutex_init(&node->lock, NULL);
            node->locked_by[0] = '\0';
            node->is_locked = 0;
            node->next = NULL;
            
            // Add to linked list
            if (!head) {
                head = node;
                tail = node;
            } else {
                tail->next = node;
                tail = node;
            }
            
            sentence_count++;
            
            // Set next sentence start
            start = p;
        } else {
            p++;
        }
    }
    
    // Handle remaining text (incomplete sentence)
    if (start < p && *start != '\0') {
        SentenceNode* node = (SentenceNode*)malloc(sizeof(SentenceNode));
        if (!node) {
            free_sentence_list(head);
            if (count) *count = 0;
            return NULL;
        }
        
        node->text = strdup(start);
        node->trailing_ws = strdup("");
        pthread_mutex_init(&node->lock, NULL);
        node->locked_by[0] = '\0';
        node->is_locked = 0;
        node->next = NULL;
        
        if (!head) {
            head = node;
            tail = node;
        } else {
            tail->next = node;
            tail = node;
        }
        
        sentence_count++;
    }
    
    if (count) *count = sentence_count;
    return head;
}

/**
 * free_sentence_list
 * @brief Free a linked list of SentenceNode structures.
 * 
 * @param head Pointer to the head of the linked list.
 */
void free_sentence_list(SentenceNode* head) {
    SentenceNode* current = head;
    while (current) {
        SentenceNode* next = current->next;
        
        if (current->text) {
            free(current->text);
        }
        if (current->trailing_ws) {
            free(current->trailing_ws);
        }
        pthread_mutex_destroy(&current->lock);
        free(current);
        
        current = next;
    }
}

/**
 * get_sentence_at_index
 * @brief Get the sentence node at a specific index in a linked list.
 * 
 * @param head Head of the linked list.
 * @param index 0-based index of the sentence to retrieve.
 * @return Pointer to the SentenceNode, or NULL if index is out of bounds.
 */
static SentenceNode* get_sentence_at_index(SentenceNode* head, int index) {
    SentenceNode* current = head;
    int i = 0;
    
    while (current && i < index) {
        current = current->next;
        i++;
    }
    
    return current;
}

/**
 * lock_sentence
 * @brief Attempt to acquire a non-blocking lock on a sentence for `username`.
 *
 * This uses pthread_mutex_trylock so callers can return immediately with a
 * lock-failed error rather than block. If locked successfully the sentence's
 * locked_by and is_locked fields are set.
 *
 * @param file Pointer to FileWithSentences containing the sentences.
 * @param sentence_idx Index of the sentence to lock (0-based).
 * @param username Username attempting the lock.
 * @return ERR_SUCCESS on success, ERR_INVALID_SENTENCE for bad index, or
 *         ERR_SENTENCE_LOCKED if the sentence is already locked.
 */
int lock_sentence(FileWithSentences* file, int sentence_idx, const char* username) {
    if (sentence_idx < 0 || sentence_idx >= file->sentence_count) {
        return ERR_INVALID_SENTENCE;
    }
    
    Sentence* sentence = &file->sentences[sentence_idx];
    
    // Try to acquire lock (non-blocking)
    if (pthread_mutex_trylock(&sentence->lock) == 0) {
        // Check if already locked by someone else
        if (sentence->is_locked && strcmp(sentence->locked_by, username) != 0) {
            pthread_mutex_unlock(&sentence->lock);
            return ERR_SENTENCE_LOCKED;
        }
        
        // Lock acquired
        strcpy(sentence->locked_by, username);
        sentence->is_locked = 1;
        return ERR_SUCCESS;
    }
    
    return ERR_SENTENCE_LOCKED;
}

/**
 * unlock_sentence
 * @brief Release a previously acquired sentence lock owned by `username`.
 *
 * Verifies ownership before unlocking. Returns permission error if the
 * calling user does not hold the lock.
 *
 * @param file Pointer to FileWithSentences.
 * @param sentence_idx Index of the sentence to unlock.
 * @param username Username requesting unlock.
 * @return ERR_SUCCESS on success, ERR_INVALID_SENTENCE or ERR_PERMISSION_DENIED.
 */
int unlock_sentence(FileWithSentences* file, int sentence_idx, const char* username) {
    if (sentence_idx < 0 || sentence_idx >= file->sentence_count) {
        return ERR_INVALID_SENTENCE;
    }
    
    Sentence* sentence = &file->sentences[sentence_idx];
    
    // Verify that this user owns the lock
    if (!sentence->is_locked || strcmp(sentence->locked_by, username) != 0) {
        return ERR_PERMISSION_DENIED;
    }
    
    // Clear lock
    sentence->is_locked = 0;
    sentence->locked_by[0] = '\0';
    pthread_mutex_unlock(&sentence->lock);
    
    return ERR_SUCCESS;
}

/**
 * free_sentences
 * @brief Free an array of Sentence objects previously created by
 *        `parse_sentences`.
 *
 * Destroys per-sentence mutexes and frees allocated strings and the array.
 *
 * @param sentences Pointer returned by parse_sentences.
 * @param count Number of sentences in the array.
 */
void free_sentences(Sentence* sentences, int count) {
    for (int i = 0; i < count; i++) {
        if (sentences[i].text) {
            free(sentences[i].text);
        }
        if (sentences[i].trailing_ws) {
            free(sentences[i].trailing_ws);
        }
        pthread_mutex_destroy(&sentences[i].lock);
    }
    free(sentences);
}

/**
 * ss_write_lock
 * @brief Acquire a write lock for a specific sentence in `filename`.
 *
 * Reads the file, parses sentences, and tries to lock the requested index.
 * In the current design the parsed sentences are not stored long-term; this
 * function provides the locking semantic and should be followed by write
 * operations that assume the lock is held by `username`.
 *
 * @param filename Filename to operate on.
 * @param sentence_idx Index of the sentence to lock.
 * @param username Username requesting the lock.
 * @return ERR_SUCCESS on success, or ERR_FILE_NOT_FOUND / ERR_INVALID_SENTENCE / ERR_SENTENCE_LOCKED.
 */
int ss_write_lock(const char* filename, int sentence_idx, const char* username) {
    char filepath[MAX_PATH];
    if (ss_build_filepath(filepath, sizeof(filepath), filename, NULL) != ERR_SUCCESS) {
        return ERR_FILE_OPERATION_FAILED;
    }
    
    if (!file_exists(filepath)) {
        return ERR_FILE_NOT_FOUND;
    }
    
    // Read current content
    char* content = read_file_content(filepath);
    if (!content) {
        return ERR_FILE_OPERATION_FAILED;
    }
    
    // Parse into linked list
    int count = 0;
    SentenceNode* sentence_list = parse_sentences_to_list(content, &count);
    free(content);
    
    if (!sentence_list && count == 0) {
        // Empty file - create a single empty sentence node
        sentence_list = (SentenceNode*)malloc(sizeof(SentenceNode));
        if (!sentence_list) {
            return ERR_FILE_OPERATION_FAILED;
        }
        sentence_list->text = strdup("");
        sentence_list->trailing_ws = strdup("");
        pthread_mutex_init(&sentence_list->lock, NULL);
        sentence_list->locked_by[0] = '\0';
        sentence_list->is_locked = 0;
        sentence_list->next = NULL;
        count = 1;
    }
    
    // SPECIAL CASE: Appending a new sentence (sentence_idx == count)
    if (sentence_idx == count && count > 0) {
        // Find the last node
        SentenceNode* last = sentence_list;
        while (last->next) {
            last = last->next;
        }
        
        // Check if last sentence ends with delimiter
        int has_delimiter = 0;
        if (last->text && strlen(last->text) > 0) {
            char last_char = last->text[strlen(last->text) - 1];
            if (last_char == '.' || last_char == '!' || last_char == '?') {
                has_delimiter = 1;
            }
        }
        
        if (!has_delimiter) {
            free_sentence_list(sentence_list);
            return ERR_INVALID_SENTENCE;
        }
        
        // Create new empty sentence node
        SentenceNode* new_node = (SentenceNode*)malloc(sizeof(SentenceNode));
        if (!new_node) {
            free_sentence_list(sentence_list);
            return ERR_FILE_OPERATION_FAILED;
        }
        new_node->text = strdup("");
        new_node->trailing_ws = strdup("");
        pthread_mutex_init(&new_node->lock, NULL);
        new_node->locked_by[0] = '\0';
        new_node->is_locked = 0;
        new_node->next = NULL;
        
        last->next = new_node;
        count++;
        
        // Check if already locked by sentence index (not node pointer, since we create new nodes each time)
        int lock_status = check_lock(filename, sentence_idx, username);
        if (lock_status == 1) {
            // Already locked by this user
            free_sentence_list(sentence_list);
            return ERR_SUCCESS;
        } else if (lock_status == -1) {
            // Locked by someone else
            free_sentence_list(sentence_list);
            return ERR_SENTENCE_LOCKED;
        }
        
        // Try to acquire lock on the node
        if (pthread_mutex_trylock(&new_node->lock) != 0) {
            free_sentence_list(sentence_list);
            return ERR_SENTENCE_LOCKED;
        }
        
        if (new_node->is_locked && strcmp(new_node->locked_by, username) != 0) {
            pthread_mutex_unlock(&new_node->lock);
            free_sentence_list(sentence_list);
            return ERR_SENTENCE_LOCKED;
        }
        
        // Lock acquired
        strncpy(new_node->locked_by, username, MAX_USERNAME - 1);
        new_node->locked_by[MAX_USERNAME - 1] = '\0';
        new_node->is_locked = 1;
        
        // Add to lock registry (original text is empty for new sentence)
        int result = add_locked_file(filename, username, sentence_idx, new_node, sentence_list, count, "");
        if (result != ERR_SUCCESS) {
            new_node->is_locked = 0;
            new_node->locked_by[0] = '\0';
            pthread_mutex_unlock(&new_node->lock);
            free_sentence_list(sentence_list);
            return ERR_FILE_OPERATION_FAILED;
        }
        
        char msg[256];
        snprintf(msg, sizeof(msg), 
                 "Locked new sentence %d in '%s' (append mode)", 
                 sentence_idx, filename);
        log_message("SS", "INFO", msg);
        
        return ERR_SUCCESS;
    }
    
    // Validate index
    if (sentence_idx < 0 || sentence_idx >= count) {
        free_sentence_list(sentence_list);
        return ERR_INVALID_SENTENCE;
    }
    
    // Get the node at the specified index
    SentenceNode* target_node = get_sentence_at_index(sentence_list, sentence_idx);
    if (!target_node) {
        free_sentence_list(sentence_list);
        return ERR_INVALID_SENTENCE;
    }
    
    // Check if already locked by sentence index (not node pointer, since we create new nodes each time)
    int lock_status = check_lock(filename, sentence_idx, username);
    if (lock_status == 1) {
        // Already locked by this user
        free_sentence_list(sentence_list);
        return ERR_SUCCESS;
    } else if (lock_status == -1) {
        // Locked by someone else
        free_sentence_list(sentence_list);
        return ERR_SENTENCE_LOCKED;
    }
    
    // Try to acquire lock on the node
    if (pthread_mutex_trylock(&target_node->lock) != 0) {
        free_sentence_list(sentence_list);
        return ERR_SENTENCE_LOCKED;
    }
    
    if (target_node->is_locked && strcmp(target_node->locked_by, username) != 0) {
        pthread_mutex_unlock(&target_node->lock);
        free_sentence_list(sentence_list);
        return ERR_SENTENCE_LOCKED;
    }
    
    // Lock acquired
    strncpy(target_node->locked_by, username, MAX_USERNAME - 1);
    target_node->locked_by[MAX_USERNAME - 1] = '\0';
    target_node->is_locked = 1;
    
    // Capture original text before any edits
    const char* original_text = target_node->text ? target_node->text : "";
    
    // Add to lock registry
    int result = add_locked_file(filename, username, sentence_idx, target_node, sentence_list, count, original_text);
    if (result != ERR_SUCCESS) {
        target_node->is_locked = 0;
        target_node->locked_by[0] = '\0';
        pthread_mutex_unlock(&target_node->lock);
        free_sentence_list(sentence_list);
        return ERR_FILE_OPERATION_FAILED;
    }
    
    char msg[256];
    snprintf(msg, sizeof(msg), 
             "Locked sentence %d in '%s' (total sentences: %d)", 
             sentence_idx, filename, count);
    log_message("SS", "INFO", msg);
    
    return ERR_SUCCESS;
}

/**
 * ss_write_word
 * @brief Replace a single word inside a sentence in-memory.
 *
 * Verifies the caller holds the sentence lock, modifies the requested word
 * in the locked in-memory sentence list. Changes are NOT written to disk
 * until ss_write_unlock is called (via ETIRW command).
 *
 * Uses identity-based locking: finds sentence by original content, not index.
 *
 * @param filename Target filename.
 * @param sentence_idx Sentence index to edit (0-based) - used for lookup in lock registry only.
 * @param word_idx Word index inside the sentence to replace (0-based).
 * @param new_word New null-terminated word to insert.
 * @param username Username performing the edit (must hold lock).
 * @return ERR_SUCCESS on success or an ERR_* code for errors.
 */
int ss_write_word(const char* filename, int sentence_idx, int word_idx, 
                  const char* new_word, const char* username) {
    // Get locked file entry to verify lock exists
    LockedFile* locked_file = find_locked_file(filename, username);
    if (!locked_file || !locked_file->is_active) {
        return ERR_PERMISSION_DENIED; // No active lock found
    }
    
    // Get locked sentence list from registry (for editing)
    // We edit the in-memory locked sentence, validation happens on unlock
    int locked_count = 0;
    SentenceNode* locked_list = get_locked_sentence_list(filename, username, &locked_count);
    
    if (!locked_list || sentence_idx >= locked_count) {
        return ERR_INVALID_SENTENCE;
    }
    
    // Get the target sentence node from locked registry (this is what we're editing)
    SentenceNode* target_sentence = get_sentence_at_index(locked_list, sentence_idx);
    if (!target_sentence) {
        return ERR_INVALID_SENTENCE;
    }
    
    // Split sentence into words (use dynamic copy to avoid truncation)
    char* sentence_copy = strdup(target_sentence->text);
    if (!sentence_copy) {
        return ERR_FILE_OPERATION_FAILED;
    }
    
    char* words[100];
    int word_count = 0;
    
    // SPECIAL CASE: Empty sentence
    if (strlen(sentence_copy) == 0) {
        // Allow only word_idx 0 for empty sentence
        if (word_idx != 0) {
            free(sentence_copy);
            return ERR_INVALID_WORD;
        }
        word_count = 0;
    } else {
        // Tokenize by spaces/tabs/newlines
        char* token = strtok(sentence_copy, " \t\n");
        while (token != NULL && word_count < 100) {
            words[word_count++] = strdup(token);
            token = strtok(NULL, " \t\n");
        }
    }
    
    free(sentence_copy);
    
    // Validate word index (allow up to word_count for appending)
    if (word_idx < 0 || word_idx > word_count) {
        for (int i = 0; i < word_count; i++) free(words[i]);
        return ERR_INVALID_WORD;
    }
    
    // Split new_word by spaces to handle multi-word input (use dynamic copy)
    char* new_word_copy = strdup(new_word);
    if (!new_word_copy) {
        for (int i = 0; i < word_count; i++) free(words[i]);
        return ERR_FILE_OPERATION_FAILED;
    }
    
    char* new_words[100];
    int new_word_count = 0;
    
    char* new_token = strtok(new_word_copy, " \t\n");
    while (new_token != NULL && new_word_count < 100) {
        new_words[new_word_count++] = strdup(new_token);
        new_token = strtok(NULL, " \t\n");
    }
    
    free(new_word_copy);
    
    // Prepare final word array using INSERT semantics
    char* final_words[100];
    int final_count = 0;
    
    // Copy words before word_idx
    for (int i = 0; i < word_idx && final_count < 100; i++) {
        final_words[final_count++] = strdup(words[i]);
    }
    
    // Insert all new words at word_idx
    for (int i = 0; i < new_word_count && final_count < 100; i++) {
        final_words[final_count++] = strdup(new_words[i]);
    }
    
    // Copy remaining words after word_idx (INCLUDING the word at word_idx)
    for (int i = word_idx; i < word_count && final_count < 100; i++) {
        final_words[final_count++] = strdup(words[i]);
    }
    
    // Clean up temporary arrays
    for (int i = 0; i < word_count; i++) free(words[i]);
    for (int i = 0; i < new_word_count; i++) free(new_words[i]);
    
    // Rebuild sentence from final_words with dynamic allocation
    size_t new_sentence_size = 0;
    for (int i = 0; i < final_count; i++) {
        new_sentence_size += strlen(final_words[i]);
        if (i > 0) new_sentence_size++; // space
    }
    new_sentence_size += 1; // null terminator
    
    char* new_sentence = (char*)malloc(new_sentence_size);
    if (!new_sentence) {
        for (int i = 0; i < final_count; i++) free(final_words[i]);
        return ERR_FILE_OPERATION_FAILED;
    }
    
    new_sentence[0] = '\0';
    for (int i = 0; i < final_count; i++) {
        if (i > 0) strcat(new_sentence, " ");
        strcat(new_sentence, final_words[i]);
    }
    
    // Clean up final_words
    for (int i = 0; i < final_count; i++) free(final_words[i]);
    
    // Update sentence in locked registry (in-memory only, no disk write yet)
    free(target_sentence->text);
    target_sentence->text = strdup(new_sentence);
    free(new_sentence);
    
    // Note: We do NOT write to disk here - that happens in ss_write_unlock
    // This ensures that readers see the original content until ETIRW is sent
    
    return ERR_SUCCESS;
}

/**
 * ss_write_unlock
 * @brief Finalize a write session by re-parsing content, updating metadata
 *        and releasing any in-memory resources.
 *
 * Note: This implementation re-parses the file and writes back the content
 * to ensure sentence boundaries are normalized after edits.
 *
 * @param filename Target filename.
 * @param sentence_idx Unused in current implementation, kept for API
 *                     consistency.
 * @param username Username that completed the write.
 * @return ERR_SUCCESS on success or an ERR_* code on failure.
 */
int ss_write_unlock(const char* filename, int sentence_idx, const char* username) {
    char filepath[MAX_PATH];
    if (ss_build_filepath(filepath, sizeof(filepath), filename, NULL) != ERR_SUCCESS) {
        return ERR_FILE_OPERATION_FAILED;
    }
    
    if (!file_exists(filepath)) {
        return ERR_FILE_NOT_FOUND;
    }
    
    // Get locked file entry
    LockedFile* locked_file = find_locked_file(filename, username);
    if (!locked_file || !locked_file->is_active) {
        return ERR_PERMISSION_DENIED; // No active lock found
    }
    
    // Get the locked node and edited sentence from lock registry
    SentenceNode* locked_node = locked_file->locked_node;
    SentenceNode* locked_list = locked_file->sentence_list_head;
    
    if (!locked_node || !locked_list) {
        remove_lock_by_node(filename, locked_node);
        return ERR_INVALID_SENTENCE;
    }
    
    // Get the edited sentence from the locked list
    SentenceNode* edited_node = get_sentence_at_index(locked_list, sentence_idx);
    if (!edited_node) {
        remove_lock_by_node(filename, locked_node);
        return ERR_INVALID_SENTENCE;
    }
    
    // Read current file content (may have changed since lock was acquired)
    char* content = read_file_content(filepath);
    if (!content) {
        remove_lock_by_node(filename, locked_node);
        return ERR_FILE_OPERATION_FAILED;
    }
    
    // Parse current file into linked list
    int current_count = 0;
    SentenceNode* current_list = parse_sentences_to_list(content, &current_count);
    free(content);
    
    // Find the node in current file that corresponds to the locked node
    // We match by comparing the original content stored at lock time
    SentenceNode* target_node = NULL;
    const char* original_text = locked_file->original_text;
    
    // SPECIAL CASE: Empty file and original sentence was empty
    if (current_count == 0 && (!original_text || strlen(original_text) == 0)) {
        // Create a new sentence node
        current_list = (SentenceNode*)malloc(sizeof(SentenceNode));
        if (!current_list) {
            remove_lock_by_node(filename, locked_node);
            return ERR_FILE_OPERATION_FAILED;
        }
        current_list->text = strdup("");
        current_list->trailing_ws = strdup("");
        pthread_mutex_init(&current_list->lock, NULL);
        current_list->locked_by[0] = '\0';
        current_list->is_locked = 0;
        current_list->next = NULL;
        current_count = 1;
        target_node = current_list;
    } else {
        // Find node by matching original content
        SentenceNode* current = current_list;
        while (current) {
            int content_match = 0;
            if (!original_text || strlen(original_text) == 0) {
                content_match = (!current->text || strlen(current->text) == 0);
            } else {
                content_match = (current->text && strcmp(current->text, original_text) == 0);
            }
            
            if (content_match) {
                target_node = current;
                break;
            }
            current = current->next;
        }
        
        // If not found, check if this was an append operation
        if (!target_node && (!original_text || strlen(original_text) == 0) && current_count > 0) {
            // Find last node
            SentenceNode* last = current_list;
            while (last->next) {
                last = last->next;
            }
            
            // Check if last sentence ends with delimiter
            int has_delimiter = 0;
            if (last->text && strlen(last->text) > 0) {
                char last_char = last->text[strlen(last->text) - 1];
                if (last_char == '.' || last_char == '!' || last_char == '?') {
                    has_delimiter = 1;
                }
            }
            
            if (has_delimiter) {
                // Create new node and append
                SentenceNode* new_node = (SentenceNode*)malloc(sizeof(SentenceNode));
                if (!new_node) {
                    free_sentence_list(current_list);
                    remove_lock_by_node(filename, locked_node);
                    return ERR_FILE_OPERATION_FAILED;
                }
                new_node->text = strdup(edited_node->text ? edited_node->text : "");
                new_node->trailing_ws = strdup(edited_node->trailing_ws ? edited_node->trailing_ws : "");
                pthread_mutex_init(&new_node->lock, NULL);
                new_node->locked_by[0] = '\0';
                new_node->is_locked = 0;
                new_node->next = NULL;
                
                last->next = new_node;
                target_node = new_node;
                current_count++;
            } else {
                // Cannot append
                free_sentence_list(current_list);
                remove_lock_by_node(filename, locked_node);
                
                char msg[512];
                snprintf(msg, sizeof(msg), 
                         "Cannot append: last sentence doesn't end with delimiter");
                log_message("SS", "ERROR", msg);
                return ERR_INVALID_SENTENCE;
            }
        }
    }
    
    if (!target_node) {
        // Original sentence not found - may have been deleted
        free_sentence_list(current_list);
        remove_lock_by_node(filename, locked_node);
        
        char msg[512];
        snprintf(msg, sizeof(msg), 
                 "Cannot commit: original sentence not found in current file (may have been deleted)");
        log_message("SS", "ERROR", msg);
        return ERR_INVALID_SENTENCE;
    }
    
    // Update the target node with edited content
    if (target_node->text) {
        free(target_node->text);
    }
    if (target_node->trailing_ws) {
        free(target_node->trailing_ws);
    }
    
    target_node->text = strdup(edited_node->text ? edited_node->text : "");
    target_node->trailing_ws = strdup(edited_node->trailing_ws ? edited_node->trailing_ws : "");
    
    // Rebuild file content from linked list
    size_t total_size = 1; // null terminator
    SentenceNode* current = current_list;
    while (current) {
        if (current->text) {
            total_size += strlen(current->text);
        }
        if (current->trailing_ws) {
            total_size += strlen(current->trailing_ws);
        }
        current = current->next;
    }
    
    char* final_content = (char*)malloc(total_size);
    if (!final_content) {
        free_sentence_list(current_list);
        remove_lock_by_node(filename, locked_node);
        return ERR_FILE_OPERATION_FAILED;
    }
    
    final_content[0] = '\0';
    current = current_list;
    while (current) {
        if (current->text) {
            strcat(final_content, current->text);
        }
        if (current->trailing_ws) {
            strcat(final_content, current->trailing_ws);
        }
        current = current->next;
    }
    
    // Decode <NL> tokens back to actual newlines
    char* decoded_content = (char*)malloc(strlen(final_content) + 1);
    if (!decoded_content) {
        free(final_content);
        free_sentence_list(current_list);
        remove_lock_by_node(filename, locked_node);
        return ERR_FILE_OPERATION_FAILED;
    }
    
    const char* src = final_content;
    char* dst = decoded_content;
    while (*src) {
        if (strncmp(src, "<NL>", 4) == 0) {
            *dst++ = '\n';
            src += 4;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
    
    // Save undo snapshot before overwriting file
    if (ss_save_undo(filename) != ERR_SUCCESS) {
        char msg[256];
        snprintf(msg, sizeof(msg), 
                 "Failed to create undo snapshot for '%s' during write unlock", 
                 filename);
        log_message("SS", "WARN", msg);
        // Continue with write despite undo failure
    }
    
    // Write back properly formatted content with decoded newlines
    int write_result = write_file_content(filepath, decoded_content);
    
    free(decoded_content);
    free(final_content);
    free_sentence_list(current_list);
    
    if (write_result != 0) {
        remove_lock_by_node(filename, locked_node);
        return ERR_FILE_OPERATION_FAILED;
    }
    
    // Unlock the node and remove from lock registry
    if (locked_node) {
        locked_node->is_locked = 0;
        locked_node->locked_by[0] = '\0';
        pthread_mutex_unlock(&locked_node->lock);
    }
    
    // Remove lock from global registry by node
    if (remove_lock_by_node(filename, locked_node) != ERR_SUCCESS) {
        return ERR_FILE_OPERATION_FAILED;
    }
    
    // Update metadata
    char metapath[MAX_PATH];
    if (ss_build_filepath(metapath, sizeof(metapath), filename, ".meta") == ERR_SUCCESS) {
        FILE* f = fopen(metapath, "r+");  // Change from "a" to "r+"
        if (f) {
            // Read existing metadata
            char line[256];
            char owner[MAX_USERNAME] = "";
            long created = 0;
            
            while (fgets(line, sizeof(line), f)) {
                if (strncmp(line, "owner:", 6) == 0) {
                    sscanf(line, "owner:%s", owner);
                } else if (strncmp(line, "created:", 8) == 0) {
                    sscanf(line, "created:%ld", &created);
                }
            }
            
            // Rewrite metadata with updated timestamp
            rewind(f);
            ftruncate(fileno(f), 0);  // Clear file
            fprintf(f, "owner:%s\n", owner);
            fprintf(f, "created:%ld\n", created);
            fprintf(f, "modified:%ld\n", time(NULL));
            fclose(f);
        }
    }
    
    // Track edit statistics
    increment_edit_stats(filename, username);
    
    char msg[512];
    const char* orig_text = locked_file->original_text;
    snprintf(msg, sizeof(msg), 
             "Write completed on '%s' sentence %d (total sentences: %d, original: '%.50s%s')", 
             filename, sentence_idx, current_count,
             orig_text && strlen(orig_text) > 0 ? orig_text : "(empty)",
             orig_text && strlen(orig_text) > 50 ? "..." : "");
    log_message("SS", "INFO", msg);
    
    return ERR_SUCCESS;
}

/**
 * ss_save_undo
 * @brief Save the current file contents to a `.undo` snapshot for rollback.
 *
 * Overwrites the previous undo snapshot. Returns error if the current file
 * cannot be read or the snapshot cannot be written.
 *
 * @param filename Target filename.
 * @return ERR_SUCCESS on success, or ERR_FILE_OPERATION_FAILED.
 */
int ss_save_undo(const char* filename) {
    char filepath[MAX_PATH];
    char undopath[MAX_PATH];
    
    // Safely construct paths
    if (ss_build_filepath(filepath, sizeof(filepath), filename, NULL) != ERR_SUCCESS) {
        return ERR_FILE_OPERATION_FAILED;
    }
    
    char* content = read_file_content(filepath);
    if (!content) {
        return ERR_FILE_OPERATION_FAILED;
    }
    
    if (ss_build_filepath(undopath, sizeof(undopath), filename, ".undo") != ERR_SUCCESS) {
        free(content);
        return ERR_FILE_OPERATION_FAILED;
    }
    
    int result = write_file_content(undopath, content);
    free(content);
    
    return result == 0 ? ERR_SUCCESS : ERR_FILE_OPERATION_FAILED;
}

/**
 * ss_undo_file
 * @brief Restore the latest `.undo` snapshot into the primary file.
 *
 * If no undo snapshot exists returns ERR_UNDO_NOT_AVAILABLE.
 *
 * @param filename Target filename.
 * @return ERR_SUCCESS on success or an ERR_* code on failure.
 */
int ss_undo_file(const char* filename) {
    char undopath[MAX_PATH];
    char filepath[MAX_PATH];
    
    // Safely construct paths
    if (ss_build_filepath(undopath, sizeof(undopath), filename, ".undo") != ERR_SUCCESS) {
        return ERR_FILE_OPERATION_FAILED;
    }
    
    if (!file_exists(undopath)) {
        return ERR_UNDO_NOT_AVAILABLE;
    }
    
    char* undo_content = read_file_content(undopath);
    if (!undo_content) {
        return ERR_FILE_OPERATION_FAILED;
    }
    
    if (ss_build_filepath(filepath, sizeof(filepath), filename, NULL) != ERR_SUCCESS) {
        free(undo_content);
        return ERR_FILE_OPERATION_FAILED;
    }
    
    int result = write_file_content(filepath, undo_content);
    free(undo_content);
    
    if (result == 0) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Undo performed on '%s'", filename);
        log_message("SS", "INFO", msg);
        return ERR_SUCCESS;
    }
    
    return ERR_FILE_OPERATION_FAILED;
}

/**
 * ss_stream_file
 * @brief Stream the file contents word-by-word to a connected client socket.
 *
 * Each word is sent as a separate framed MSG_RESPONSE. Between words the
 * function sleeps briefly to simulate streaming. Sends a MSG_STOP at the end
 * to indicate completion.
 *
 * @param client_socket Connected client socket fd.
 * @param filename Filename to stream.
 * @return ERR_SUCCESS on success or an ERR_* code on failure.
 */
int ss_stream_file(int client_socket, const char* filename) {
    char filepath[MAX_PATH];
    if (ss_build_filepath(filepath, sizeof(filepath), filename, NULL) != ERR_SUCCESS) {
        return ERR_FILE_OPERATION_FAILED;
    }
    
    char* content = read_file_content(filepath);
    if (!content) {
        return ERR_FILE_NOT_FOUND;
    }
    
    // Check if file is empty
    if (strlen(content) == 0) {
        free(content);
        
        // Send error message header
        MessageHeader error_header;
        memset(&error_header, 0, sizeof(error_header));
        error_header.msg_type = MSG_ERROR;
        error_header.error_code = ERR_FILE_EMPTY;
        error_header.data_length = 0;
        
        send_message(client_socket, &error_header, NULL);
        return ERR_FILE_EMPTY;
    }
    
    // Tokenize and send word by word
    char content_copy[BUFFER_SIZE * 10];
    strncpy(content_copy, content, sizeof(content_copy) - 1);
    content_copy[sizeof(content_copy) - 1] = '\0';
    
    char* word = strtok(content_copy, " \t\r");
    while (word != NULL) {
        MessageHeader header;
        memset(&header, 0, sizeof(header));
        header.msg_type = MSG_RESPONSE;
        header.data_length = strlen(word);
        
        send_message(client_socket, &header, word);
        
        // Sleep for 0.1 seconds (100,000 microseconds)
        usleep(100000);
        
        word = strtok(NULL, " \t\r");
    }
    
    // Send STOP message
    MessageHeader stop_header;
    memset(&stop_header, 0, sizeof(stop_header));
    stop_header.msg_type = MSG_STOP;
    stop_header.data_length = 0;
    send_message(client_socket, &stop_header, NULL);
    
    free(content);
    return ERR_SUCCESS;
}

/**
 * send_simple_response
 * @brief Helper to send a simple response with no payload.
 *
 * Reduces repetitive header initialization and send patterns.
 *
 * @param client_fd Client socket fd.
 * @param msg_type MSG_ACK, MSG_ERROR, etc.
 * @param error_code Error code to include in the response.
 */
void send_simple_response(int client_fd, int msg_type, int error_code) {
    MessageHeader resp;
    memset(&resp, 0, sizeof(resp));
    resp.msg_type = msg_type;
    resp.error_code = error_code;
    resp.data_length = 0;
    send_message(client_fd, &resp, NULL);
}

/**
 * handle_ss_create
 * @brief Handler for OP_SS_CREATE operation.
 */
int handle_ss_create(int client_fd, MessageHeader* header, const char* payload) {
    // Construct full path from foldername and filename
    char fullpath[MAX_PATH];
    if (header->foldername[0]) {
        snprintf(fullpath, sizeof(fullpath), "%s/%s", 
                 header->foldername, header->filename);
    } else {
        strncpy(fullpath, header->filename, sizeof(fullpath) - 1);
        fullpath[sizeof(fullpath) - 1] = '\0';
    }
    
    // Log operation start
    char details[1200];
    snprintf(details, sizeof(details), "file=%s owner=%s", fullpath, payload ? payload : "unknown");
    log_message("SS", "INFO", details);
    
    int result = ss_create_file(fullpath, payload ? payload : "unknown");
    
    // Log completion
    if (result == ERR_SUCCESS) {
        char msg[1200];
        snprintf(msg, sizeof(msg), "✓ File '%s' created successfully", fullpath);
        log_message("SS", "INFO", msg);
    } else {
        char msg[1200];
        snprintf(msg, sizeof(msg), "✗ File creation failed for '%s': %s", 
                 fullpath, get_error_message(result));
        log_message("SS", "ERROR", msg);
    }
    
    send_simple_response(client_fd, 
                        (result == ERR_SUCCESS) ? MSG_ACK : MSG_ERROR, 
                        result);
    return result;
}

/**
 * handle_ss_delete
 * @brief Handler for OP_SS_DELETE operation.
 */
int handle_ss_delete(int client_fd, MessageHeader* header) {
    // Log delete request
    char details[1200];
    snprintf(details, sizeof(details), "file=%s", header->filename);
    log_message("SS", "INFO", details);
    
    int result = ss_delete_file(header->filename);
    
    // Log result
    if (result == ERR_SUCCESS) {
        char msg[1200];
        snprintf(msg, sizeof(msg), "✓ File '%s' deleted successfully", header->filename);
        log_message("SS", "INFO", msg);
    } else {
        char msg[1200];
        snprintf(msg, sizeof(msg), "✗ File deletion failed for '%s': %s", 
                 header->filename, get_error_message(result));
        log_message("SS", "ERROR", msg);
    }
    
    send_simple_response(client_fd, 
                        (result == ERR_SUCCESS) ? MSG_ACK : MSG_ERROR, 
                        result);
    return result;
}

/**
 * handle_ss_read
 * @brief Handler for OP_SS_READ operation.
 */
int handle_ss_read(int client_fd, MessageHeader* header) {
    // Log read request
    char details[1200];
    snprintf(details, sizeof(details), "file=%s user=%s", header->filename, header->username);
    log_message("SS", "INFO", details);
    
    char* content = NULL;
    int result = ss_read_file(header->filename, &content);
    
    if (result == ERR_SUCCESS) {
        char msg[1200];
        snprintf(msg, sizeof(msg), "✓ File '%s' read successfully (%zu bytes)", 
                 header->filename, content ? strlen(content) : 0);
        log_message("SS", "INFO", msg);
        
        MessageHeader resp;
        memset(&resp, 0, sizeof(resp));
        resp.msg_type = MSG_RESPONSE;
        resp.error_code = ERR_SUCCESS;
        resp.data_length = strlen(content);
        send_message(client_fd, &resp, content);
        free(content);
    } else {
        send_simple_response(client_fd, MSG_ERROR, result);
    }
    return result;
}

/**
 * handle_ss_write_lock
 * @brief Handler for OP_SS_WRITE_LOCK operation.
 */
void handle_ss_write_lock(int client_fd, MessageHeader* header) {
    int result = ss_write_lock(header->filename, header->sentence_index, header->username);
    send_simple_response(client_fd, 
                        (result == ERR_SUCCESS) ? MSG_ACK : MSG_ERROR, 
                        result);
}

/**
 * handle_ss_write_word
 * @brief Handler for OP_SS_WRITE_WORD operation.
 *
 * Parses payload format: "word_index <new_word...>" and updates the word.
 */
void handle_ss_write_word(int client_fd, MessageHeader* header, const char* payload) {
    if (!payload) {
        send_simple_response(client_fd, MSG_ERROR, ERR_INVALID_WORD);
        return;
    }

    char* space_ptr = strchr(payload, ' ');
    if (!space_ptr) {
        send_simple_response(client_fd, MSG_ERROR, ERR_INVALID_WORD);
        return;
    }

    int word_idx = atoi(payload);
    space_ptr++; // move to start of new_word
    while (*space_ptr == ' ' || *space_ptr == '\t') space_ptr++;

    // Trim trailing newline/carriage return
    char* endp = space_ptr + strlen(space_ptr) - 1;
    while (endp >= space_ptr && (*endp == '\n' || *endp == '\r')) {
        *endp = '\0';
        endp--;
    }

    char* new_word = strdup(space_ptr);
    if (!new_word) {
        send_simple_response(client_fd, MSG_ERROR, ERR_FILE_OPERATION_FAILED);
        return;
    }

    int result = ss_write_word(header->filename, header->sentence_index,
                               word_idx, new_word, header->username);
    free(new_word);

    send_simple_response(client_fd, 
                        (result == ERR_SUCCESS) ? MSG_ACK : MSG_ERROR, 
                        result);
}

/**
 * handle_ss_write_unlock
 * @brief Handler for OP_SS_WRITE_UNLOCK operation.
 */
void handle_ss_write_unlock(int client_fd, MessageHeader* header) {
    int result = ss_write_unlock(header->filename, header->sentence_index, header->username);
    send_simple_response(client_fd, 
                        (result == ERR_SUCCESS) ? MSG_ACK : MSG_ERROR, 
                        result);
}

/**
 * handle_ss_info
 * @brief Handler for OP_INFO operation - returns detailed file information
 */
void handle_ss_info(int client_fd, MessageHeader* header) {
    long size;
    int words, chars;
    int result = ss_get_file_info(header->filename, &size, &words, &chars);

    if (result == ERR_SUCCESS) {
        // Get basic metadata
        char metapath[MAX_PATH];
        char owner[MAX_USERNAME] = "unknown";
        long created = 0;
        long modified = 0;
        
        if (ss_build_filepath(metapath, sizeof(metapath), header->filename, ".meta") == ERR_SUCCESS) {
            FILE* mf = fopen(metapath, "r");
            if (mf) {
                char line[256];
                while (fgets(line, sizeof(line), mf)) {
                    if (strncmp(line, "owner:", 6) == 0) {
                        sscanf(line, "owner:%s", owner);
                    } else if (strncmp(line, "created:", 8) == 0) {
                        sscanf(line, "created:%ld", &created);
                    } else if (strncmp(line, "modified:", 9) == 0) {
                        sscanf(line, "modified:%ld", &modified);
                    }
                }
                fclose(mf);
            }
        }
        
        // Get lock information
        char lock_info[4096];
        int active_locks = get_file_locks(header->filename, lock_info, sizeof(lock_info));
        
        // Get statistics
        char stats_info[2048];
        get_file_stats(header->filename, stats_info, sizeof(stats_info));
        
        // Format timestamps as human-readable dates
        char created_str[64] = "Unknown";
        char modified_str[64] = "Unknown";
        
        if (created > 0) {
            time_t created_time = (time_t)created;
            strftime(created_str, sizeof(created_str), "%Y-%m-%d %H:%M:%S", 
                    localtime(&created_time));
        }
        
        if (modified > 0) {
            time_t modified_time = (time_t)modified;
            strftime(modified_str, sizeof(modified_str), "%Y-%m-%d %H:%M:%S", 
                    localtime(&modified_time));
        }
        
        // Build comprehensive info response with ANSI colors
        char info[8192];
        snprintf(info, sizeof(info),
                "%s%sFile:%s %s\n"
                "%s%sOwner:%s %s\n"
                "%s%sCreated:%s %s\n"
                "%s%sLast Modified:%s %s\n"
                "%s%sSize:%s %ld bytes\n"
                "%s%sWords:%s %d\n"
                "%s%sChars:%s %d\n"
                "\n"
                "%s%s═══ Active Locks (%d) ═══%s\n"
                "%s"
                "\n"
                "%s%s═══ Statistics ═══%s\n"
                "%s\n",
                // File details with colors
                ANSI_BOLD, ANSI_CYAN, ANSI_RESET, header->filename,
                ANSI_BOLD, ANSI_CYAN, ANSI_RESET, owner,
                ANSI_BOLD, ANSI_CYAN, ANSI_RESET, created_str,
                ANSI_BOLD, ANSI_CYAN, ANSI_RESET, modified_str,
                ANSI_BOLD, ANSI_CYAN, ANSI_RESET, size,
                ANSI_BOLD, ANSI_CYAN, ANSI_RESET, words,
                ANSI_BOLD, ANSI_CYAN, ANSI_RESET, chars,
                // Lock section header
                ANSI_BOLD, ANSI_YELLOW, active_locks, ANSI_RESET,
                // Lock info (colorized in get_file_locks)
                lock_info,
                // Stats section header
                ANSI_BOLD, ANSI_GREEN, ANSI_RESET,
                // Stats info (colorized in get_file_stats)
                stats_info);
        
        MessageHeader resp;
        memset(&resp, 0, sizeof(resp));
        resp.msg_type = MSG_RESPONSE;
        resp.error_code = ERR_SUCCESS;
        resp.data_length = strlen(info);
        
        send_message(client_fd, &resp, info);
    } else {
        send_simple_response(client_fd, MSG_ERROR, result);
    }
}

/**
 * handle_ss_undo
 * @brief Handler for OP_UNDO operation.
 */
void handle_ss_undo(int client_fd, MessageHeader* header) {
    int result = ss_undo_file(header->filename);
    send_simple_response(client_fd, 
                        (result == ERR_SUCCESS) ? MSG_ACK : MSG_ERROR, 
                        result);
}

/**
 * handle_ss_move
 * @brief Handler for OP_SS_MOVE operation.
 */
void handle_ss_move(int client_fd, MessageHeader* header, const char* payload) {
    if (!payload) {
        send_simple_response(client_fd, MSG_ERROR, ERR_FILE_OPERATION_FAILED);
        return;
    }
    
    // header->filename contains old path
    // payload contains new path
    int result = ss_move_file(header->filename, payload);
    send_simple_response(client_fd, 
                        (result == ERR_SUCCESS) ? MSG_ACK : MSG_ERROR, 
                        result);
}

/**
 * handle_ss_checkpoint
 * @brief Handler for OP_SS_CHECKPOINT operation.
 */
void handle_ss_checkpoint(int client_fd, MessageHeader* header) {
    int result = ss_create_checkpoint(header->filename, header->checkpoint_tag);
    send_simple_response(client_fd, 
                        (result == ERR_SUCCESS) ? MSG_ACK : MSG_ERROR, 
                        result);
}

/**
 * handle_ss_viewcheckpoint
 * @brief Handler for OP_SS_VIEWCHECKPOINT operation.
 */
void handle_ss_viewcheckpoint(int client_fd, MessageHeader* header) {
    char* content = NULL;
    int result = ss_view_checkpoint(header->filename, header->checkpoint_tag, &content);
    
    if (result == ERR_SUCCESS && content) {
        MessageHeader response;
        memset(&response, 0, sizeof(response));
        response.msg_type = MSG_RESPONSE;
        response.error_code = ERR_SUCCESS;
        response.data_length = strlen(content);
        send_message(client_fd, &response, content);
        free(content);
    } else {
        send_simple_response(client_fd, MSG_ERROR, result);
        if (content) free(content);
    }
}

/**
 * handle_ss_revert
 * @brief Handler for OP_SS_REVERT operation.
 */
void handle_ss_revert(int client_fd, MessageHeader* header) {
    int result = ss_revert_checkpoint(header->filename, header->checkpoint_tag);
    send_simple_response(client_fd, 
                        (result == ERR_SUCCESS) ? MSG_ACK : MSG_ERROR, 
                        result);
}

/**
 * handle_ss_listcheckpoints
 * @brief Handler for OP_SS_LISTCHECKPOINTS operation.
 */
void handle_ss_listcheckpoints(int client_fd, MessageHeader* header) {
    char* checkpoint_list = NULL;
    int result = ss_list_checkpoints(header->filename, &checkpoint_list);
    
    if (result == ERR_SUCCESS && checkpoint_list) {
        MessageHeader response;
        memset(&response, 0, sizeof(response));
        response.msg_type = MSG_RESPONSE;
        response.error_code = ERR_SUCCESS;
        response.data_length = strlen(checkpoint_list);
        send_message(client_fd, &response, checkpoint_list);
        free(checkpoint_list);
    } else {
        send_simple_response(client_fd, MSG_ERROR, result);
        if (checkpoint_list) free(checkpoint_list);
    }
}

/**
 * handle_client_request
 * @brief Thread entrypoint for per-client connections to the Storage Server.
 *
 * Receives requests and dispatches them to individual handler functions.
 * Maintains connection for WRITE sessions (LOCK -> WORD edits -> UNLOCK).
 *
 * @param arg Pointer to an allocated int containing the accepted socket fd.
 * @return Always returns NULL when the thread exits.
 */
void* handle_client_request(void* arg) {
    int client_fd = *(int*)arg;
    free(arg);
    
    // Get client IP and port for logging
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    char client_ip[MAX_IP] = "unknown";
    int client_port = 0;
    if (getpeername(client_fd, (struct sockaddr*)&client_addr, &addr_len) == 0) {
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        client_port = ntohs(client_addr.sin_port);
    }
    
    MessageHeader header;
    char* payload = NULL;
    int keep_alive = 1;  // Keep connection open for WRITE sessions
    
    // Handle multiple requests on same connection
    while (keep_alive && recv_message(client_fd, &header, &payload) > 0) {
        const char* operation = "UNKNOWN";
        char details[1200];
        int result_code = ERR_SUCCESS;
        
        // Determine operation name
        switch (header.op_code) {
            case OP_SS_CREATE: operation = "CREATE"; break;
            case OP_SS_DELETE: operation = "DELETE"; break;
            case OP_SS_READ: operation = "READ"; break;
            case OP_SS_WRITE_LOCK: operation = "WRITE_LOCK"; break;
            case OP_SS_WRITE_WORD: operation = "WRITE_WORD"; break;
            case OP_SS_WRITE_UNLOCK: operation = "WRITE_UNLOCK"; break;
            case OP_STREAM: operation = "STREAM"; break;
            case OP_UNDO: operation = "UNDO"; break;
            case OP_INFO: operation = "INFO"; break;
            case OP_VIEW: operation = "VIEW"; break;
            case OP_SS_MOVE: operation = "MOVE"; break;
            case OP_SS_CHECKPOINT: operation = "CHECKPOINT"; break;
            case OP_SS_VIEWCHECKPOINT: operation = "VIEW_CHECKPOINT"; break;
            case OP_SS_REVERT: operation = "REVERT"; break;
            case OP_SS_LISTCHECKPOINTS: operation = "LIST_CHECKPOINTS"; break;
            case OP_EXEC: operation = "EXEC"; break;
            default: operation = "UNKNOWN"; break;
        }
        
        // Initialize default details
        if (header.filename[0]) {
            snprintf(details, sizeof(details), "file=%s", header.filename);
        } else {
            details[0] = '\0';
        }
        
        // Log request received
        log_operation("SS", "INFO", operation, header.username[0] ? header.username : "system",
                     client_ip, client_port, details, 0);
        
        switch (header.op_code) {
            case OP_SS_CREATE:
                result_code = handle_ss_create(client_fd, &header, payload);
                keep_alive = 0;  // Single-shot operation
                break;
            
            case OP_SS_DELETE:
                result_code = handle_ss_delete(client_fd, &header);
                keep_alive = 0;
                break;
            
            case OP_SS_READ:
                result_code = handle_ss_read(client_fd, &header);
                keep_alive = 0;
                break;
            
            case OP_SS_WRITE_LOCK:
                result_code = ERR_SUCCESS;  // Will be handled by write_lock handler
                handle_ss_write_lock(client_fd, &header);
                // Keep connection alive for subsequent WRITE_WORD commands
                break;
            
            case OP_SS_WRITE_WORD:
                result_code = ERR_SUCCESS;  // Will be handled by write_word handler
                handle_ss_write_word(client_fd, &header, payload);
                // Keep connection alive for more WRITE_WORD or WRITE_UNLOCK
                break;
            
            case OP_SS_WRITE_UNLOCK:
                result_code = ERR_SUCCESS;  // Will be handled by write_unlock handler
                handle_ss_write_unlock(client_fd, &header);
                keep_alive = 0;  // End of WRITE session
                break;
            
            case OP_STREAM:
                ss_stream_file(client_fd, header.filename);
                keep_alive = 0;
                break;
            
            case OP_INFO:
                handle_ss_info(client_fd, &header);
                keep_alive = 0;
                break;
            
            case OP_UNDO:
                handle_ss_undo(client_fd, &header);
                keep_alive = 0;
                break;
            
            case OP_SS_MOVE:
                handle_ss_move(client_fd, &header, payload);
                keep_alive = 0;
                break;
            
            case OP_SS_CHECKPOINT:
                handle_ss_checkpoint(client_fd, &header);
                keep_alive = 0;
                break;
            
            case OP_SS_VIEWCHECKPOINT:
                handle_ss_viewcheckpoint(client_fd, &header);
                keep_alive = 0;
                break;
            
            case OP_SS_REVERT:
                handle_ss_revert(client_fd, &header);
                keep_alive = 0;
                break;
            
            case OP_SS_LISTCHECKPOINTS:
                handle_ss_listcheckpoints(client_fd, &header);
                keep_alive = 0;
                break;
            
            default:
                // Unknown operation - close connection
                result_code = ERR_INVALID_COMMAND;
                snprintf(details, sizeof(details), "Invalid operation code");
                keep_alive = 0;
                break;
        }
        
        // Log the completed operation
        log_operation("SS", result_code == ERR_SUCCESS ? "INFO" : "ERROR",
                     operation, header.username[0] ? header.username : "system",
                     client_ip, client_port, details, result_code);
        
        if (payload) {
            free(payload);
            payload = NULL;
        }
    }
    
    // Log client disconnection
    char disconnect_msg[512];
    snprintf(disconnect_msg, sizeof(disconnect_msg), 
             "Client connection closed from %s:%d", client_ip, client_port);
    log_operation("SS", "INFO", "CLIENT_DISCONNECT", "system",
                 client_ip, client_port, disconnect_msg, ERR_SUCCESS);
    
    close(client_fd);
    return NULL;
}
