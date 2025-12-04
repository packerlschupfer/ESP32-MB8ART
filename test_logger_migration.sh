#!/bin/bash
# Test MB8ART Logger Migration

echo "MB8ART Logger Migration Test"
echo "==========================="

# Save current directory
ORIGINAL_DIR=$(pwd)

# Navigate to example project
cd example/ESPlan-blueprint-libs-freertos-taskmanager-MB8ART-workspace || exit 1

echo ""
echo "1. Testing WITHOUT custom logger (release build)..."
echo "---------------------------------------------------"
pio run -e mb8art_release -t clean > /dev/null 2>&1
if pio run -e mb8art_release; then
    echo "✅ SUCCESS: Compilation without custom logger passed"
else
    echo "❌ FAILED: Compilation without custom logger failed"
    exit 1
fi

echo ""
echo "2. Testing WITH custom logger (custom_logger build)..."
echo "------------------------------------------------------"
pio run -e mb8art_custom_logger -t clean > /dev/null 2>&1
if pio run -e mb8art_custom_logger; then
    echo "✅ SUCCESS: Compilation with custom logger passed"
else
    echo "❌ FAILED: Compilation with custom logger failed"
    exit 1
fi

# Return to original directory
cd "$ORIGINAL_DIR"

echo ""
echo "✅ All tests passed! MB8ART Logger migration successful."
echo ""
echo "Summary:"
echo "- MB8ART can now be used WITHOUT Logger.h dependency (default)"
echo "- MB8ART can still use custom Logger when MB8ART_USE_CUSTOM_LOGGER is defined"
echo ""
echo "To use with custom logger in your platformio.ini:"
echo "build_flags = -DMB8ART_USE_CUSTOM_LOGGER"