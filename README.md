[![Review Assignment Due Date](https://classroom.github.com/assets/deadline-readme-button-22041afd0340ce965d47ae6ef1cefeee28c7c493a6346c4f15d667ab976d596c.svg)](https://classroom.github.com/a/0ek2UV58)

# Docs++ - Distributed File System

A simplified, shared document system with support for concurrency and access control, similar in spirit to Google Docs.

## Team: Flying Fish

## Features

### Core Functionalities (150 points)
1. ✅ **VIEW** - List files with various flags (-a, -l)
2. ✅ **READ** - Read file contents
3. ✅ **CREATE** - Create new files
4. ✅ **WRITE** - Write to files at word level with sentence locking
5. ✅ **UNDO** - Revert last file changes
6. ✅ **INFO** - Get file metadata and details
7. ✅ **DELETE** - Delete files (owner only)
8. ✅ **STREAM** - Stream file content word-by-word
9. ✅ **LIST** - List all registered users
10. ✅ **ADDACCESS** - Add read/write access to users
11. ✅ **REMACCESS** - Remove user access
12. ✅ **EXEC** - Execute file contents as shell commands

### System Requirements (40 points)
- ✅ Data Persistence - Files stored persistently
- ✅ Access Control - Permission-based file access
- ✅ Logging - Complete operation logging
- ✅ Error Handling - Comprehensive error codes
- ✅ Efficient Search - Fast file lookups

## Architecture

The system consists of three main components:

1. **Name Server (NM)** - Central coordinator managing:
   - File registry and metadata
   - Storage server registration
   - Client connections
   - Access control lists (ACL)
   - Request routing

2. **Storage Servers (SS)** - File storage nodes providing:
   - Physical file storage
   - Sentence-level locking for concurrent writes
   - Undo functionality
   - Direct client connections for read/write/stream

3. **Clients** - User interface providing:
   - Command-line interface
   - All 12 file operations
   - Direct SS connections for data operations

## Building

```bash
make
```

This will create three executables:
- `name_server` - Name Server
- `storage_server` - Storage Server
- `client` - Client

## Running the System

### 1. Start the Name Server

```bash
./name_server <port>
```

Example:
```bash
./name_server 8000
```

### 2. Start Storage Servers

```bash
./storage_server <nm_ip> <nm_port> <client_port> <server_id>
```

Examples:
```bash
./storage_server 127.0.0.1 8000 9000 1
./storage_server 127.0.0.1 8000 9001 2
```

### 3. Start Clients

```bash
./client <nm_ip> <nm_port>
```

Example:
```bash
./client 127.0.0.1 8000
```

You will be prompted to enter a username.

### Quick Start Script

```bash
./tests/run_tests.sh
```

This script will:
- Build the project
- Start the Name Server on port 8000
- Start two Storage Servers on ports 9000 and 9001
- Keep all servers running until you press Ctrl+C

## Usage Examples

### Viewing Files

```
VIEW              # List your accessible files
VIEW -a           # List all files in the system
VIEW -l           # List with details (words, chars, owner, etc.)
VIEW -al          # List all files with details
```

### Creating and Managing Files

```
CREATE test.txt                    # Create a new file
READ test.txt                      # Read file content
DELETE test.txt                    # Delete file (owner only)
INFO test.txt                      # Get file metadata
```

### Writing to Files

```
WRITE test.txt 0                   # Start writing to sentence 0
1 Hello                            # Insert "Hello" at word index 1
3 world!                           # Insert "world!" at word index 3
ETIRW                              # Finish writing (unlocks sentence)
```

### Access Control

```
ADDACCESS -R test.txt user2        # Give user2 read access
ADDACCESS -W test.txt user3        # Give user3 write access
REMACCESS test.txt user2           # Remove user2's access
```

### Other Operations

```
UNDO test.txt                      # Undo last change
STREAM test.txt                    # Stream content word-by-word
EXEC script.sh                     # Execute file as shell script
LIST                               # List all users
```

## Project Structure

```
course_project/
├── include/              # Header files
│   ├── common.h         # Shared definitions
│   ├── client.h         # Client interfaces
│   ├── name_server.h    # Name server interfaces
│   └── storage_server.h # Storage server interfaces
├── src/
│   ├── common/          # Common utilities
│   │   ├── logger.c     # Logging functions
│   │   ├── network.c    # Network utilities
│   │   └── utils.c      # Utility functions
│   ├── name_server/     # Name server implementation
│   │   ├── main.c       # Main server loop
│   │   ├── file_registry.c # File management
│   │   └── handlers.c   # Request handlers
│   ├── storage_server/  # Storage server implementation
│   │   ├── main.c       # Main server loop
│   │   ├── file_ops.c   # File operations
│   │   └── sentence.c   # Sentence parsing & locking
│   └── client/          # Client implementation
│       ├── main.c       # Main client loop
│       ├── commands.c   # Command implementations
│       └── parser.c     # Command parser
├── data/                # Persistent data storage
├── logs/                # Log files
├── tests/               # Test scripts
├── Makefile            # Build configuration
└── README.md           # This file
```

## Implementation Details

### Concurrency Control

- **Sentence-level locking**: Multiple users can edit different sentences simultaneously
- **Lock ownership**: Each locked sentence tracks the user who locked it
- **Automatic unlock**: ETIRW command releases sentence locks

### Data Persistence

- Files stored in `data/ss_<id>/` directories
- Metadata stored in `.meta` files
- Undo history in `.undo` files
- Name server state in `data/nm_state.dat`

### Access Control

- Owner has full read/write access
- Owner can grant read or write access to other users
- Write access includes read access
- All operations check permissions before execution

### Error Handling

Comprehensive error codes for:
- File not found (101)
- Permission denied (102)
- File already exists (103)
- Sentence locked (104)
- Invalid indices (105, 114, 115)
- Ownership errors (106)
- Storage server issues (108, 109)
- And more...

### Logging

All operations are logged with:
- Timestamps
- Component (NM, SS, Client)
- User information
- Operation details
- Success/failure status

Logs are stored in `logs/` directory.

## Testing

The system has been tested with:
- Multiple concurrent clients
- Multiple storage servers
- Concurrent read/write operations
- Permission checks
- Error conditions
- Long-running operations

## Known Limitations

1. Name Server failure brings down the entire system (as specified)
2. Single-level undo (only last operation)
3. No automatic failover for storage servers
4. No folder hierarchy (bonus feature not implemented)

## Error Codes Reference

| Code | Description |
|------|-------------|
| 0    | Success |
| 101  | File not found |
| 102  | Permission denied |
| 103  | File already exists |
| 104  | Sentence locked by another user |
| 105  | Invalid sentence or word index |
| 106  | Not owner |
| 107  | User not found |
| 108  | Storage server unavailable |
| 109  | Storage server disconnected |
| 110  | Invalid command |
| 111  | Network error |
| 112  | File operation failed |
| 113  | No undo history available |

## Future Enhancements

Potential bonus features that could be added:
- Hierarchical folder structure
- Checkpointing system
- Request access mechanism
- Fault tolerance with replication
- Multiple undo levels
- Real-time notifications

## Contributors

Team Flying Fish

## License

This is a course project for OSN Monsoon 2025.

