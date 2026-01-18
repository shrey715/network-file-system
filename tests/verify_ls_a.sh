#!/bin/bash
# verify_ls_a.sh

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m'

echo -e "${GREEN}=== Testing ls -a ===${NC}"

# 1. Cleanup old logs
rm -rf logs/*
mkdir -p logs

# 2. Setup Files (requires running server)
if ! pgrep -f "name_server" > /dev/null; then
    echo "Starting Name Server..."
    ./name_server 8080 > logs/nm.log 2>&1 &
    NM_PID=$!
    sleep 1
    
    echo "Starting Storage Server..."
    ./storage_server 127.0.0.1 8080 8081 1 > logs/ss.log 2>&1 &
    SS_PID=$!
    sleep 1
else
    echo "Servers already running."
fi

# 3. Create normal and hidden files
{
  echo "testuser"
  echo "touch normal.txt"
  echo "touch .hidden.txt"
} | ./client 127.0.0.1 8080 > logs/client_setup.log 2>&1

# 4. Test ls (should show normal.txt, hide .hidden.txt)
{
  echo "testuser"
  echo "ls"
} | ./client 127.0.0.1 8080 > logs/client_ls.log 2>&1

# 5. Test ls -a (should show both)
{
  echo "testuser"
  echo "ls -a"
} | ./client 127.0.0.1 8080 > logs/client_ls_a.log 2>&1

# 6. Verify
echo "Checking ls output..."
if grep -q "normal.txt" logs/client_ls.log && ! grep -q "\.hidden\.txt" logs/client_ls.log; then
    echo -e "${GREEN}PASS: ls hides hidden files${NC}"
else
    echo -e "${RED}FAIL: ls output incorrect${NC}"
    cat logs/client_ls.log
fi

echo "Checking ls -a output..."
if grep -q "normal.txt" logs/client_ls_a.log && grep -q "\.hidden\.txt" logs/client_ls_a.log; then
    echo -e "${GREEN}PASS: ls -a shows hidden files${NC}"
else
    echo -e "${RED}FAIL: ls -a output incorrect${NC}"
    cat logs/client_ls_a.log
fi

# Cleanup
if [ ! -z "$NM_PID" ]; then
    kill $NM_PID $SS_PID 2>/dev/null
fi
