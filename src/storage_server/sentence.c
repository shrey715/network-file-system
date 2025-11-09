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
    
    // Check if already locked
    int lock_status = check_lock(filename, sentence_idx, username);
    if (lock_status == -1) {
        return ERR_SENTENCE_LOCKED; // Locked by someone else
    }
    if (lock_status == 1) {
        return ERR_SUCCESS; // Already locked by this user
    }

    ss_save_undo(filename);
    
    // Read current content
    char* content = read_file_content(filepath);
    if (!content) {
        return ERR_FILE_OPERATION_FAILED;
    }
    
    // Parse into sentences
    Sentence* sentences;
    int count = parse_sentences(content, &sentences);
    
    // SPECIAL CASE: Empty file and requesting sentence 0
    if (count == 0 && sentence_idx == 0) {
        free(content);
        
        // Create a single empty sentence
        sentences = (Sentence*)malloc(sizeof(Sentence));
        sentences[0].text = strdup("");
        sentences[0].trailing_ws = strdup("");
        pthread_mutex_init(&sentences[0].lock, NULL);
        sentences[0].locked_by[0] = '\0';
        sentences[0].is_locked = 0;
        count = 1;
        
        // Add to global lock registry
        int result = add_locked_file(filename, username, sentence_idx, sentences, count);
        
        if (result != 0) {
            free_sentences(sentences, count);
            return ERR_FILE_OPERATION_FAILED;
        }
        
        char msg[256];
        snprintf(msg, sizeof(msg), "Locked empty file %s (sentence 0) by %s", 
                 filename, username);
        log_message("SS", "INFO", msg);
        
        return ERR_SUCCESS;
    }
    
    // SPECIAL CASE: Appending a new sentence (sentence_idx == count)
    // This is ONLY allowed if the last sentence ends with a delimiter
    if (sentence_idx == count && count > 0) {
        // Check if the last sentence ends with a delimiter
        char* last_text = sentences[count - 1].text;
        int last_len = strlen(last_text);
        
        int has_delimiter = 0;
        if (last_len > 0) {
            char last_char = last_text[last_len - 1];
            if (last_char == '.' || last_char == '!' || last_char == '?') {
                has_delimiter = 1;
            }
        }
        
        if (!has_delimiter) {
            // Last sentence doesn't end with delimiter - cannot append
            free(content);
            free_sentences(sentences, count);
            return ERR_INVALID_SENTENCE;
        }
        
        free(content);
        
        // Expand sentence array to include new empty sentence
        sentences = (Sentence*)realloc(sentences, sizeof(Sentence) * (count + 1));
        sentences[count].text = strdup("");
        sentences[count].trailing_ws = strdup("");
        pthread_mutex_init(&sentences[count].lock, NULL);
        sentences[count].locked_by[0] = '\0';
        sentences[count].is_locked = 0;
        count++;
        
        // Add to global lock registry
        int result = add_locked_file(filename, username, sentence_idx, sentences, count);
        
        if (result != 0) {
            free_sentences(sentences, count);
            return ERR_FILE_OPERATION_FAILED;
        }
        
        char msg[256];
        snprintf(msg, sizeof(msg), "Locked new sentence %d in %s by %s (appending)", 
                 sentence_idx, filename, username);
        log_message("SS", "INFO", msg);
        
        return ERR_SUCCESS;
    }
    
    free(content);
    
    if (sentence_idx < 0 || sentence_idx >= count) {
        free_sentences(sentences, count);
        return ERR_INVALID_SENTENCE;
    }
    
    // Add to global lock registry
    int result = add_locked_file(filename, username, sentence_idx, sentences, count);
    
    if (result != 0) {
        free_sentences(sentences, count);
        return ERR_FILE_OPERATION_FAILED;
    }
    
    char msg[256];
    snprintf(msg, sizeof(msg), "Locked sentence %d in %s by %s", 
             sentence_idx, filename, username);
    log_message("SS", "INFO", msg);
    
    return ERR_SUCCESS;
}

/**
 * ss_write_word
 * @brief Replace a single word inside a sentence and persist the file.
 *
 * Performs an undo snapshot before modification, verifies the caller holds
 * the sentence lock, modifies the requested word, rebuilds the file text,
 * and writes it back atomically.
 *
 * @param filename Target filename.
 * @param sentence_idx Sentence index to edit (0-based).
 * @param word_idx Word index inside the sentence to replace (0-based).
 * @param new_word New null-terminated word to insert.
 * @param username Username performing the edit (must hold lock).
 * @return ERR_SUCCESS on success or an ERR_* code for errors.
 */
int ss_write_word(const char* filename, int sentence_idx, int word_idx, 
                  const char* new_word, const char* username) {
    char filepath[MAX_PATH];
    if (ss_build_filepath(filepath, sizeof(filepath), filename, NULL) != ERR_SUCCESS) {
        return ERR_FILE_OPERATION_FAILED;
    }
    
    // Check lock using global registry
    int lock_status = check_lock(filename, sentence_idx, username);
    if (lock_status != 1) {
        return lock_status == -1 ? ERR_SENTENCE_LOCKED : ERR_PERMISSION_DENIED;
    }
    
    // Get locked sentence from registry
    int locked_count = 0;
    Sentence* locked_sentences = get_locked_sentence(filename, username, &locked_count);
    
    if (!locked_sentences || sentence_idx >= locked_count) {
        return ERR_INVALID_SENTENCE;
    }
    
    // Get the target sentence
    Sentence* target_sentence = &locked_sentences[sentence_idx];
    
    // Split sentence into words
    char sentence_copy[BUFFER_SIZE];
    strncpy(sentence_copy, target_sentence->text, BUFFER_SIZE - 1);
    sentence_copy[BUFFER_SIZE - 1] = '\0';
    
    char* words[100];
    int word_count = 0;
    
    // SPECIAL CASE: Empty sentence
    if (strlen(sentence_copy) == 0) {
        // Allow only word_idx 0 for empty sentence
        if (word_idx != 0) {
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
    
    // Validate word index (allow up to word_count for appending)
    if (word_idx < 0 || word_idx > word_count) {
        for (int i = 0; i < word_count; i++) free(words[i]);
        return ERR_INVALID_WORD;
    }
    
    // Split new_word by spaces to handle multi-word input
    char new_word_copy[BUFFER_SIZE];
    strncpy(new_word_copy, new_word, BUFFER_SIZE - 1);
    new_word_copy[BUFFER_SIZE - 1] = '\0';
    
    char* new_words[100];
    int new_word_count = 0;
    
    char* new_token = strtok(new_word_copy, " \t\n");
    while (new_token != NULL && new_word_count < 100) {
        new_words[new_word_count++] = strdup(new_token);
        new_token = strtok(NULL, " \t\n");
    }
    
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
    
    // Rebuild sentence from final_words
    char new_sentence[BUFFER_SIZE] = "";
    for (int i = 0; i < final_count; i++) {
        if (i > 0) strcat(new_sentence, " ");
        strcat(new_sentence, final_words[i]);
    }
    
    // Clean up final_words
    for (int i = 0; i < final_count; i++) free(final_words[i]);
    
    // Update sentence in locked registry
    free(target_sentence->text);
    target_sentence->text = strdup(new_sentence);
    
    // Rebuild full file content from ALL locked sentences
    char new_content[BUFFER_SIZE * 10] = "";
    for (int i = 0; i < locked_count; i++) {
        if (i > 0) strcat(new_content, " ");
        strcat(new_content, locked_sentences[i].text);
    }
    
    // Write back to file
    write_file_content(filepath, new_content);
    
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
    
    // Check lock ownership
    int lock_status = check_lock(filename, sentence_idx, username);
    if (lock_status != 1) {
        return ERR_PERMISSION_DENIED;
    }
    
    // Read current content
    char* content = read_file_content(filepath);
    if (!content) {
        remove_lock(filename, sentence_idx);
        return ERR_FILE_OPERATION_FAILED;
    }
    
    // RE-PARSE to handle any new delimiters created during editing
    Sentence* sentences;
    int new_count = parse_sentences(content, &sentences);
    
    // Rebuild content with proper sentence formatting
    char final_content[BUFFER_SIZE * 10] = "";
    for (int i = 0; i < new_count; i++) {
        if (i > 0) strcat(final_content, " ");
        strcat(final_content, sentences[i].text);
    }
    
    // Write back properly formatted content
    write_file_content(filepath, final_content);
    
    free(content);
    free_sentences(sentences, new_count);
    
    // Remove lock from global registry
    if (remove_lock(filename, sentence_idx) != 0) {
        return ERR_FILE_OPERATION_FAILED;
    }
    
    // Update metadata
    char metapath[MAX_PATH];
    if (ss_build_filepath(metapath, sizeof(metapath), filename, ".meta") == ERR_SUCCESS) {
        FILE* f = fopen(metapath, "a");
        if (f) {
            fprintf(f, "modified:%ld\n", time(NULL));
            fclose(f);
        }
    }
    
    char msg[256];
    snprintf(msg, sizeof(msg), "Write completed: %s sentence %d by %s (re-parsed to %d sentences)", 
             filename, sentence_idx, username, new_count);
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
        snprintf(msg, sizeof(msg), "Undo performed: %s", filename);
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
    
    // Safely construct the file path
    if (ss_build_filepath(filepath, sizeof(filepath), filename, NULL) != ERR_SUCCESS) {
        return ERR_FILE_OPERATION_FAILED;
    }
    
    char* content = read_file_content(filepath);
    if (!content) {
        return ERR_FILE_NOT_FOUND;
    }
    
    // Tokenize and send word by word
    char* word = strtok(content, " \t");
    while (word != NULL) {
        MessageHeader header;
        memset(&header, 0, sizeof(header));
        header.msg_type = MSG_RESPONSE;
        header.data_length = strlen(word);
        
        send_message(client_socket, &header, word);
        
        // Sleep for 0.1 seconds
        usleep(100000);
        
        word = strtok(NULL, " \t");
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
 * handle_client_request
 * @brief Thread entrypoint for per-client connections to the Storage Server.
 *
 * Receives one request header+payload, executes the corresponding SS_* API
 * call, and sends back an acknowledgement, error, or response. The thread
 * then closes the connection and exits.
 *
 * @param arg Pointer to an allocated int containing the accepted socket fd.
 * @return Always returns NULL when the thread exits.
 */
void* handle_client_request(void* arg) {
    int client_fd = *(int*)arg;
    free(arg);
    
    MessageHeader header;
    char* payload = NULL;
    int keep_alive = 1;  // Keep connection open for WRITE sessions
    
    // Handle multiple requests on same connection
    while (keep_alive && recv_message(client_fd, &header, &payload) > 0) {
        char* response_payload = NULL;
        
        switch (header.op_code) {
            case OP_SS_CREATE: {
                // payload contains owner username
                int result = ss_create_file(header.filename, payload ? payload : "unknown");
                header.msg_type = (result == ERR_SUCCESS) ? MSG_ACK : MSG_ERROR;
                header.error_code = result;
                header.data_length = 0;
                send_message(client_fd, &header, NULL);
                break;
            }
            
            case OP_SS_DELETE: {
                int result = ss_delete_file(header.filename);
                header.msg_type = (result == ERR_SUCCESS) ? MSG_ACK : MSG_ERROR;
                header.error_code = result;
                header.data_length = 0;
                send_message(client_fd, &header, NULL);
                break;
            }
            
            case OP_SS_READ: {
                char* content;
                int result = ss_read_file(header.filename, &content);
                if (result == ERR_SUCCESS) {
                    header.msg_type = MSG_RESPONSE;
                    header.data_length = strlen(content);
                    header.error_code = ERR_SUCCESS;
                    send_message(client_fd, &header, content);
                    free(content);
                } else {
                    header.msg_type = MSG_ERROR;
                    header.error_code = result;
                    header.data_length = 0;
                    send_message(client_fd, &header, NULL);
                }
                break;
            }
            
            case OP_SS_WRITE_LOCK: {
                int result = ss_write_lock(header.filename, header.sentence_index, header.username);
                header.msg_type = (result == ERR_SUCCESS) ? MSG_ACK : MSG_ERROR;
                header.error_code = result;
                header.data_length = 0;
                send_message(client_fd, &header, NULL);
                // Keep connection alive for subsequent WRITE_WORD commands
                // keep_alive = 1 (already set)
                break;
            }
            
            case OP_SS_WRITE_WORD: {
                // Payload format: "word_index <new_word...>" - capture rest of line
                if (!payload) {
                    header.msg_type = MSG_ERROR;
                    header.error_code = ERR_INVALID_WORD;
                    header.data_length = 0;
                    send_message(client_fd, &header, NULL);
                    break;
                }

                char* space_ptr = strchr(payload, ' ');
                if (!space_ptr) {
                    header.msg_type = MSG_ERROR;
                    header.error_code = ERR_INVALID_WORD;
                    header.data_length = 0;
                    send_message(client_fd, &header, NULL);
                    break;
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
                    header.msg_type = MSG_ERROR;
                    header.error_code = ERR_FILE_OPERATION_FAILED;
                    header.data_length = 0;
                    send_message(client_fd, &header, NULL);
                    break;
                }

                int result = ss_write_word(header.filename, header.sentence_index,
                                           word_idx, new_word, header.username);
                free(new_word);

                header.msg_type = (result == ERR_SUCCESS) ? MSG_ACK : MSG_ERROR;
                header.error_code = result;
                header.data_length = 0;
                send_message(client_fd, &header, NULL);
                // Keep connection alive for more WRITE_WORD or WRITE_UNLOCK
                break;
            }
            
            case OP_SS_WRITE_UNLOCK: {
                int result = ss_write_unlock(header.filename, header.sentence_index, header.username);
                header.msg_type = (result == ERR_SUCCESS) ? MSG_ACK : MSG_ERROR;
                header.error_code = result;
                header.data_length = 0;
                send_message(client_fd, &header, NULL);
                // WRITE session complete - close connection
                keep_alive = 0;
                break;
            }
            
            case OP_STREAM: {
                ss_stream_file(client_fd, header.filename);
                break;
            }
            
            case OP_INFO: {
                long size;
                int words, chars;
                int result = ss_get_file_info(header.filename, &size, &words, &chars);
                if (result == ERR_SUCCESS) {
                    char info[256];
                    snprintf(info, sizeof(info), "Size:%ld Words:%d Chars:%d", 
                            size, words, chars);
                    header.msg_type = MSG_RESPONSE;
                    header.data_length = strlen(info);
                    send_message(client_fd, &header, info);
                } else {
                    header.msg_type = MSG_ERROR;
                    header.error_code = result;
                    header.data_length = 0;
                    send_message(client_fd, &header, NULL);
                }
                break;
            }
            
            case OP_UNDO: {
                int result = ss_undo_file(header.filename);
                header.msg_type = (result == ERR_SUCCESS) ? MSG_ACK : MSG_ERROR;
                header.error_code = result;
                header.data_length = 0;
                send_message(client_fd, &header, NULL);
                keep_alive = 0;  // Close after single-shot operations
                break;
            }
            
            default: {
                // Unknown operation - close connection
                keep_alive = 0;
                break;
            }
        }
        
        if (payload) {
            free(payload);
            payload = NULL;
        }
        if (response_payload) {
            free(response_payload);
            response_payload = NULL;
        }
        
        // Close connection for all non-WRITE operations
        if (header.op_code != OP_SS_WRITE_LOCK && 
            header.op_code != OP_SS_WRITE_WORD) {
            keep_alive = 0;
        }
    }
    
    close(client_fd);
    return NULL;
}
