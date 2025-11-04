#!/bin/bash

# Test script for the distributed file system

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}Starting Distributed File System Test${NC}"
echo "========================================"

# Clean up previous runs
echo -e "\n${YELLOW}Cleaning up previous data...${NC}"
make clean
rm -rf data/* logs/*

# Build the project
echo -e "\n${YELLOW}Building project...${NC}"
make
if [ $? -ne 0 ]; then
    echo -e "${RED}Build failed!${NC}"
    exit 1
fi
echo -e "${GREEN}Build successful!${NC}"

# Start Name Server in background
echo -e "\n${YELLOW}Starting Name Server on port 8000...${NC}"
./name_server 8000 > logs/nm_output.log 2>&1 &
NM_PID=$!
sleep 1

# Check if Name Server is running
if ! ps -p $NM_PID > /dev/null; then
    echo -e "${RED}Name Server failed to start!${NC}"
    exit 1
fi
echo -e "${GREEN}Name Server started (PID: $NM_PID)${NC}"

# Start Storage Server in background
echo -e "\n${YELLOW}Starting Storage Server 1 on port 9000...${NC}"
./storage_server 127.0.0.1 8000 9000 1 > logs/ss1_output.log 2>&1 &
SS1_PID=$!
sleep 1

# Check if Storage Server is running
if ! ps -p $SS1_PID > /dev/null; then
    echo -e "${RED}Storage Server 1 failed to start!${NC}"
    kill $NM_PID 2>/dev/null
    exit 1
fi
echo -e "${GREEN}Storage Server 1 started (PID: $SS1_PID)${NC}"

# Start a second Storage Server
echo -e "\n${YELLOW}Starting Storage Server 2 on port 9001...${NC}"
./storage_server 127.0.0.1 8000 9001 2 > logs/ss2_output.log 2>&1 &
SS2_PID=$!
sleep 1

if ! ps -p $SS2_PID > /dev/null; then
    echo -e "${RED}Storage Server 2 failed to start!${NC}"
    kill $NM_PID $SS1_PID 2>/dev/null
    exit 1
fi
echo -e "${GREEN}Storage Server 2 started (PID: $SS2_PID)${NC}"

echo -e "\n${GREEN}All servers started successfully!${NC}"
echo ""
echo "Name Server:      PID $NM_PID (port 8000)"
echo "Storage Server 1: PID $SS1_PID (port 9000)"
echo "Storage Server 2: PID $SS2_PID (port 9001)"
echo ""
echo "You can now run clients using:"
echo "  ./client 127.0.0.1 8000"
echo ""
echo "To stop all servers, run:"
echo "  kill $NM_PID $SS1_PID $SS2_PID"
echo ""
echo "Logs are available in logs/ directory"
echo ""
echo -e "${YELLOW}Press Ctrl+C to stop all servers${NC}"

# Wait for interrupt
trap "echo -e '\n${YELLOW}Stopping servers...${NC}'; kill $NM_PID $SS1_PID $SS2_PID 2>/dev/null; echo -e '${GREEN}Servers stopped${NC}'; exit 0" INT

# Keep script running
wait
