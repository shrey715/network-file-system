# Implementation Notes - Docs++ Distributed File System

## Overview

This document provides detailed implementation notes for the Docs++ distributed file system project.

## Architecture

### Three-Tier Architecture

1. **Client Layer** - User interface and command execution
2. **Name Server Layer** - Central coordination and metadata management
3. **Storage Server Layer** - Physical data storage and management

### Communication Flow

```
Client --> Name Server --> Storage Server
         (Metadata)        (Data Operations)
         
Client -------------------> Storage Server
         (Direct for READ/WRITE/STREAM)
```

## Component Details

### 1. Name Server (NM)

**File**: `src/name_server/`

**Responsibilities**:
- Maintain file registry with metadata
- Manage access control lists (ACL)
- Route client requests to appropriate storage servers
- Handle storage server registration
- Execute commands (EXEC operation)

**Key Data Structures**:
- `FileMetadata`: Stores file information and ACL
- `StorageServerInfo`: Tracks active storage servers
- `ClientInfo`: Maintains connected client information

**Threading Model**:
- Main thread accepts connections
- Each client connection handled in separate thread
- Mutex locks protect shared state

**File Registry Implementation**:
- Linear search for MVP (can be optimized with hash table/trie)
- Persistent state saved to `data/nm_state.dat`
- ACL stored per file with owner always having RW access

### 2. Storage Server (SS)

**File**: `src/storage_server/`

**Responsibilities**:
- Physical file storage in `data/ss_<id>/` directory
- Sentence-level locking for concurrent writes
- Undo functionality with `.undo` files
- Metadata management with `.meta` files
- Word-by-word streaming

**Key Features**:

#### Sentence Parsing
- Delimiters: `.`, `!`, `?`
- Every delimiter creates a new sentence
- Handles incomplete sentences (no trailing delimiter)

#### Write Operation Flow
1. Client requests write lock for sentence N
2. SS locks the sentence (stored in memory)
3. Client sends multiple word updates
4. Each update modifies the sentence
5. Client sends ETIRW to unlock
6. SS re-parses file to handle new delimiters created during editing

#### Undo Implementation
- Before first write, current content saved to `.undo` file
- Only one level of undo supported
- Undo restores from `.undo` file
- File-level undo (not user-specific)

#### Concurrency Control
- `pthread_mutex_t` per sentence
- Non-blocking lock attempts (pthread_mutex_trylock)
- Username tracked with each lock
- Same user can re-acquire their own lock

### 3. Client

**File**: `src/client/`

**Responsibilities**:
- Parse user commands
- Communicate with Name Server
- Direct connection to Storage Servers for data ops
- Display results to user

**Command Parser**:
- Simple string tokenization
- Flag handling for VIEW (-a, -l) and ADDACCESS (-R, -W)
- Two-argument support for most commands

**Operation Modes**:

1. **NM-handled**: VIEW, LIST, INFO, CREATE, DELETE, ADDACCESS, REMACCESS, EXEC
   - Single request-response with Name Server
   
2. **SS-direct**: READ, WRITE, STREAM, UNDO
   - Request SS info from NM
   - Direct connection to SS for data transfer
   - More efficient for large files

## Network Protocol

### Message Structure

```c
typedef struct {
    int msg_type;      // MSG_REQUEST, MSG_RESPONSE, MSG_ERROR, MSG_ACK, MSG_STOP
    int op_code;       // Operation code
    char username[64]; // Username
    char filename[256];// Filename
    int data_length;   // Payload length
    int error_code;    // Error code (if error)
    int sentence_index;// For WRITE operations
    int word_index;    // For WRITE operations
    int flags;         // For VIEW and ADDACCESS
} MessageHeader;
```

### Communication Pattern

1. Send header (fixed size)
2. Send payload (if data_length > 0)
3. Receive header
4. Receive payload (if data_length > 0)

### Error Codes

See `common.h` for complete list. Examples:
- 0: Success
- 101: File not found
- 102: Permission denied
- 104: Sentence locked
- 105: Invalid index

## File Operations

### CREATE
1. Client -> NM: CREATE request
2. NM selects SS (round-robin)
3. NM -> SS: Forward CREATE
4. SS creates empty file
5. SS -> NM: ACK
6. NM registers file in registry
7. NM -> Client: ACK

### READ
1. Client -> NM: READ request with permission check
2. NM verifies permission, returns SS info
3. Client -> SS: Direct READ request
4. SS reads file content
5. SS -> Client: File content
6. Client displays content

### WRITE
1. Client -> NM: WRITE request with permission check
2. NM verifies permission, returns SS info
3. Client -> SS: WRITE_LOCK for sentence N
4. SS locks sentence
5. Client sends multiple WRITE_WORD commands
6. SS updates words in sentence
7. Client sends WRITE_UNLOCK (ETIRW)
8. SS unlocks, re-parses file
9. SS -> Client: ACK

### DELETE
1. Client -> NM: DELETE request
2. NM checks ownership
3. NM -> SS: DELETE request
4. SS deletes file and metadata
5. SS -> NM: ACK
6. NM removes from registry
7. NM -> Client: ACK

## Concurrency Handling

### Multi-Client Support
- Each client connection runs in separate thread
- Name Server uses mutex for file registry access
- Storage Servers use sentence-level mutexes

### Sentence Locking
- Only one user can edit a sentence at a time
- Other sentences remain accessible
- Lock released on ETIRW or connection close

### Race Conditions Handled
1. Multiple clients creating same file: NM checks existence atomically
2. Concurrent writes: Sentence locking prevents conflicts
3. Read during write: Reads blocked for locked sentences only

## Access Control

### Permission Model
- Owner: Full RW access (cannot be removed)
- Other users: No access by default
- Read access: Can READ, STREAM, INFO
- Write access: Can READ, WRITE, UNDO (includes read)

### ACL Management
- Stored in Name Server memory
- Owner can ADDACCESS (-R or -W)
- Owner can REMACCESS
- Persisted to disk with file registry

## Logging

### Components
- All operations logged with timestamps
- Separate logs for NM, SS, Client
- Log files in `logs/` directory
- Format: `[timestamp] [level] message`

### Logged Information
- Client connections/disconnections
- File operations (create, read, write, delete)
- Access control changes
- Errors and exceptions
- Storage server registration

## Data Persistence

### Name Server
- File: `data/nm_state.dat`
- Format: Text-based, pipe-delimited
- Contains: File list, owners, ACLs, metadata
- Loaded on startup, saved after modifications

### Storage Server
- Files: `data/ss_<id>/<filename>`
- Metadata: `data/ss_<id>/<filename>.meta`
- Undo: `data/ss_<id>/<filename>.undo`
- All writes are atomic (via temp files)

## Testing

### Manual Testing
```bash
# Terminal 1: Start Name Server
./name_server 8000

# Terminal 2: Start Storage Server
./storage_server 127.0.0.1 8000 9000 1

# Terminal 3: Start Client
./client 127.0.0.1 8000
```

### Automated Testing
```bash
./tests/run_tests.sh
```

### Test Scenarios
1. Single client operations
2. Multiple clients concurrent read
3. Multiple clients concurrent write (different sentences)
4. Permission checks
5. Error handling
6. Server restart recovery

## Performance Considerations

### Optimizations Implemented
- Direct client-SS connection for data operations
- Sentence-level vs file-level locking
- Atomic file writes with temp files
- Round-robin SS selection

### Potential Improvements
- Hash table for O(1) file lookup
- Trie for prefix-based file search
- Connection pooling
- Caching frequently accessed files
- Batch operations

## Known Issues and Limitations

1. **Single-level undo**: Only last operation can be undone
2. **No SS failover**: If SS crashes, files unavailable
3. **No NM failover**: NM crash brings down system
4. **Linear file search**: O(N) lookup time
5. **Memory-based sentence locking**: Lost on SS restart
6. **No compression**: Large files consume significant space

## Extension Points (Bonus Features)

### 1. Folder Hierarchy
- Add parent_dir field to FileMetadata
- Implement path resolution in NM
- Update CREATE to support paths
- Add CREATEFOLDER, MOVE, VIEWFOLDER commands

### 2. Checkpointing
- Add checkpoint storage in SS
- Tag-based checkpoint management
- Implement CHECKPOINT, VIEWCHECKPOINT, REVERT, LISTCHECKPOINTS
- Store checkpoints as separate files

### 3. Fault Tolerance
- Implement file replication across multiple SS
- Add heartbeat mechanism for failure detection
- Automatic failover to replica SS
- Async write replication

### 4. Request Access
- Add access_requests table in NM
- Implement REQUEST, VIEWREQUESTS, APPROVE/DENY commands
- Notification system for owners

## Code Organization

### Header Files
- `common.h`: Shared definitions, constants, structures
- `client.h`: Client-specific functions
- `name_server.h`: Name server functions and structures
- `storage_server.h`: Storage server functions

### Source Organization
- `common/`: Utilities used by all components
- `name_server/`: NM implementation
- `storage_server/`: SS implementation
- `client/`: Client implementation

## Build System

### Makefile Targets
- `all`: Build all components (default)
- `name_server`: Build only NM
- `storage_server`: Build only SS
- `client`: Build only client
- `clean`: Remove binaries and temporary files
- `test`: Run test suite

### Dependencies
- pthread (POSIX threads)
- Standard C library
- POSIX sockets

## Debugging Tips

1. **Check logs**: All operations logged in `logs/`
2. **Verbose output**: Enable in logger.c
3. **Network issues**: Use netstat to check port availability
4. **Segfaults**: Run with gdb, check null pointers
5. **Deadlocks**: Check mutex lock/unlock pairs

## Security Considerations

### Current Implementation
- Basic username-based authentication
- No password protection
- No encryption
- Trust-based system

### Production Requirements
- Add authentication mechanism
- Encrypt network communication (TLS)
- Validate all user inputs
- Rate limiting for operations
- Audit logging

## Conclusion

This implementation provides a solid foundation for a distributed file system with:
- ✅ All 12 required operations
- ✅ Concurrent access with locking
- ✅ Access control
- ✅ Data persistence
- ✅ Error handling
- ✅ Comprehensive logging

The architecture is extensible for bonus features and production hardening.
