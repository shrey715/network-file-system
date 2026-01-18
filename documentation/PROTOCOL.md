# Communication Protocol

Docs++ uses a custom binary protocol over TCP sockets. All messages begin with a fixed-size header.

## Message Header structure

```c
typedef struct {
  int msg_type;       // MSG_REQUEST (1), MSG_ACK (2), MSG_ERROR (3)
  int op_code;        // Operation ID (e.g., OP_READ, OP_WRITE)
  char username[32];  // Authenticated user
  char filename[64];  // Target file
  char foldername[64];
  char checkpoint_tag[32];
  int data_length;    // Size of payload following header
  int error_code;     // 0 on success, or specific error code
  int sentence_index; // For granular editing
  int word_index;     // For granular editing
  int flags;          // Bitmask (e.g., bit 0 for '-a')
} MessageHeader;
```

## Operations (Opcodes)

### Client <-> Name Server
*   `OP_VIEW` (1): List files.
*   `OP_READ` (2): Request file read (returns SS IP/Port).
*   `OP_WRITE` (3): Request file write (returns SS IP/Port).
*   `OP_CREATE` (4): Create new file.
*   `OP_DELETE` (5): Delete file.

### Client <-> Storage Server
*   `OP_SS_READ` (42): Read file content.
*   `OP_SS_WRITE_LOCK` (43): Lock file for editing.
*   `OP_SS_WRITE_WORD` (44): Insert/Update word (deprecated in favor of Piece Table logic).

### System
*   `OP_REGISTER_SS` (30): Storage Server -> Name Server registration.
*   `OP_HEARTBEAT` (33): SS keep-alive signal.

## Communication Flows

### 1. Client Reading a File
1.  **Client -> Name Server**: `OP_READ` ("path/to/file")
2.  **Name Server**: Consents (Checks Trie, Permissions). Returns Storage Server IP/Port.
3.  **Client -> Storage Server**: `OP_SS_READ` ("path/to/file")
4.  **Storage Server**: Streams file content back to Client.

### 2. Client Listing Files (`ls -a`)
1.  **Client -> Name Server**: `OP_VIEW` (Flags: `0x01` for `-a`)
2.  **Name Server**:
    *   Locks Trie.
    *   Iterates files.
    *   Filters: `if (is_hidden && !flag_a) skip;`
    *   Constructs list of accessible files.
3.  **Name Server -> Client**: Returns JSON-formatted list of files.

### 3. Server Registration (Startup)
1.  **Storage Server**: Starts up, reads config.
2.  **SS -> Name Server**: `OP_REGISTER_SS` (Payload: IP, Port, SS_ID)
3.  **Name Server**: Adds SS to `ss_registry` array.
4.  **Name Server -> SS**: `MSG_ACK`.
5.  **SS**: Starts background thread to send `OP_HEARTBEAT` every 5s.

## Error Codes

| Code | Description |
|------|-------------|
| 0    | Success |

| 101  | File Not Found |
| 102  | Permission Denied |
| 104  | File Locked |
| 108  | Storage Server Unavailable |
| 126  | Username Taken |
