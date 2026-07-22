#!/bin/bash
# Build script for MB8ART example

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}MB8ART Example Build Script${NC}"
echo "=============================="

# Clean previous build
echo -e "\n${YELLOW}Cleaning previous build...${NC}"
rm -rf .pio

# Build the project
echo -e "\n${YELLOW}Building mb8art_debug_full environment...${NC}"
pio run -e mb8art_debug_full

# Check if build succeeded
if [ $? -eq 0 ]; then
    echo -e "\n${GREEN}✓ Build succeeded!${NC}"
    echo -e "${GREEN}Upload with: pio run -e mb8art_debug_full -t upload${NC}"
else
    echo -e "\n${RED}✗ Build failed!${NC}"
    echo -e "${RED}Check the errors above and fix them.${NC}"
    exit 1
fi