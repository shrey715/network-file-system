#define _POSIX_C_SOURCE 200809L
#include "../../include/common.h"
#include "../../include/client.h"
#include "../../include/cJSON.h"
#include <unistd.h>

// Configure your API Key here or load from ENV
// To use: export GEMINI_API_KEY="your-key-here" before running
static const char* get_api_key(void) {
    const char* key = getenv("GEMINI_API_KEY");
    if (!key || strlen(key) == 0) {
        PRINT_ERR("GEMINI_API_KEY environment variable not set");
        PRINT_INFO("Set it with: export GEMINI_API_KEY='your-key-here'");
        return NULL;
    }
    return key;
}

/**
 * @brief Helper to perform a non-interactive write to a file.
 *        Writes entire content at once using word_idx=-1 (full replacement).
 */
static int auto_write_file(ClientState* state, const char* filename, const char* content) {
    int ss_socket;
    int result = get_storage_server_connection(state, filename, OP_WRITE, &ss_socket);
    if (result != ERR_SUCCESS) return result;

    // 1. Lock Sentence 0 (We assume a new empty file)
    MessageHeader header;
    init_message_header(&header, MSG_REQUEST, OP_SS_WRITE_LOCK, state->username);
    strcpy(header.filename, filename);
    header.sentence_index = 0;
    
    send_message(ss_socket, &header, NULL);
    
    char* response = NULL;
    recv_message(ss_socket, &header, &response);
    if (response) free(response);
    
    if (header.msg_type != MSG_ACK) {
        safe_close_socket(&ss_socket);
        return header.error_code;
    }

    PRINT_INFO("AI Agent: Writing content to %s...", filename);

    // 2. Write entire content at once using word_idx=-1
    init_message_header(&header, MSG_REQUEST, OP_SS_WRITE_WORD, state->username);
    strcpy(header.filename, filename);
    header.sentence_index = 0;

    // Allocate payload: "-1 " + content
    size_t content_len = strlen(content);
    size_t payload_size = content_len + 8;  // "-1 " prefix + safety
    char* payload = malloc(payload_size);
    if (!payload) {
        safe_close_socket(&ss_socket);
        return ERR_FILE_OPERATION_FAILED;
    }
    snprintf(payload, payload_size, "-1 %s", content);
    header.data_length = strlen(payload);

    send_message(ss_socket, &header, payload);
    free(payload);
    
    recv_message(ss_socket, &header, &response);
    if (response) free(response);

    if (header.msg_type != MSG_ACK) {
        PRINT_ERR("Write failed: %s", get_error_message(header.error_code));
        safe_close_socket(&ss_socket);
        return header.error_code;
    }

    // 3. Unlock (ETIRW)
    init_message_header(&header, MSG_REQUEST, OP_SS_WRITE_UNLOCK, state->username);
    strcpy(header.filename, filename);
    header.sentence_index = 0;
    
    send_message(ss_socket, &header, NULL);
    recv_message(ss_socket, &header, &response);
    if (response) free(response);

    safe_close_socket(&ss_socket);
    PRINT_OK("Content written successfully!");
    return ERR_SUCCESS;
}

/**
 * @brief Calls Gemini API using curl via popen to generate file content.
 * @param state Client state
 * @param filename The filename to create (user-specified)
 * @param user_prompt The AI prompt describing what content to generate
 */
int execute_agent(ClientState* state, const char* filename, const char* user_prompt) {
    PRINT_INFO("Contacting Gemini AI to generate content for '%s'...", filename);

    // Get API key from environment
    const char* api_key = get_api_key();
    if (!api_key) return ERR_NETWORK_ERROR;

    // 1. Construct JSON Request for Gemini API
    cJSON *root = cJSON_CreateObject();
    cJSON *contents = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "contents", contents);
    cJSON *content_obj = cJSON_CreateObject();
    cJSON_AddItemToArray(contents, content_obj);
    cJSON *parts = cJSON_CreateArray();
    cJSON_AddItemToObject(content_obj, "parts", parts);
    cJSON *text_part = cJSON_CreateObject();
    cJSON_AddItemToArray(parts, text_part);

    // Engineered prompt to get clean content output
    char system_prompt[BUFFER_SIZE * 2];
    snprintf(system_prompt, sizeof(system_prompt), 
        "User Request: '%s'. "
        "Generate the content for this request. "
        "Return ONLY the raw content that should go in the file, with NO markdown formatting, "
        "NO code fences (```), NO explanations. Just the pure content.", 
        user_prompt);
    
    cJSON_AddStringToObject(text_part, "text", system_prompt);
    
    // Add generation config for better output
    cJSON *genConfig = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "generationConfig", genConfig);
    cJSON_AddNumberToObject(genConfig, "temperature", 0.7);
    cJSON_AddNumberToObject(genConfig, "maxOutputTokens", 8192);
    
    char *json_str = cJSON_PrintUnformatted(root);

    // 2. Create temporary file for request body
    char temp_req_file[] = "/tmp/gemini_req_XXXXXX";
    int fd = mkstemp(temp_req_file);
    if (fd < 0) {
        PRINT_ERR("Failed to create temporary file");
        cJSON_Delete(root);
        free(json_str);
        return ERR_FILE_OPERATION_FAILED;
    }
    write(fd, json_str, strlen(json_str));
    close(fd);

    // 3. Execute curl command using popen
    char cmd[4096];
    snprintf(cmd, sizeof(cmd), 
        "curl -s -X POST \"https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash:generateContent?key=%s\" "
        "-H \"Content-Type: application/json\" -d @%s 2>/dev/null", 
        api_key, temp_req_file);
    
    FILE* fp = popen(cmd, "r");
    if (!fp) {
        PRINT_ERR("Failed to connect to AI service (curl not available?)");
        unlink(temp_req_file);
        cJSON_Delete(root);
        free(json_str);
        return ERR_NETWORK_ERROR;
    }

    // 4. Read Response (Handle large responses)
    char *response_buf = malloc(1);
    response_buf[0] = '\0';
    size_t total_len = 0;
    char buffer[1024];

    while (fgets(buffer, sizeof(buffer), fp) != NULL) {
        size_t chunk_len = strlen(buffer);
        char* new_buf = realloc(response_buf, total_len + chunk_len + 1);
        if (!new_buf) {
            PRINT_ERR("Memory allocation failed");
            free(response_buf);
            pclose(fp);
            unlink(temp_req_file);
            cJSON_Delete(root);
            free(json_str);
            return ERR_NETWORK_ERROR;
        }
        response_buf = new_buf;
        strcpy(response_buf + total_len, buffer);
        total_len += chunk_len;
    }
    pclose(fp);
    unlink(temp_req_file);

    if (total_len == 0) {
        PRINT_ERR("No response from AI service");
        free(response_buf);
        cJSON_Delete(root);
        free(json_str);
        return ERR_NETWORK_ERROR;
    }

    // 5. Parse Response
    cJSON *resp_json = cJSON_Parse(response_buf);
    if (!resp_json) {
        PRINT_ERR("Failed to parse AI response");
        free(response_buf);
        cJSON_Delete(root);
        free(json_str);
        return ERR_FILE_OPERATION_FAILED;
    }

    // Navigate Gemini JSON structure: candidates[0].content.parts[0].text
    cJSON *cand = cJSON_GetObjectItem(resp_json, "candidates");
    if (!cand) {
        PRINT_ERR("Invalid AI response: no candidates");
        cJSON_Delete(resp_json);
        free(response_buf);
        cJSON_Delete(root);
        free(json_str);
        return ERR_FILE_OPERATION_FAILED;
    }

    cJSON *first_cand = cJSON_GetArrayItem(cand, 0);
    cJSON *content = cJSON_GetObjectItem(first_cand, "content");
    cJSON *parts_resp = cJSON_GetObjectItem(content, "parts");
    cJSON *first_part = cJSON_GetArrayItem(parts_resp, 0);
    cJSON *text_item = cJSON_GetObjectItem(first_part, "text");

    if (!text_item || !text_item->valuestring) {
        PRINT_ERR("Invalid AI response format");
        cJSON_Delete(resp_json);
        free(response_buf);
        cJSON_Delete(root);
        free(json_str);
        return ERR_FILE_OPERATION_FAILED;
    }

    char *generated_content = text_item->valuestring;
    
    // Strip markdown code fences if present (```language ... ```)
    char *content_start = generated_content;
    if (strncmp(generated_content, "```", 3) == 0) {
        // Skip first line (```language)
        content_start = strchr(generated_content + 3, '\n');
        if (content_start) {
            content_start++; // Skip the newline
            // Find closing ```
            char *content_end = strstr(content_start, "```");
            if (content_end) {
                *content_end = '\0'; // Truncate at closing fence
            }
        }
    }

    PRINT_OK("AI generated content for: %s", filename);

    // 6. Create and Write File
    int ret = execute_create(state, filename);
    if (ret == ERR_SUCCESS) {
        ret = auto_write_file(state, filename, content_start);
        if (ret != ERR_SUCCESS) {
            PRINT_ERR("Failed to write AI content: %s", get_error_message(ret));
        }
    } else {
        PRINT_ERR("Failed to create file: %s", get_error_message(ret));
    }

    // Cleanup
    cJSON_Delete(resp_json);
    cJSON_Delete(root);
    free(json_str);
    free(response_buf);
    
    return ret;
}
