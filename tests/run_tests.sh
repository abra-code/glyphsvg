#!/bin/bash
set -e

cd "$(dirname "$0")/.."

BUILD_DIR="./build"
BIN_DIR="$BUILD_DIR/bin"
TEST_DIR="$BUILD_DIR/test_output"

/bin/rm -rf "$TEST_DIR"
/bin/mkdir -p "$TEST_DIR"

echo "=== Test 1: Custom font - single character to file ==="
./build/bin/glyphsvg --font=Helvetica A 100 --output="$TEST_DIR/A.svg"
test -f "$TEST_DIR/A.svg" && echo "PASS" || echo "FAIL"

echo ""
echo "=== Test 2: Custom font - single character to stdout ==="
./build/bin/glyphsvg --font=Helvetica A 100 > "$TEST_DIR/stdout.svg"
test -f "$TEST_DIR/stdout.svg" && echo "PASS" || echo "FAIL"

echo ""
echo "=== Test 3: Custom font - codepoint U+0041 to file ==="
./build/bin/glyphsvg --font=Helvetica U+0041 100 --output="$TEST_DIR/codepoint.svg"
test -f "$TEST_DIR/codepoint.svg" && echo "PASS" || echo "FAIL"

echo ""
echo "=== Test 4: Custom font - multiple characters to directory ==="
mkdir -p "$TEST_DIR/multi"
./build/bin/glyphsvg --font=Helvetica "Hi" 100 --output="$TEST_DIR/multi/"
test -f "$TEST_DIR/multi/U+48_0.svg" && test -f "$TEST_DIR/multi/U+69_1.svg" && echo "PASS" || echo "FAIL"

echo ""
echo "=== Test 5: SF Symbols - heart to file ==="
./build/bin/glyphsvg heart bold 100 --output="$TEST_DIR/heart.svg"
test -f "$TEST_DIR/heart.svg" && echo "PASS" || echo "FAIL"

echo ""
echo "=== Test 6: SF Symbols - to stdout ==="
./build/bin/glyphsvg star bold 100 > "$TEST_DIR/star.svg"
test -f "$TEST_DIR/star.svg" && echo "PASS" || echo "FAIL"

echo ""
echo "=== All tests completed ==="
