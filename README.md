# Docs++

**Docs++** is a distributed network file system built for the **Operating Systems and Networks** course at **IIIT Hyderabad** (Monsoon 2025).

It features a central Name Server (NM), multiple Storage Servers (SS), and Clients with a command-line interface.

## Architecture

The system consists of three main components:

1.  **Name Server (NM)**: The central coordinator.
    *   Maintains the directory tree (Trie structure) and file metadata.
    *   Tracks available Storage Servers.
    *   Handles client requests for file locations and permissions.
    *   Uses an LRU cache for efficient path lookups.

2.  **Storage Server (SS)**: Stores actual file data.
    *   Registers with the Name Server upon startup.
    *   Manages local file operations (read, write, create, delete).
    *   Implements a **Piece Table** data structure for efficient text editing.
    *   Supports backing up data to disk.

3.  **Client**: The user interface.
    *   Connects to the Name Server to locate files.
    *   Connects directly to Storage Servers for data transfer.
    *   Provides a shell-like CLI for file management.
    *   Includes a built-in **TUI Text Editor** (Text User Interface) with support for large files and visual navigation.

## Features

*   **Distributed Storage**: Files are stored across multiple storage servers.
*   **Efficient Editing**: Uses Piece Table structure for O(1) insert/delete operations on large files.
*   **Visual Editor**: Interactive terminal-based editor with syntax highlighting buffer, scrolling, and undo capabilities.
*   **Replication**: Basic support for data redundancy (if configured).
*   **Access Control**: Simple permission system (ACLs) for users.
*   **Search**: Optimized file search using Trie and LRU cache.
*   **Automation**: Supports piped input for scripted file editing.

## Build Instructions

**Prerequisites**: GCC compiler, Make.

```bash
# Build all components (Client, Name Server, Storage Server)
make all

# Clean build artifacts
make clean
```

## Usage

### 1. Start Name Server
Start the Name Server on a specific port (e.g., 8080).
```bash
./name_server 8080
```

### 2. Start Storage Server(s)
Start one or more storage servers. They need to know the Name Server's IP/Port.
```bash
# Usage: ./storage_server <NM_IP> <NM_PORT> <CLIENT_PORT> <SS_ID>
./storage_server 127.0.0.1 8080 8081 1
```

### 3. Start Client
Connect the client to the Name Server.
```bash
# Usage: ./client <NM_IP> <NM_PORT>
./client 127.0.0.1 8080
```

## Client Commands

Once connected, the client provides a shell environment.

### File Operations
*   `ls [-a] [-l]` : List files. Use `-a` to show hidden files (starting with `.`). Use `-l` for detailed table view.
*   `cat <file>` : Display file contents.
*   `touch <file>` : Create a new empty file.
*   `rm <file>` : Delete a file.
*   `mv <src> <dest>` : Rename or move a file.
*   `mkdir <dir>` : Create a directory.
*   `info <file>` : Show file metadata (size, owner, storage server).

### Editor
*   `open <file>` : Open file in read-only mode.
*   `edit <file> <idx>` : Open file for editing starting at sentence index `<idx>` (use 0 for start).
    *   **Interactive**: Opens TUI editor.
    *   **Headless**: Pipe content to stdin (e.g., `echo "data" | client ... edit file 0`).
*   `undo <file>` : Revert last change (if supported by SS).

### Version Control
*   `commit <file> <tag>` : Save a checkpoint of the file.
*   `log <file>` : List all checkpoints.
*   `checkout <file> <tag>` : Revert file to a previous checkpoint.
*   `diff <file> <tag>` : View content of a specific checkpoint.

### Access Control
*   `chmod <file> <user>` : Grant permissions to another user.
*   `acl <file>` : List users with access to the file.

### Other
*   `agent <file> <prompt>` : (Experimental) AI agent helper.
*   `quit` / `exit` : Close the client.

## Testing

The project includes unit tests and stress tests.

```bash
# Run unit tests (Piece Table, Document, Editor)
make test

# Run integration/stress test
./tests/run_stress_test.sh
```
