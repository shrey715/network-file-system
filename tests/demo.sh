#!/bin/bash

# Demo script to showcase the file system functionality

echo "================================================"
echo "  Docs++ Distributed File System Demo"
echo "================================================"
echo ""
echo "This demo will showcase various operations:"
echo "1. Creating files"
echo "2. Writing content"
echo "3. Reading files"
echo "4. Access control"
echo "5. Undo functionality"
echo ""
echo "Make sure you have started the servers:"
echo "  - Name Server on port 8000"
echo "  - Storage Server(s) on ports 9000+"
echo ""
read -p "Press Enter to continue..."

# Create a temporary file with demo commands
cat > /tmp/demo_commands.txt << 'EOF'
CREATE demo.txt
READ demo.txt
WRITE demo.txt 0
1 This
2 is
3 a
4 test.
ETIRW
READ demo.txt
WRITE demo.txt 0
5 Amazing!
ETIRW
READ demo.txt
INFO demo.txt
VIEW
VIEW -l
LIST
UNDO demo.txt
READ demo.txt
DELETE demo.txt
VIEW
EOF

echo ""
echo "Demo commands have been prepared."
echo "You can run the client manually and try these commands:"
echo ""
cat /tmp/demo_commands.txt
echo ""
echo "Or copy-paste them into the client interface."
echo ""
echo "To run the client:"
echo "  ./client 127.0.0.1 8000"
echo ""
echo "Demo commands saved to: /tmp/demo_commands.txt"
