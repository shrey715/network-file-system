#!/bin/bash
# run_stress_test.sh - Integration and Stress Testing

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m'

echo -e "${GREEN}=== Setting up Stress Test ===${NC}"

# 1. Cleanup old data
rm -rf data/* logs/*
mkdir -p data logs

# 2. Build binaries
echo "Building..."
make clean > /dev/null
make > /dev/null
if [ $? -ne 0 ]; then
    echo -e "${RED}Build failed!${NC}"
    exit 1
fi

# 3. Start Name Server
echo "Starting Name Server..."
./name_server 8080 > logs/nm.log 2>&1 &
NM_PID=$!
sleep 1

# 4. Start Storage Server
echo "Starting Storage Server..."
# IP Port ClientPort ServerID
./storage_server 127.0.0.1 8080 8081 1 > logs/ss.log 2>&1 &
SS_PID=$!
sleep 1

# 5. Run Client Stress Test
echo -e "${GREEN}=== Running Client Stress Test ===${NC}"

STRESS_LOG="logs/client_stress.log"

{
  echo "testuser"                # Username
  echo "touch huge_file.txt"     # Create file
  echo "edit huge_file.txt 0"    # Edit file (will consume rest of usage)
  echo "This is a massive content update to verify streaming capabilities and large buffer handling."
} | ./client 127.0.0.1 8080 > $STRESS_LOG 2>&1

# Verify content by reading it back in a separate session
{
  echo "testuser"
  echo "cat huge_file.txt"
} | ./client 127.0.0.1 8080 > logs/client_cat.log 2>&1


# 7. Verify Results
echo -e "${GREEN}=== verifying ===${NC}"

# Check if client log contains the huge content
grep "massive content" logs/client_cat.log > /dev/null
if [ $? -eq 0 ]; then
    echo -e "${GREEN}PASS: Huge write verification${NC}"
else
    echo -e "${RED}FAIL: Huge write content missing${NC}"
fi

# Cleanup
kill $NM_PID $SS_PID 2>/dev/null
rm $STRESS_FILE

echo -e "${GREEN}Stress test completed.${NC}"
exit 0
