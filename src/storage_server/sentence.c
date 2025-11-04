#include "common.h"
#include "storage_server.h"

extern SSConfig config;

// Parse text into sentences
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
            
            // Initialize lock
            pthread_mutex_init(&(*sentences)[count].lock, NULL);
            (*sentences)[count].locked_by[0] = '\0';
            (*sentences)[count].is_locked = 0;
            
            count++;
            
            // Move past delimiter and whitespace
            p++;
            while (*p == ' ' || *p == '\n' || *p == '\t') p++;
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
        pthread_mutex_init(&(*sentences)[count].lock, NULL);
        (*sentences)[count].locked_by[0] = '\0';
        (*sentences)[count].is_locked = 0;
        count++;
    }
    
    return count;
}

// Lock a sentence for writing
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

// Unlock a sentence
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

// Free sentence structures
void free_sentences(Sentence* sentences, int count) {
    for (int i = 0; i < count; i++) {
        if (sentences[i].text) {
            free(sentences[i].text);
        }
        pthread_mutex_destroy(&sentences[i].lock);
    }
    free(sentences);
}

// Write lock operation
int ss_write_lock(const char* filename, int sentence_idx, const char* username) {
    char filepath[MAX_PATH];
    snprintf(filepath, sizeof(filepath), "%s/%s", config.storage_dir, filename);
    
    if (!file_exists(filepath)) {
        return ERR_FILE_NOT_FOUND;
    }
    
    // Read current content
    char* content = read_file_content(filepath);
    if (!content) {
        return ERR_FILE_OPERATION_FAILED;
    }
    
    // Parse into sentences
    Sentence* sentences;
    int count = parse_sentences(content, &sentences);
    free(content);
    
    if (sentence_idx < 0 || sentence_idx >= count) {
        free_sentences(sentences, count);
        return ERR_INVALID_SENTENCE;
    }
    
    // Try to lock the sentence
    FileWithSentences file;
    strcpy(file.filename, filename);
    file.sentences = sentences;
    file.sentence_count = count;
    
    int result = lock_sentence(&file, sentence_idx, username);
    
    // For now, we keep sentences in memory during write session
    // In a production system, you'd maintain a global registry
    
    return result;
}

// Write word operation
int ss_write_word(const char* filename, int sentence_idx, int word_idx, 
                  const char* new_word, const char* username) {
    char filepath[MAX_PATH];
    snprintf(filepath, sizeof(filepath), "%s/%s", config.storage_dir, filename);
    
    // Save undo snapshot before first write
    ss_save_undo(filename);
    
    // Read content
    char* content = read_file_content(filepath);
    if (!content) {
        return ERR_FILE_OPERATION_FAILED;
    }
    
    // Parse sentences
    Sentence* sentences;
    int count = parse_sentences(content, &sentences);
    free(content);
    
    if (sentence_idx < 0 || sentence_idx >= count) {
        free_sentences(sentences, count);
        return ERR_INVALID_SENTENCE;
    }
    
    // Verify lock
    if (!sentences[sentence_idx].is_locked || 
        strcmp(sentences[sentence_idx].locked_by, username) != 0) {
        free_sentences(sentences, count);
        return ERR_PERMISSION_DENIED;
    }
    
    // Split sentence into words
    char* sentence_text = strdup(sentences[sentence_idx].text);
    char* words[100];
    int word_count = 0;
    
    char* token = strtok(sentence_text, " \t\n");
    while (token != NULL && word_count < 100) {
        words[word_count++] = strdup(token);
        token = strtok(NULL, " \t\n");
    }
    
    if (word_idx < 0 || word_idx >= word_count) {
        free(sentence_text);
        for (int i = 0; i < word_count; i++) free(words[i]);
        free_sentences(sentences, count);
        return ERR_INVALID_WORD;
    }
    
    // Replace the word
    free(words[word_idx]);
    words[word_idx] = strdup(new_word);
    
    // Rebuild sentence
    char new_sentence[BUFFER_SIZE] = "";
    for (int i = 0; i < word_count; i++) {
        strcat(new_sentence, words[i]);
        if (i < word_count - 1) strcat(new_sentence, " ");
    }
    
    // Update sentence in array
    free(sentences[sentence_idx].text);
    sentences[sentence_idx].text = strdup(new_sentence);
    
    // Rebuild full file content
    char new_content[BUFFER_SIZE * 10] = "";
    for (int i = 0; i < count; i++) {
        strcat(new_content, sentences[i].text);
        if (i < count - 1) strcat(new_content, " ");
    }
    
    // Write back to file
    write_file_content(filepath, new_content);
    
    // Cleanup
    free(sentence_text);
    for (int i = 0; i < word_count; i++) free(words[i]);
    free_sentences(sentences, count);
    
    return ERR_SUCCESS;
}

// Write unlock operation
int ss_write_unlock(const char* filename, int sentence_idx, const char* username) {
    char filepath[MAX_PATH];
    snprintf(filepath, sizeof(filepath), "%s/%s", config.storage_dir, filename);
    
    if (!file_exists(filepath)) {
        return ERR_FILE_NOT_FOUND;
    }
    
    // Parse and re-split based on delimiters after writes
    char* content = read_file_content(filepath);
    if (!content) {
        return ERR_FILE_OPERATION_FAILED;
    }
    
    // Re-parse to handle new delimiters created during editing
    Sentence* sentences;
    int count = parse_sentences(content, &sentences);
    
    // Write final content back
    write_file_content(filepath, content);
    free(content);
    
    // Update metadata
    char metapath[MAX_PATH];
    snprintf(metapath, sizeof(metapath), "%s/%s.meta", config.storage_dir, filename);
    FILE* f = fopen(metapath, "a");
    if (f) {
        fprintf(f, "modified:%ld\n", time(NULL));
        fclose(f);
    }
    
    free_sentences(sentences, count);
    
    char msg[256];
    snprintf(msg, sizeof(msg), "Write completed: %s by %s", filename, username);
    log_message("SS", "INFO", msg);
    
    return ERR_SUCCESS;
}

// Save undo snapshot
int ss_save_undo(const char* filename) {
    char filepath[MAX_PATH];
    snprintf(filepath, sizeof(filepath), "%s/%s", config.storage_dir, filename);
    
    char* content = read_file_content(filepath);
    if (!content) {
        return ERR_FILE_OPERATION_FAILED;
    }
    
    char undopath[MAX_PATH];
    snprintf(undopath, sizeof(undopath), "%s/%s.undo", config.storage_dir, filename);
    
    int result = write_file_content(undopath, content);
    free(content);
    
    return result == 0 ? ERR_SUCCESS : ERR_FILE_OPERATION_FAILED;
}

// Undo file changes
int ss_undo_file(const char* filename) {
    char undopath[MAX_PATH];
    snprintf(undopath, sizeof(undopath), "%s/%s.undo", config.storage_dir, filename);
    
    if (!file_exists(undopath)) {
        return ERR_UNDO_NOT_AVAILABLE;
    }
    
    char* undo_content = read_file_content(undopath);
    if (!undo_content) {
        return ERR_FILE_OPERATION_FAILED;
    }
    
    char filepath[MAX_PATH];
    snprintf(filepath, sizeof(filepath), "%s/%s", config.storage_dir, filename);
    
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

// Stream file word by word
int ss_stream_file(int client_socket, const char* filename) {
    char filepath[MAX_PATH];
    snprintf(filepath, sizeof(filepath), "%s/%s", config.storage_dir, filename);
    
    char* content = read_file_content(filepath);
    if (!content) {
        return ERR_FILE_NOT_FOUND;
    }
    
    // Tokenize and send word by word
    char* word = strtok(content, " \t\n");
    while (word != NULL) {
        MessageHeader header;
        memset(&header, 0, sizeof(header));
        header.msg_type = MSG_RESPONSE;
        header.data_length = strlen(word);
        
        send_message(client_socket, &header, word);
        
        // Sleep for 0.1 seconds
        usleep(100000);
        
        word = strtok(NULL, " \t\n");
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

// Handle client requests
void* handle_client_request(void* arg) {
    int client_fd = *(int*)arg;
    free(arg);
    
    MessageHeader header;
    char* payload;
    
    if (recv_message(client_fd, &header, &payload) > 0) {
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
                break;
            }
            
            case OP_SS_WRITE_WORD: {
                // Payload format: "word_index new_word"
                int word_idx;
                char new_word[256];
                sscanf(payload, "%d %s", &word_idx, new_word);
                
                int result = ss_write_word(header.filename, header.sentence_index, 
                                          word_idx, new_word, header.username);
                header.msg_type = (result == ERR_SUCCESS) ? MSG_ACK : MSG_ERROR;
                header.error_code = result;
                header.data_length = 0;
                send_message(client_fd, &header, NULL);
                break;
            }
            
            case OP_SS_WRITE_UNLOCK: {
                int result = ss_write_unlock(header.filename, header.sentence_index, header.username);
                header.msg_type = (result == ERR_SUCCESS) ? MSG_ACK : MSG_ERROR;
                header.error_code = result;
                header.data_length = 0;
                send_message(client_fd, &header, NULL);
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
                break;
            }
        }
        
        if (payload) free(payload);
        if (response_payload) free(response_payload);
    }
    
    close(client_fd);
    return NULL;
}
