# Developer Guide - Quick Reference

## Common Patterns and Helper Functions

This guide shows you how to use the refactored helper functions when adding new features or fixing bugs.

### Network Operations

#### Initializing Message Headers

**Use `init_message_header()` instead of manual initialization:**

```c
// ✓ Good
MessageHeader header;
init_message_header(&header, MSG_REQUEST, OP_MYOP, username);
strcpy(header.filename, filename);  // Add operation-specific fields

// ✗ Avoid
MessageHeader header;
memset(&header, 0, sizeof(header));
header.msg_type = MSG_REQUEST;
header.op_code = OP_MYOP;
strcpy(header.username, username);
```

#### Parsing Storage Server Info

**Use `parse_ss_info()` for IP:port strings:**

```c
// ✓ Good
char ip[MAX_IP];
int port;
if (parse_ss_info(ss_info, ip, &port) != 0) {
    // Handle error
}
int socket = connect_to_server(ip, port);

// ✗ Avoid
char ip[MAX_IP];
int port;
sscanf(ss_info, "%[^:]:%d", ip, &port);  // Missing error check!
```

#### Closing Sockets

**Use `safe_close_socket()` to prevent double-close bugs:**

```c
// ✓ Good
int socket = connect_to_server(ip, port);
// ... use socket ...
safe_close_socket(&socket);  // socket is now -1
// Safe to call again without error

// ✗ Avoid
close(socket);  // socket value unchanged, risk of double-close
```

### Client Command Development

#### Talking to Name Server

**Use `send_nm_request_and_get_response()` for NM operations:**

```c
// ✓ Good
MessageHeader header;
init_message_header(&header, MSG_REQUEST, OP_MYOP, state->username);
strcpy(header.filename, filename);

char* response = NULL;
send_nm_request_and_get_response(state, &header, payload, &response);

if (header.msg_type == MSG_RESPONSE) {
    // Process response
}
if (response) free(response);

// ✗ Avoid: Manual send/recv with boilerplate error checking
```

#### Connecting to Storage Server

**Use `get_storage_server_connection()` for file operations:**

```c
// ✓ Good
int ss_socket;
int result = get_storage_server_connection(state, filename, OP_READ, &ss_socket);
if (result != ERR_SUCCESS) {
    return result;
}

// Now send request to storage server
MessageHeader header;
init_message_header(&header, MSG_REQUEST, OP_SS_READ, state->username);
strcpy(header.filename, filename);
send_message(ss_socket, &header, NULL);

// ... receive response ...
safe_close_socket(&ss_socket);

// ✗ Avoid: Manually querying NM, parsing response, connecting
```

### Storage Server Handler Development

#### Adding a New Operation Handler

**Follow the modular pattern:**

```c
/**
 * handle_ss_myoperation
 * @brief Handler for OP_SS_MYOPERATION.
 *
 * Detailed description of what this operation does.
 */
void handle_ss_myoperation(int client_fd, MessageHeader* header, const char* payload) {
    // 1. Extract parameters from header and payload
    const char* filename = header->filename;
    
    // 2. Perform the operation
    int result = ss_do_myoperation(filename, payload);
    
    // 3. Send response
    if (result == ERR_SUCCESS) {
        // For operations returning data
        MessageHeader resp;
        memset(&resp, 0, sizeof(resp));
        resp.msg_type = MSG_RESPONSE;
        resp.data_length = strlen(data);
        send_message(client_fd, &resp, data);
    } else {
        // For simple success/failure
        send_simple_response(client_fd, MSG_ERROR, result);
    }
}
```

**Then add to the dispatcher in `handle_client_request()`:**

```c
switch (header.op_code) {
    // ... existing cases ...
    
    case OP_SS_MYOPERATION:
        handle_ss_myoperation(client_fd, &header, payload);
        keep_alive = 0;  // or 1 if part of a session
        break;
}
```

#### Sending Simple Responses

**Use `send_simple_response()` for ACK/ERROR with no payload:**

```c
// ✓ Good
void handle_ss_delete(int client_fd, MessageHeader* header) {
    int result = ss_delete_file(header->filename);
    send_simple_response(client_fd, 
                        (result == ERR_SUCCESS) ? MSG_ACK : MSG_ERROR, 
                        result);
}

// ✗ Avoid: Repetitive header setup
MessageHeader resp;
memset(&resp, 0, sizeof(resp));
resp.msg_type = (result == ERR_SUCCESS) ? MSG_ACK : MSG_ERROR;
resp.error_code = result;
resp.data_length = 0;
send_message(client_fd, &resp, NULL);
```

## Error Handling Best Practices

### 1. Always Check Return Values

```c
// ✓ Good
if (send_message(socket, &header, payload) < 0) {
    log_message("CLIENT", "ERROR", "Failed to send message");
    return ERR_NETWORK_ERROR;
}

// ✗ Avoid
send_message(socket, &header, payload);  // Ignoring errors!
```

### 2. Free Allocated Memory

```c
// ✓ Good
char* response = NULL;
send_nm_request_and_get_response(state, &header, NULL, &response);
// ... use response ...
if (response) free(response);  // Always cleanup

// ✗ Avoid
char* response;
send_nm_request_and_get_response(state, &header, NULL, &response);
// Forgot to free! Memory leak.
```

### 3. Use Error Messages

```c
// ✓ Good
if (header.msg_type == MSG_ERROR) {
    printf("Error: %s\n", get_error_message(header.error_code));
    return header.error_code;
}

// ✗ Avoid
if (header.msg_type == MSG_ERROR) {
    printf("Error code: %d\n", header.error_code);  // Unhelpful!
}
```

## Code Organization Checklist

When adding new functionality:

- [ ] Use `init_message_header()` for all MessageHeader initialization
- [ ] Use helper functions (`parse_ss_info()`, `safe_close_socket()`, etc.)
- [ ] Free all allocated memory before returning
- [ ] Check all return values and handle errors
- [ ] Add comprehensive docstring comments
- [ ] Follow existing naming conventions
- [ ] Keep functions focused (single responsibility)
- [ ] Extract repeated code into helpers
- [ ] Test both success and error paths

## Testing Your Changes

```bash
# Build all components
make clean && make -j2

# Check for warnings
make 2>&1 | grep -i warning

# Run basic smoke test
# Terminal 1: Start name server
./name_server 8000

# Terminal 2: Start storage server  
./storage_server 0 127.0.0.1 8000 8001 ./data

# Terminal 3: Run client
./client 127.0.0.1 8000
```

## Common Gotchas

1. **Forgetting to set `data_length`**: Always set `header.data_length` when sending payload
2. **Double-free**: Use `safe_close_socket()` and always set freed pointers to NULL
3. **Uninitialized headers**: Always use `init_message_header()` or `memset()` first
4. **Buffer overflows**: Use `strncpy()` with proper null termination
5. **Missing error checks**: Every network operation can fail - check return values!

## Getting Help

- See `documentation/REFACTORING.md` for detailed refactoring documentation
- See `documentation/IMPLEMENTATION.md` for system architecture
- See `documentation/QUICKREF.md` for protocol details
- Check function docstrings in source files for usage examples
