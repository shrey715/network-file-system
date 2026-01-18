# System Architecture

## Overview

Docs++ implements a distributed file system with a strong focus on concurrent access and efficient text editing.

## Core Components

### 1. Name Server
The central metadata repository.
*   **Data Structure**: Uses a **Trie** (Prefix Tree) for storing file paths, enabling O(L) search time where L is path length.
*   **Caching**: Implements an **LRU Cache** to speed up frequent path lookups.
*   **Concurrency**: Uses a global mutex to protect the file registry while handling multiple client connections via threads.

### 2. Storage Server
The data persistence layer.
*   **Piece Table**: The core data structure for file content. It allows for efficient insertion and deletion by maintaining a read-only buffer (original file) and an append-only buffer (new adds), with a list of "pieces" pointing to these buffers.
*   **Lock Registry**: Fine-grained locking system that tracks active operations on files, allowing high concurrency. Multiple clients can edit different files simultaneously.

### 3. Client
The user interface.
*   **TUI Editor**: A custom-built text editor that renders the Piece Table content. It supports:
    *   **Visual Navigation**: Smart cursor movement through wrapped lines.
    *   **Piped Input**: Non-interactive editing for automation.

## Data Structures

### Piece Table (Text Editing Engine)
The Piece Table is the core of our high-performance text editor. It avoids the performance pitfalls of traditional gap buffers or rope structures for our specific use case.

#### Structure
*   **Buffers**:
    *   `Original Buffer` (RO): Memory-mapped view of the file on disk.
    *   `Add Buffer` (Append-Only): Stores all new characters typed by the user.
*   **Piece List**:
    *   A doubly linked list of `PieceTableNode` structs.
    *   Each node contains: `{ source: (ORIGINAL|ADD), start_index, length }`.
*   **Operations**:
    *   **Insert**: Splits the current piece node into two strings, inserts a new node pointing to the `Add Buffer` in between. Complexity: O(1) relative to file size.
    *   **Delete**: Adjusts `length` or `start_index` of span nodes, potentially splitting or removing nodes. O(1).
    *   **Undo/Redo**: We maintain a stack of "Snapshots" (lightweight copies of the piece list pointers) to allow infinite undo.

### Name Server Internals
The Name Server acts as the brain of the distributed system.

#### 1. Trie (Prefix Tree) for Namespace
*   **Purpose**: Stores the file hierarchy (folders/files).
*   **Advantage**: Lookup time depends only on path length `K`, not the total number of files `N`. $O(K)$.
*   **Locking**: The Trie is protected by the global `ns_state.lock`.

#### 2. LRU Cache (Search Optimization)
*   **Purpose**: Caches results of `search` operations.
*   **Implementation**: A combination of a **Hash Map** (for O(1) access) and a **Doubly Linked List** (to track usage order).
*   **Policy**: When full, the Least Recently Used item (tail of list) is evicted.

#### 3. Failure Detection (Heartbeats)
*   **Mechanism**: Every Storage Server sends a heartbeat packet (`OP_HEARTBEAT`) every `X` seconds.
*   **Monitoring**: A dedicated background thread in the Name Server wakes up periodically to check timestamps.
*   **Timeout**: If `(current_time - last_heartbeat) > TIMEOUT_THRESHOLD`, the SS is marked "Offline". Clients requesting files on offline servers receive `ERR_SS_UNAVAILABLE`.

## Concurrency Model

### Name Server
*   **Model**: Thread-per-Client.
*   **Synchronization**: Coarse-grained Global Mutex (`ns_state.lock`).
    *   *Trade-off*: Simplicity and safety over raw parallel throughput. Since metadata ops are fast (in-memory Trie lookup), contention is manageable.

### Storage Server
*   **Model**: Thread-per-Client.
*   **Synchronization**: Fine-grained Lock Registry.
    *   **File Locks**: We use a custom `LockRegistry` struct.
    *   **Granularity**: Locks are per-file (or per-sentence for granular edits).
    *   **Benefit**: A read operation on `fileA` does not block a write operation on `fileB`.

