#include "common.h"
#include "name_server.h"
#include <ctype.h>

/**
 * ============================================================================
 * TRIE OPERATIONS FOR EFFICIENT FILE SEARCH
 * ============================================================================
 * Trie provides O(m) search time where m is the length of the filename
 * Much better than O(N) linear search where N is the number of files
 */

/**
 * trie_create_node
 * @brief Create and initialize a new Trie node.
 */
TrieNode* trie_create_node(void) {
    TrieNode* node = (TrieNode*)malloc(sizeof(TrieNode));
    if (!node) return NULL;
    
    node->file_index = -1;
    node->is_end_of_path = 0;
    
    for (int i = 0; i < 256; i++) {
        node->children[i] = NULL;
    }
    
    return node;
}

/**
 * trie_insert
 * @brief Insert a file path into the Trie with its array index.
 *
 * @param root Root of the Trie
 * @param path Full file path (folder_path/filename or just filename)
 * @param file_index Index in the files array
 */
void trie_insert(TrieNode* root, const char* path, int file_index) {
    if (!root || !path) return;
    
    TrieNode* current = root;
    const char* p = path;
    
    while (*p) {
        unsigned char ch = (unsigned char)*p;
        
        if (!current->children[ch]) {
            current->children[ch] = trie_create_node();
            if (!current->children[ch]) return;  // Allocation failed
        }
        
        current = current->children[ch];
        p++;
    }
    
    current->is_end_of_path = 1;
    current->file_index = file_index;
}

/**
 * trie_search
 * @brief Search for a file path in the Trie.
 *
 * @param root Root of the Trie
 * @param path Full file path to search
 * @return File index if found, -1 otherwise
 */
int trie_search(TrieNode* root, const char* path) {
    if (!root || !path) return -1;
    
    TrieNode* current = root;
    const char* p = path;
    
    while (*p) {
        unsigned char ch = (unsigned char)*p;
        
        if (!current->children[ch]) {
            return -1;  // Path not found
        }
        
        current = current->children[ch];
        p++;
    }
    
    if (current->is_end_of_path) {
        return current->file_index;
    }
    
    return -1;
}

/**
 * trie_delete_helper
 * @brief Helper function to recursively delete a path from Trie.
 */
static int trie_delete_helper(TrieNode* node, const char* path, int depth, int len) {
    if (!node) return 0;
    
    // Base case: reached end of path
    if (depth == len) {
        if (node->is_end_of_path) {
            node->is_end_of_path = 0;
            node->file_index = -1;
        }
        
        // Check if node has any children
        for (int i = 0; i < 256; i++) {
            if (node->children[i]) return 0;
        }
        
        return 1;  // Can delete this node
    }
    
    unsigned char ch = (unsigned char)path[depth];
    if (trie_delete_helper(node->children[ch], path, depth + 1, len)) {
        free(node->children[ch]);
        node->children[ch] = NULL;
        
        // If node doesn't mark end of path and has no children
        if (!node->is_end_of_path) {
            for (int i = 0; i < 256; i++) {
                if (node->children[i]) return 0;
            }
            return 1;
        }
    }
    
    return 0;
}

/**
 * trie_delete
 * @brief Delete a file path from the Trie.
 */
void trie_delete(TrieNode* root, const char* path) {
    if (!root || !path) return;
    trie_delete_helper(root, path, 0, strlen(path));
}

/**
 * trie_free
 * @brief Free all memory used by the Trie.
 */
void trie_free(TrieNode* root) {
    if (!root) return;
    
    for (int i = 0; i < 256; i++) {
        if (root->children[i]) {
            trie_free(root->children[i]);
        }
    }
    
    free(root);
}

/**
 * ============================================================================
 * LRU CACHE OPERATIONS FOR FAST REPEATED LOOKUPS
 * ============================================================================
 * LRU cache provides O(1) lookups for recently accessed files
 * Combined with Trie, gives best performance for real-world access patterns
 */

/**
 * cache_create
 * @brief Create and initialize an LRU cache.
 */
LRUCache* cache_create(int capacity) {
    LRUCache* cache = (LRUCache*)malloc(sizeof(LRUCache));
    if (!cache) return NULL;
    
    cache->head = NULL;
    cache->tail = NULL;
    cache->size = 0;
    cache->capacity = capacity;
    cache->hits = 0;
    cache->misses = 0;
    
    pthread_mutex_init(&cache->lock, NULL);
    
    for (int i = 0; i < LRU_CACHE_SIZE; i++) {
        cache->nodes[i] = NULL;
    }
    
    return cache;
}

/**
 * cache_hash
 * @brief Simple hash function for cache key.
 */
static unsigned int cache_hash(const char* key) {
    unsigned int hash = 5381;
    int c;
    
    while ((c = *key++)) {
        hash = ((hash << 5) + hash) + c;
    }
    
    return hash % LRU_CACHE_SIZE;
}

/**
 * cache_remove_node
 * @brief Remove a node from the doubly linked list.
 */
static void cache_remove_node(LRUCache* cache, CacheNode* node) {
    if (!node) return;
    
    if (node->prev) {
        node->prev->next = node->next;
    } else {
        cache->head = node->next;
    }
    
    if (node->next) {
        node->next->prev = node->prev;
    } else {
        cache->tail = node->prev;
    }
}

/**
 * cache_add_to_head
 * @brief Add a node to the head of the list (most recently used).
 */
static void cache_add_to_head(LRUCache* cache, CacheNode* node) {
    node->next = cache->head;
    node->prev = NULL;
    
    if (cache->head) {
        cache->head->prev = node;
    }
    
    cache->head = node;
    
    if (!cache->tail) {
        cache->tail = node;
    }
}

/**
 * cache_get
 * @brief Get file index from cache.
 *
 * @return File index if found, -1 otherwise
 */
int cache_get(LRUCache* cache, const char* key) {
    if (!cache || !key) return -1;
    
    pthread_mutex_lock(&cache->lock);
    
    unsigned int hash = cache_hash(key);
    CacheNode* node = cache->nodes[hash];
    
    // Check if key exists in this hash bucket
    CacheNode* current = node;
    while (current) {
        if (strcmp(current->key, key) == 0) {
            // Found - move to head (most recently used)
            cache_remove_node(cache, current);
            cache_add_to_head(cache, current);
            current->last_access = time(NULL);
            
            cache->hits++;
            
            int result = current->file_index;
            pthread_mutex_unlock(&cache->lock);
            return result;
        }
        current = current->next;
    }
    
    cache->misses++;
    pthread_mutex_unlock(&cache->lock);
    return -1;
}

/**
 * cache_put
 * @brief Insert or update a file index in the cache.
 */
void cache_put(LRUCache* cache, const char* key, int file_index) {
    if (!cache || !key) return;
    
    pthread_mutex_lock(&cache->lock);
    
    unsigned int hash = cache_hash(key);
    
    // Check if key already exists
    CacheNode* current = cache->nodes[hash];
    while (current) {
        if (strcmp(current->key, key) == 0) {
            // Update existing entry
            current->file_index = file_index;
            current->last_access = time(NULL);
            cache_remove_node(cache, current);
            cache_add_to_head(cache, current);
            pthread_mutex_unlock(&cache->lock);
            return;
        }
        current = current->next;
    }
    
    // Create new node
    CacheNode* new_node = (CacheNode*)malloc(sizeof(CacheNode));
    if (!new_node) {
        pthread_mutex_unlock(&cache->lock);
        return;
    }
    
    strncpy(new_node->key, key, MAX_PATH - 1);
    new_node->key[MAX_PATH - 1] = '\0';
    new_node->file_index = file_index;
    new_node->last_access = time(NULL);
    new_node->prev = NULL;
    new_node->next = cache->nodes[hash];
    
    if (cache->nodes[hash]) {
        cache->nodes[hash]->prev = new_node;
    }
    cache->nodes[hash] = new_node;
    
    cache_add_to_head(cache, new_node);
    cache->size++;
    
    // Evict LRU if capacity exceeded
    if (cache->size > cache->capacity) {
        CacheNode* lru = cache->tail;
        if (lru) {
            cache_remove_node(cache, lru);
            
            // Remove from hash table
            unsigned int lru_hash = cache_hash(lru->key);
            if (cache->nodes[lru_hash] == lru) {
                cache->nodes[lru_hash] = lru->next;
            } else {
                CacheNode* prev = cache->nodes[lru_hash];
                while (prev && prev->next != lru) {
                    prev = prev->next;
                }
                if (prev) {
                    prev->next = lru->next;
                }
            }
            
            free(lru);
            cache->size--;
        }
    }
    
    pthread_mutex_unlock(&cache->lock);
}

/**
 * cache_invalidate
 * @brief Remove an entry from the cache.
 */
void cache_invalidate(LRUCache* cache, const char* key) {
    if (!cache || !key) return;
    
    pthread_mutex_lock(&cache->lock);
    
    unsigned int hash = cache_hash(key);
    CacheNode* current = cache->nodes[hash];
    CacheNode* prev = NULL;
    
    while (current) {
        if (strcmp(current->key, key) == 0) {
            // Remove from linked list
            cache_remove_node(cache, current);
            
            // Remove from hash table
            if (prev) {
                prev->next = current->next;
            } else {
                cache->nodes[hash] = current->next;
            }
            
            free(current);
            cache->size--;
            break;
        }
        prev = current;
        current = current->next;
    }
    
    pthread_mutex_unlock(&cache->lock);
}

/**
 * cache_free
 * @brief Free all memory used by the cache.
 */
void cache_free(LRUCache* cache) {
    if (!cache) return;
    
    pthread_mutex_lock(&cache->lock);
    
    for (int i = 0; i < LRU_CACHE_SIZE; i++) {
        CacheNode* current = cache->nodes[i];
        while (current) {
            CacheNode* next = current->next;
            free(current);
            current = next;
        }
    }
    
    pthread_mutex_unlock(&cache->lock);
    pthread_mutex_destroy(&cache->lock);
    free(cache);
}

/**
 * cache_print_stats
 * @brief Print cache statistics for monitoring.
 */
void cache_print_stats(LRUCache* cache) {
    if (!cache) return;
    
    pthread_mutex_lock(&cache->lock);
    
    long total = cache->hits + cache->misses;
    double hit_rate = (total > 0) ? (100.0 * cache->hits / total) : 0.0;
    
    char msg[256];
    snprintf(msg, sizeof(msg), 
             "Cache Stats - Size: %d/%d | Hits: %ld | Misses: %ld | Hit Rate: %.2f%%",
             cache->size, cache->capacity, cache->hits, cache->misses, hit_rate);
    log_message("NM", "INFO", msg);
    
    pthread_mutex_unlock(&cache->lock);
}

