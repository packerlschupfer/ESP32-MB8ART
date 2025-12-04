#!/bin/bash

# MB8ART Unit Test Runner Script

echo "========================================"
echo "Running MB8ART Unit Tests"
echo "========================================"

# Colors for output
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Change to test directory
cd "$(dirname "$0")"

# Function to run a specific test
run_test() {
    local test_name=$1
    echo -e "\n${YELLOW}Running: $test_name${NC}"
    
    if platformio test -e native -f "$test_name" --verbose; then
        echo -e "${GREEN}✓ $test_name passed${NC}"
        return 0
    else
        echo -e "${RED}✗ $test_name failed${NC}"
        return 1
    fi
}

# Run all tests or specific test if provided
if [ $# -eq 0 ]; then
    echo "Running all tests..."
    
    # Run native tests
    echo -e "\n${YELLOW}=== Native Platform Tests ===${NC}"
    platformio test -e native --verbose
    
    # Optionally run hardware tests if connected
    if [ "$1" == "--hardware" ]; then
        echo -e "\n${YELLOW}=== ESP32 Hardware Tests ===${NC}"
        platformio test -e esp32 --verbose
    fi
else
    # Run specific test
    run_test "$1"
fi

echo -e "\n${GREEN}Test run complete!${NC}"