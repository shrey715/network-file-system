# Quick Reference Guide - Docs++

## Starting the System

### 1. Build Everything
```bash
make
```

### 2. Start Name Server
```bash
./name_server 8000
```

### 3. Start Storage Servers
```bash
./storage_server 127.0.0.1 8000 9000 1
./storage_server 127.0.0.1 8000 9001 2
```

### 4. Start Clients
```bash
./client 127.0.0.1 8000
```

### Quick Start (All-in-One)
```bash
./tests/run_tests.sh
```

## Command Reference

### File Operations

| Command | Syntax | Description | Example |
|---------|--------|-------------|---------|
| CREATE | `CREATE <filename>` | Create new file | `CREATE test.txt` |
| READ | `READ <filename>` | Read file content | `READ test.txt` |
| DELETE | `DELETE <filename>` | Delete file (owner only) | `DELETE test.txt` |
| INFO | `INFO <filename>` | Get file metadata | `INFO test.txt` |

### Viewing Files

| Command | Syntax | Description |
|---------|--------|-------------|
| VIEW | `VIEW` | List your files |
| VIEW -a | `VIEW -a` | List all files |
| VIEW -l | `VIEW -l` | List with details |
| VIEW -al | `VIEW -al` | List all with details |

### Writing to Files

```bash
WRITE <filename> <sentence_index>
<word_index> <new_word>
<word_index> <new_word>
...
ETIRW
```

**Example:**
```bash
WRITE test.txt 0
1 Hello
2 World!
ETIRW
```

### Access Control

| Command | Syntax | Description |
|---------|--------|-------------|
| ADDACCESS -R | `ADDACCESS -R <file> <user>` | Add read access |
| ADDACCESS -W | `ADDACCESS -W <file> <user>` | Add write access |
| REMACCESS | `REMACCESS <file> <user>` | Remove access |

**Examples:**
```bash
ADDACCESS -R test.txt alice    # Alice can read
ADDACCESS -W test.txt bob      # Bob can read and write
REMACCESS test.txt alice       # Remove Alice's access
```

### Other Operations

| Command | Syntax | Description |
|---------|--------|-------------|
| UNDO | `UNDO <filename>` | Undo last change |
| STREAM | `STREAM <filename>` | Stream word-by-word |
| EXEC | `EXEC <filename>` | Execute as commands |
| LIST | `LIST` | List all users |

## Common Workflows

### Workflow 1: Create and Edit a File

```bash
# Create file
CREATE myfile.txt

# Write initial content
WRITE myfile.txt 0
1 This
2 is
3 test.
ETIRW

# Read to verify
READ myfile.txt

# Edit second sentence
WRITE myfile.txt 1
1 Another
2 sentence.
ETIRW

# Read again
READ myfile.txt
```

### Workflow 2: Share a File

```bash
# Create file
CREATE shared.txt

# Write content
WRITE shared.txt 0
1 Shared
2 content.
ETIRW

# Give read access to alice
ADDACCESS -R shared.txt alice

# Give write access to bob
ADDACCESS -W shared.txt bob

# Check permissions
INFO shared.txt
```

### Workflow 3: Execute Script

```bash
# Create script file
CREATE script.sh

# Write commands
WRITE script.sh 0
1 echo
2 "Hello
3 World"
ETIRW

# Execute
EXEC script.sh
```

## Error Codes

| Code | Message | Cause |
|------|---------|-------|
| 0 | Success | Operation completed |
| 101 | File not found | File doesn't exist |
| 102 | Permission denied | No access to file |
| 103 | File already exists | CREATE duplicate |
| 104 | Sentence locked | Another user editing |
| 105 | Invalid index | Bad sentence/word index |
| 106 | Not owner | Owner-only operation |
| 108 | SS unavailable | Storage server down |

## Tips and Tricks

### 1. Sentence Indexing
- Sentences are 0-indexed
- First sentence is index 0
- Sentence ends with `.`, `!`, or `?`

### 2. Word Indexing
- Words are 0-indexed within each sentence
- Spaces separate words
- Can insert at any position

### 3. Concurrent Editing
- Multiple users can edit different sentences
- Same sentence blocks other users
- ETIRW releases the lock

### 4. File Permissions
- Owner always has full access
- Write access includes read access
- Only owner can change permissions

### 5. Viewing Files
- Use `-l` for word/char counts
- Use `-a` to see all files
- Combine flags: `-al`

## Troubleshooting

### Problem: Can't connect to Name Server
**Solution:** 
- Check if Name Server is running
- Verify IP and port are correct
- Check firewall settings

### Problem: File not found
**Solution:**
- Use `VIEW` to list available files
- Check file name spelling
- Verify you have access

### Problem: Permission denied
**Solution:**
- Check file permissions with `INFO`
- Ask owner for access
- Verify you're the right user

### Problem: Sentence locked
**Solution:**
- Wait for other user to finish (ETIRW)
- Try different sentence
- Contact the user holding the lock

### Problem: Storage Server unavailable
**Solution:**
- Check if Storage Servers are running
- Restart Storage Server
- Check network connectivity

## Testing Checklist

- [ ] Start Name Server
- [ ] Start 2+ Storage Servers
- [ ] Connect multiple clients
- [ ] Create files on different servers
- [ ] Read from multiple clients
- [ ] Write concurrently (different sentences)
- [ ] Test permission system
- [ ] Test undo functionality
- [ ] Test error conditions
- [ ] Check logs for errors

## Performance Tips

### For Large Files
1. Use STREAM instead of READ
2. Write in smaller chunks
3. Use multiple Storage Servers

### For Many Files
1. Use `-l` flag sparingly (slower)
2. Name files meaningfully
3. Use access control to limit visibility

### For Concurrent Users
1. Coordinate sentence editing
2. Use multiple files when possible
3. Release locks quickly (ETIRW)

## Directory Structure

```
course_project/
├── name_server          # Name Server binary
├── storage_server       # Storage Server binary
├── client              # Client binary
├── data/               # Persistent data
│   ├── nm_state.dat   # Name Server state
│   ├── ss_1/          # Storage Server 1 files
│   └── ss_2/          # Storage Server 2 files
└── logs/              # Log files
    ├── NM.log         # Name Server logs
    ├── SS.log         # Storage Server logs
    └── *.log          # Other logs
```

## Example Session

```bash
$ ./client 127.0.0.1 8000
Enter username: alice
Connected to Name Server as 'alice'

Enter commands (type 'help' for list of commands, 'quit' to exit):
> CREATE demo.txt
File 'demo.txt' created successfully!

> WRITE demo.txt 0
Sentence locked. Enter word modifications (word_index content), then ETIRW:
  1 Hello
  2 World!
  3 This
  4 is
  5 amazing.
  ETIRW
Write successful!

> READ demo.txt
Hello World! This is amazing.

> INFO demo.txt
File: demo.txt
Owner: alice
Created: 2025-11-04 16:54:23
Last Modified: 2025-11-04 16:54:45
Size: 28 bytes
Words: 5
Chars: 23
Access:
  alice (RW)

> ADDACCESS -R demo.txt bob
Access granted successfully!

> quit
Disconnected from Name Server
```

## Need More Help?

- Check `README.md` for detailed documentation
- See `IMPLEMENTATION.md` for technical details
- Review `logs/` for operation history
- Use `help` command in client
