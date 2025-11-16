# Command Reference Guide

This document provides a comprehensive guide to the reorganized command structure of the distributed file system client.

## Command Structure Overview

The client now uses a hierarchical, two-level command structure that groups related operations together:

```
<category> <operation> [arguments] [flags]
```

This organization makes the system more intuitive and reduces cognitive load by grouping commands into logical categories.

## Command Categories

### 1. File System (`file`)

Basic file operations for managing files.

| Command | Description | Example |
|---------|-------------|---------|
| `file create <filename>` | Create a new file | `file create myfile.txt` |
| `file delete <filename>` | Delete a file | `file delete myfile.txt` |
| `file read <filename>` | Display file content | `file read myfile.txt` |
| `file info <filename>` | Show file metadata | `file info myfile.txt` |
| `file list [-a] [-l]` | List files (with optional flags) | `file list -al` |
| `file move <file> <folder>` | Move file to folder | `file move file.txt /docs` |
| `file stream <filename>` | Stream file content | `file stream log.txt` |
| `file exec <filename>` | Execute file as commands | `file exec script.txt` |

**Flags for `file list`:**
- `-a`: Show all files (including those from other users)
- `-l`: Long listing format (detailed metadata)
- `-al`: Combine both flags

### 2. Edit System (`edit`)

Operations for modifying file content.

| Command | Description | Example |
|---------|-------------|---------|
| `edit <filename> <idx>` | Edit sentence at given index | `edit file.txt 0` |
| `edit undo <filename>` | Undo last change to file | `edit undo file.txt` |

**Edit workflow:**
1. Lock a sentence: `edit myfile.txt 2`
2. Modify words interactively: `0 new_content`
3. Finish editing: `ETIRW`

### 3. Folder System (`folder`)

Operations for managing folders.

| Command | Description | Example |
|---------|-------------|---------|
| `folder create <name>` | Create a new folder | `folder create documents` |
| `folder view [path]` | List folder contents | `folder view /docs` |

**Notes:**
- Omit path in `folder view` to list root folder contents
- Folders organize files hierarchically

### 4. Version Control (`version`)

Checkpoint-based version control for files.

| Command | Description | Example |
|---------|-------------|---------|
| `version create <file> <tag>` | Create a checkpoint | `version create file.txt v1.0` |
| `version view <file> <tag>` | View checkpoint content | `version view file.txt v1.0` |
| `version revert <file> <tag>` | Revert to checkpoint | `version revert file.txt v1.0` |
| `version list <file>` | List all checkpoints | `version list file.txt` |

**Workflow example:**
```bash
# Create initial version
version create doc.txt draft1

# Make changes...

# Create another checkpoint
version create doc.txt draft2

# View old version
version view doc.txt draft1

# Revert if needed
version revert doc.txt draft1
```

### 5. Access Control (`access`)

Manage file permissions and access requests.

| Command | Description | Example |
|---------|-------------|---------|
| `access grant <file> <user> [-R\|-W]` | Grant access to user | `access grant file.txt alice -R` |
| `access revoke <file> <user>` | Revoke user's access | `access revoke file.txt bob` |
| `access request <file> [-R] [-W]` | Request access to file | `access request doc.txt -R` |
| `access requests <file>` | View pending requests (owner) | `access requests myfile.txt` |
| `access approve <file> <user>` | Approve request (owner) | `access approve file.txt alice` |
| `access deny <file> <user>` | Deny request (owner) | `access deny file.txt bob` |

**Flags:**
- `-R`: Read-only access
- `-W`: Write access (includes read)
- No flag defaults to read-only

**Access workflow (as owner):**
```bash
# Another user requests access
# You view pending requests
access requests myfile.txt

# Approve or deny
access approve myfile.txt alice
access deny myfile.txt bob
```

**Access workflow (as requestor):**
```bash
# Request read access
access request shared.txt -R

# Request write access
access request shared.txt -W
```

### 6. User System (`user`)

Operations related to users.

| Command | Description | Example |
|---------|-------------|---------|
| `user list` | List all registered users | `user list` |

## Migration from Old Commands

For users familiar with the old command structure:

| Old Command | New Command |
|-------------|-------------|
| `VIEW -al` | `file list -al` |
| `READ file.txt` | `file read file.txt` |
| `CREATE file.txt` | `file create file.txt` |
| `WRITE file.txt 0` | `edit file.txt 0` |
| `UNDO file.txt` | `edit undo file.txt` |
| `DELETE file.txt` | `file delete file.txt` |
| `INFO file.txt` | `file info file.txt` |
| `STREAM file.txt` | `file stream file.txt` |
| `EXEC script.txt` | `file exec script.txt` |
| `LIST` | `user list` |
| `ADDACCESS -R f.txt bob` | `access grant f.txt bob -R` |
| `REMACCESS f.txt bob` | `access revoke f.txt bob` |
| `REQUESTACCESS -R f.txt` | `access request f.txt -R` |
| `VIEWREQUESTS f.txt` | `access requests f.txt` |
| `APPROVEREQUEST f.txt bob` | `access approve f.txt bob` |
| `DENYREQUEST f.txt bob` | `access deny f.txt bob` |
| `CREATEFOLDER docs` | `folder create docs` |
| `VIEWFOLDER /docs` | `folder view /docs` |
| `MOVE file.txt /docs` | `file move file.txt /docs` |
| `CHECKPOINT f.txt v1` | `version create f.txt v1` |
| `VIEWCHECKPOINT f.txt v1` | `version view f.txt v1` |
| `REVERT f.txt v1` | `version revert f.txt v1` |
| `LISTCHECKPOINTS f.txt` | `version list f.txt` |

## Key Benefits

1. **Logical Grouping**: Related commands are grouped under intuitive categories
2. **Easier Discovery**: Users can explore commands by category
3. **Reduced Memorization**: Remember 6 categories instead of 24+ individual commands
4. **Clearer Intent**: Command structure makes purpose obvious (e.g., `file read` vs `READ`)
5. **Extensibility**: Easy to add new operations to existing categories
6. **Consistent Patterns**: Similar operations follow similar patterns across categories

## Getting Help

- Type `help` in the client to see the complete command list
- Type `quit` or `exit` to disconnect from the server

## Example Session

```bash
# Connect to the server
$ ./client 127.0.0.1 8080
Enter username: alice
Connected to Name Server as 'alice'

# Create and edit a file
> file create myfile.txt
File 'myfile.txt' created successfully!

> edit myfile.txt 0
Sentence locked. Enter word modifications (word_index content), then ETIRW:
0 Hello world
1 from Alice
ETIRW
File written and unlocked successfully!

# Create a checkpoint
> version create myfile.txt v1.0
Checkpoint 'v1.0' created successfully!

# Share with another user
> access grant myfile.txt bob -R
Access granted to bob

# List files
> file list -l
Filename             Words Chars      Last Access Owner
------------------------------------------------------------
myfile.txt           4     22         2025-11-16   alice

> quit
Disconnected from Name Server
```
