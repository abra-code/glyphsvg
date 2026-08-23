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

if [ -f "./material/MaterialSymbolsOutlined.codepoints" ]; then
    echo ""
    echo "=== Test 7: Material - default style to file ==="
    ./build/bin/glyphsvg --material home 100 --output="$TEST_DIR/home.svg"
    test -f "$TEST_DIR/home.svg" && echo "PASS" || echo "FAIL"

    echo ""
    echo "=== Test 8: Material - rounded + named weight, to file ==="
    ./build/bin/glyphsvg --material=rounded settings bold 100 --output="$TEST_DIR/settings.svg"
    test -f "$TEST_DIR/settings.svg" && echo "PASS" || echo "FAIL"

    echo ""
    echo "=== Test 9: Material - numeric --weight changes outline ==="
    ./build/bin/glyphsvg --material home 100 --weight=100 --output="$TEST_DIR/home_w100.svg"
    ./build/bin/glyphsvg --material home 100 --weight=700 --output="$TEST_DIR/home_w700.svg"
    if cmp -s "$TEST_DIR/home_w100.svg" "$TEST_DIR/home_w700.svg"; then echo "FAIL"; else echo "PASS"; fi

    echo ""
    echo "=== Test 10: Material - directory output named by symbol ==="
    mkdir -p "$TEST_DIR/material"
    ./build/bin/glyphsvg --material=sharp favorite 100 --output="$TEST_DIR/material/"
    test -f "$TEST_DIR/material/favorite.svg" && echo "PASS" || echo "FAIL"

    echo ""
    echo "=== Test 11a: Material - unknown symbol name exits nonzero ==="
    if ./build/bin/glyphsvg --material no_such_symbol_xyz 64 >/dev/null 2>&1; then
        echo "FAIL (exited 0)"
    else
        echo "PASS"
    fi

    echo ""
    echo "=== Test 11: Material - --fill changes the outline (FILL axis) ==="
    ./build/bin/glyphsvg --material favorite 100 --output="$TEST_DIR/fav_out.svg"
    ./build/bin/glyphsvg --material favorite 100 --fill --output="$TEST_DIR/fav_fill.svg"
    if cmp -s "$TEST_DIR/fav_out.svg" "$TEST_DIR/fav_fill.svg"; then echo "FAIL"; else echo "PASS"; fi
else
    echo ""
    echo "=== Material tests skipped (run ./material/download.sh first) ==="
fi

# Exit-status tests. The suite runs under `set -e`, so every command expected to
# fail is wrapped in an `if !` so the nonzero status is consumed rather than
# aborting the run.

echo ""
echo "=== Test 12: --version prints the version on stdout and exits 0 ==="
VER="$(./build/bin/glyphsvg --version 2>/dev/null || true)"
test "$VER" = "1.1" && echo "PASS" || echo "FAIL (got '$VER')"

echo ""
echo "=== Test 13: missing glyph exits nonzero and writes no file ==="
if ./build/bin/glyphsvg --font=Helvetica U+1F600 100 --output="$TEST_DIR/missing.svg" 2>/dev/null; then
    echo "FAIL (exited 0)"
elif [ -f "$TEST_DIR/missing.svg" ]; then
    echo "FAIL (wrote a file)"
else
    echo "PASS"
fi

echo ""
echo "=== Test 14: unknown font exits nonzero instead of substituting ==="
if ./build/bin/glyphsvg --font=NoSuchFontXYZ A 100 --output="$TEST_DIR/nofont.svg" 2>/dev/null; then
    echo "FAIL (exited 0)"
else
    echo "PASS"
fi

echo ""
echo "=== Test 15: installed fonts still resolve by family and PostScript name ==="
FONT_OK=1
for f in "Helvetica" "Helvetica-Bold" "Menlo" "Menlo-Regular"; do
    ./build/bin/glyphsvg --font="$f" A 50 >/dev/null 2>&1 || FONT_OK=0
done
test "$FONT_OK" = "1" && echo "PASS" || echo "FAIL"

echo ""
echo "=== Test 16: blank glyphs in a string are tolerated, exit stays 0 ==="
mkdir -p "$TEST_DIR/spaced"
if ./build/bin/glyphsvg --font=Helvetica "Hi there" 100 --output="$TEST_DIR/spaced/" >/dev/null 2>&1; then
    test -f "$TEST_DIR/spaced/U+48_0.svg" && echo "PASS" || echo "FAIL (no output)"
else
    echo "FAIL (exited nonzero)"
fi

echo ""
echo "=== Test 17: unknown SF Symbols name exits nonzero ==="
if ./build/bin/glyphsvg no_such_symbol_xyz bold 64 >/dev/null 2>&1; then
    echo "FAIL (exited 0)"
else
    echo "PASS"
fi

echo ""
echo "=== Test 18: partial failure exits nonzero but keeps the glyphs it got ==="
mkdir -p "$TEST_DIR/partial"
# The middle character (U+6F22) has no glyph in Helvetica, the outer two do.
# ANSI-C quoting, not "..." - a plain double-quoted string leaves \x unexpanded.
if ./build/bin/glyphsvg --font=Helvetica $'A\xe6\xbc\xa2B' 100 --output="$TEST_DIR/partial/" >/dev/null 2>&1; then
    echo "FAIL (exited 0)"
elif [ -f "$TEST_DIR/partial/U+41_0.svg" ] && [ -f "$TEST_DIR/partial/U+42_2.svg" ]; then
    echo "PASS"
else
    echo "FAIL (lost the glyphs that did resolve)"
fi

echo ""
echo "=== Test 19: an all-blank request exits nonzero ==="
if ./build/bin/glyphsvg --font=Helvetica " " 100 --output="$TEST_DIR/blank.svg" >/dev/null 2>&1; then
    echo "FAIL (exited 0)"
elif [ -f "$TEST_DIR/blank.svg" ]; then
    echo "FAIL (wrote a file)"
else
    echo "PASS"
fi

echo ""
echo "=== Test 20: multiple glyphs to a single file is refused, not overwritten ==="
if ./build/bin/glyphsvg --font=Helvetica "Hi" 100 --output="$TEST_DIR/single.svg" >/dev/null 2>&1; then
    echo "FAIL (exited 0)"
elif [ -f "$TEST_DIR/single.svg" ]; then
    echo "FAIL (wrote a file)"
else
    echo "PASS"
fi

echo ""
echo "=== All tests completed ==="
