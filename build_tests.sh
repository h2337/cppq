#!/bin/bash

# Build script for Catch2 tests
# Requires: Catch2, nlohmann-json, hiredis, uuid libraries

echo "Building Catch2 tests for cppq..."

# Compile the test file
g++ -std=c++17 \
    tests.cpp \
    -o tests \
    -I. \
    -lCatch2Main -lCatch2 \
    -lhiredis \
    -luuid \
    -lpthread \
    -Wall -Wextra

if [ $? -eq 0 ]; then
    echo "Build successful!"
    echo ""
    echo "Make sure Redis is running on localhost:6379 before running tests."
    echo "To run tests: ./tests"
    echo "To run specific test sections: ./tests [queue] or ./tests [scheduling]"
    echo ""
    echo "Available test tags:"
    echo "  [queue]      - Basic enqueue/dequeue operations"
    echo "  [scheduling] - Task scheduling functionality"
    echo "  [recovery]   - Task recovery mechanism"
    echo "  [pause]      - Queue pause/unpause functionality"
    echo "  [threadpool] - Thread pool functionality"
    echo "  [errors]     - Error handling and edge cases"
else
    echo "Build failed!"
    exit 1
fi
