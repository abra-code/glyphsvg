#!/bin/bash
set -e

cd "$(dirname "$0")/.."

BUILD_DIR="./build"
BIN_DIR="$BUILD_DIR/bin"
TEST_DIR="$BUILD_DIR/test_output"

/bin/rm -rf "$TEST_DIR"
/bin/mkdir -p "$TEST_DIR"

FAILURES=0

# report <ok?> - prints PASS/FAIL and remembers failures for the exit status
report() {
    if [ "$1" = "0" ]; then
        echo "PASS"
    else
        echo "FAIL"
        FAILURES=$((FAILURES + 1))
    fi
}

# files_exist <path>... - PASS when every path is a regular file
files_exist() {
    for f in "$@"; do
        if [ ! -f "$f" ]; then report 1; return; fi
    done
    report 0
}

# files_differ <a> <b> - PASS when both files exist and differ (an option had an
# effect). cmp exits 2 for a missing file, so the existence check is what keeps a
# never-produced artifact from reading as "the option worked".
files_differ() {
    if [ ! -f "$1" ] || [ ! -f "$2" ]; then report 1; return; fi
    if cmp -s "$1" "$2"; then report 1; else report 0; fi
}

# files_match <a> <b> - PASS when both files exist and are byte-identical
files_match() {
    if [ ! -f "$1" ] || [ ! -f "$2" ]; then report 1; return; fi
    if cmp -s "$1" "$2"; then report 0; else report 1; fi
}

echo "=== Test 1: Custom font - single character to file ==="
"$BIN_DIR/glyphsvg" --font=Helvetica A 100 --output="$TEST_DIR/A.svg"
files_exist "$TEST_DIR/A.svg"

echo ""
echo "=== Test 2: Custom font - single character to stdout ==="
"$BIN_DIR/glyphsvg" --font=Helvetica A 100 > "$TEST_DIR/stdout.svg"
files_exist "$TEST_DIR/stdout.svg"

echo ""
echo "=== Test 3: Custom font - codepoint U+0041 to file ==="
"$BIN_DIR/glyphsvg" --font=Helvetica U+0041 100 --output="$TEST_DIR/codepoint.svg"
files_exist "$TEST_DIR/codepoint.svg"

echo ""
echo "=== Test 4: Custom font - multiple characters to directory ==="
mkdir -p "$TEST_DIR/multi"
"$BIN_DIR/glyphsvg" --font=Helvetica "Hi" 100 --output="$TEST_DIR/multi/"
files_exist "$TEST_DIR/multi/U+48_0.svg" "$TEST_DIR/multi/U+69_1.svg"

echo ""
echo "=== Test 5: SF Symbols - heart to file ==="
"$BIN_DIR/glyphsvg" heart bold 100 --output="$TEST_DIR/heart.svg"
files_exist "$TEST_DIR/heart.svg"

echo ""
echo "=== Test 6: SF Symbols - to stdout ==="
"$BIN_DIR/glyphsvg" star bold 100 > "$TEST_DIR/star.svg"
files_exist "$TEST_DIR/star.svg"

if [ -f "./material/MaterialSymbolsOutlined.codepoints" ]; then
    echo ""
    echo "=== Test 7: Material - default style to file ==="
    "$BIN_DIR/glyphsvg" --material home 100 --output="$TEST_DIR/home.svg"
    files_exist "$TEST_DIR/home.svg"

    echo ""
    echo "=== Test 8: Material - rounded + named weight, to file ==="
    "$BIN_DIR/glyphsvg" --material=rounded settings bold 100 --output="$TEST_DIR/settings.svg"
    files_exist "$TEST_DIR/settings.svg"

    echo ""
    echo "=== Test 9: Material - numeric --weight changes outline ==="
    "$BIN_DIR/glyphsvg" --material home 100 --weight=100 --output="$TEST_DIR/home_w100.svg"
    "$BIN_DIR/glyphsvg" --material home 100 --weight=700 --output="$TEST_DIR/home_w700.svg"
    files_differ "$TEST_DIR/home_w100.svg" "$TEST_DIR/home_w700.svg"

    echo ""
    echo "=== Test 10: Material - directory output named by symbol ==="
    mkdir -p "$TEST_DIR/material"
    "$BIN_DIR/glyphsvg" --material=sharp favorite 100 --output="$TEST_DIR/material/"
    files_exist "$TEST_DIR/material/favorite.svg"

    echo ""
    echo "=== Test 11a: Material - unknown symbol name exits nonzero ==="
    if ./build/bin/glyphsvg --material no_such_symbol_xyz 64 >/dev/null 2>&1; then
        echo "FAIL (exited 0)"
    else
        echo "PASS"
    fi

    echo ""
    echo "=== Test 11: Material - --fill changes the outline (FILL axis) ==="
    "$BIN_DIR/glyphsvg" --material favorite 100 --output="$TEST_DIR/fav_out.svg"
    "$BIN_DIR/glyphsvg" --material favorite 100 --fill --output="$TEST_DIR/fav_fill.svg"
    files_differ "$TEST_DIR/fav_out.svg" "$TEST_DIR/fav_fill.svg"

    echo ""
    echo "=== Test 12: Material - --material is an alias for --set=material ==="
    "$BIN_DIR/glyphsvg" --set=material --face=sharp favorite 100 --output="$TEST_DIR/set_favorite.svg"
    files_match "$TEST_DIR/material/favorite.svg" "$TEST_DIR/set_favorite.svg"

    echo ""
    echo "=== Test 13: Material - --axis reaches an axis --weight/--fill do not ==="
    "$BIN_DIR/glyphsvg" --material home 100 --axis=GRAD=200 --output="$TEST_DIR/home_grad.svg"
    files_differ "$TEST_DIR/home.svg" "$TEST_DIR/home_grad.svg"

    echo ""
    echo "=== Test 14: Material - --axis overrides --weight for the same axis ==="
    "$BIN_DIR/glyphsvg" --material home 100 --weight=100 --axis=wght=700 --output="$TEST_DIR/home_axis700.svg"
    files_match "$TEST_DIR/home_w700.svg" "$TEST_DIR/home_axis700.svg"

    echo ""
    echo "=== Test 15: Material - --info reports a variable font ==="
    "$BIN_DIR/glyphsvg" --material --info > "$TEST_DIR/material_info.txt"
    if grep -q "^variable: yes$" "$TEST_DIR/material_info.txt" &&
       grep -q "^axis: wght " "$TEST_DIR/material_info.txt"; then report 0; else report 1; fi

    # A directory holding only one style must not shadow a complete one further
    # down the search path, and must still work when it is all there is.
    PARTIAL_MATERIAL="$TEST_DIR/partialmaterial"
    mkdir -p "$PARTIAL_MATERIAL"
    cp ./material/MaterialSymbolsRounded.ttf ./material/MaterialSymbolsRounded.codepoints "$PARTIAL_MATERIAL/"

    echo ""
    echo "=== Test 15b: Material - a one-style directory does not shadow a complete one ==="
    "$BIN_DIR/glyphsvg" --material=outlined favorite 100 --output="$TEST_DIR/outlined_favorite.svg"
    GLYPHSVG_MATERIAL_DIR="$PARTIAL_MATERIAL" "$BIN_DIR/glyphsvg" --material favorite 100 \
        --output="$TEST_DIR/partial_default.svg"
    files_match "$TEST_DIR/outlined_favorite.svg" "$TEST_DIR/partial_default.svg"

    echo ""
    echo "=== Test 15c: Material - a manifest that only sets a title keeps the built-in faces ==="
    TITLED_MATERIAL="$TEST_DIR/titledmaterial"
    mkdir -p "$TITLED_MATERIAL"
    cp ./material/MaterialSymbolsOutlined.ttf ./material/MaterialSymbolsOutlined.codepoints "$TITLED_MATERIAL/"
    echo "title = My Material" > "$TITLED_MATERIAL/glyphset.conf"
    GLYPHSVG_MATERIAL_DIR="$TITLED_MATERIAL" "$BIN_DIR/glyphsvg" --material --info > "$TEST_DIR/titled_info.txt"
    if grep -q "^title: My Material$" "$TEST_DIR/titled_info.txt" &&
       grep -q "^face: Outlined$" "$TEST_DIR/titled_info.txt"; then report 0; else report 1; fi

    echo ""
    echo "=== Test 15d: --set and --material together is an error ==="
    if "$BIN_DIR/glyphsvg" --set=material --material home 100 \
        --output="$TEST_DIR/both.svg" 2>/dev/null; then report 1; else report 0; fi

    echo ""
    echo "=== Test 15e: --face wins over the --material=<style> spelling ==="
    "$BIN_DIR/glyphsvg" --material=rounded --face=sharp favorite 100 --output="$TEST_DIR/face_wins.svg"
    files_match "$TEST_DIR/material/favorite.svg" "$TEST_DIR/face_wins.svg"

    echo ""
    echo "=== Test 15f: the manifest 'default' key chooses the face ==="
    DEFAULTED_SET="$TEST_DIR/sets/defaulted"
    mkdir -p "$DEFAULTED_SET"
    cp ./material/MaterialSymbolsOutlined.ttf ./material/MaterialSymbolsSharp.ttf \
       ./material/MaterialSymbolsOutlined.codepoints "$DEFAULTED_SET/"
    cat > "$DEFAULTED_SET/glyphset.conf" <<'MANIFEST'
codepoints = MaterialSymbolsOutlined.codepoints
default = sharp
face = outlined  MaterialSymbolsOutlined.ttf
face = sharp     MaterialSymbolsSharp.ttf
MANIFEST
    "$BIN_DIR/glyphsvg" --set="$DEFAULTED_SET" favorite 100 --output="$TEST_DIR/defaulted.svg"
    files_match "$TEST_DIR/material/favorite.svg" "$TEST_DIR/defaulted.svg"

    # The face names of a variable single-face set are weight names by default
    # ("regular"), so this is where a face "answering" the weight could wrongly be
    # taken as having spent it. The font's wght axis can honor the request and must.
    VARIABLE_SET="$TEST_DIR/sets/varsingle"
    mkdir -p "$VARIABLE_SET"
    cp ./material/MaterialSymbolsOutlined.ttf "$VARIABLE_SET/only.ttf"
    cp ./material/MaterialSymbolsOutlined.codepoints "$VARIABLE_SET/only.codepoints"
    cat > "$VARIABLE_SET/glyphset.conf" <<'MANIFEST'
title      = Variable Single Face
font       = only.ttf
codepoints = only.codepoints
MANIFEST

    echo ""
    echo "=== Test 15g: a variable set with a weight-named face still honors --weight ==="
    "$BIN_DIR/glyphsvg" --set="$VARIABLE_SET" home 100 --weight=100 --output="$TEST_DIR/var_w100.svg"
    "$BIN_DIR/glyphsvg" --set="$VARIABLE_SET" home 100 --weight=700 --output="$TEST_DIR/var_w700.svg"
    files_differ "$TEST_DIR/var_w100.svg" "$TEST_DIR/var_w700.svg"

    echo ""
    echo "=== Test 15h: ... and --weight matches the equivalent --axis ==="
    "$BIN_DIR/glyphsvg" --set="$VARIABLE_SET" home 100 --axis=wght=700 --output="$TEST_DIR/var_axis700.svg"
    files_match "$TEST_DIR/var_w700.svg" "$TEST_DIR/var_axis700.svg"

    echo ""
    echo "=== Test 15i: one weight-named face among styles does not capture every weight ==="
    MIXED_SET="$TEST_DIR/sets/mixed"
    mkdir -p "$MIXED_SET"
    cp ./material/MaterialSymbolsOutlined.ttf "$MIXED_SET/O.ttf"
    cp ./material/MaterialSymbolsRounded.ttf "$MIXED_SET/R.ttf"
    cp ./material/MaterialSymbolsOutlined.codepoints "$MIXED_SET/c.codepoints"
    cat > "$MIXED_SET/glyphset.conf" <<'MANIFEST'
codepoints = c.codepoints
face = Outlined  O.ttf
face = bold      R.ttf
MANIFEST
    "$BIN_DIR/glyphsvg" --set="$MIXED_SET" home 100 --weight=100 --output="$TEST_DIR/mixed_w100.svg"
    "$BIN_DIR/glyphsvg" --set="$MIXED_SET" home 100 --face=Outlined --weight=100 \
        --output="$TEST_DIR/mixed_outlined_w100.svg"
    files_match "$TEST_DIR/mixed_w100.svg" "$TEST_DIR/mixed_outlined_w100.svg"
else
    echo ""
    echo "=== Material tests skipped (run ./material/download.sh first) ==="
fi

# Static-font coverage. Any three static faces of one family will do; the stock
# macOS Arial files stand in for a static icon family such as MDI or Fluent.
STATIC_REGULAR="/System/Library/Fonts/Supplemental/Arial.ttf"
STATIC_BOLD="/System/Library/Fonts/Supplemental/Arial Bold.ttf"
STATIC_BLACK="/System/Library/Fonts/Supplemental/Arial Black.ttf"

if [ -f "$STATIC_REGULAR" ] && [ -f "$STATIC_BOLD" ] && [ -f "$STATIC_BLACK" ]; then
    STATIC_SET="$TEST_DIR/sets/teststatic"
    mkdir -p "$STATIC_SET"
    cp "$STATIC_REGULAR" "$STATIC_SET/Test-Regular.ttf"
    cp "$STATIC_BOLD" "$STATIC_SET/Test-Bold.ttf"
    cp "$STATIC_BLACK" "$STATIC_SET/Test-Black.ttf"
    cat > "$STATIC_SET/test.codepoints" <<'CODEPOINTS'
letter_a 41
letter_b 42
CODEPOINTS
    cat > "$STATIC_SET/glyphset.conf" <<'MANIFEST'
title      = Static Test Family
codepoints = test.codepoints
face = regular  Test-Regular.ttf
face = bold     Test-Bold.ttf
face = black    Test-Black.ttf
MANIFEST

    echo ""
    echo "=== Test 16: Static set - name lookup with the default face ==="
    "$BIN_DIR/glyphsvg" --set="$STATIC_SET" letter_a 100 --output="$TEST_DIR/static_regular.svg"
    files_exist "$TEST_DIR/static_regular.svg"

    echo ""
    echo "=== Test 17: Static set - --info reports a static font ==="
    "$BIN_DIR/glyphsvg" --set="$STATIC_SET" --info > "$TEST_DIR/static_info.txt"
    if grep -q "^variable: no$" "$TEST_DIR/static_info.txt" &&
       grep -q "^faces: regular bold black$" "$TEST_DIR/static_info.txt"; then report 0; else report 1; fi

    echo ""
    echo "=== Test 18: Static set - a named weight selects the matching face ==="
    "$BIN_DIR/glyphsvg" --set="$STATIC_SET" letter_a bold 100 --output="$TEST_DIR/static_bold.svg"
    files_differ "$TEST_DIR/static_regular.svg" "$TEST_DIR/static_bold.svg"

    echo ""
    echo "=== Test 19: Static set - a numeric weight selects the nearest face ==="
    # The faces are regular/bold/black (400/700/900). 800 hits no weight name, so
    # this exercises nearest-neighbor rather than an exact hit on the name ladder;
    # equidistant from bold and black, it must take the lighter one.
    "$BIN_DIR/glyphsvg" --set="$STATIC_SET" letter_a 100 --weight=800 --output="$TEST_DIR/static_w800.svg"
    "$BIN_DIR/glyphsvg" --set="$STATIC_SET" letter_a 100 --face=bold --output="$TEST_DIR/static_facebold.svg"
    files_match "$TEST_DIR/static_w800.svg" "$TEST_DIR/static_facebold.svg"

    echo ""
    echo "=== Test 19b: Static set - nearest face is monotonic in the weight ==="
    "$BIN_DIR/glyphsvg" --set="$STATIC_SET" letter_a 100 --weight=1000 --output="$TEST_DIR/static_w1000.svg"
    "$BIN_DIR/glyphsvg" --set="$STATIC_SET" letter_a 100 --face=black --output="$TEST_DIR/static_black.svg"
    files_match "$TEST_DIR/static_w1000.svg" "$TEST_DIR/static_black.svg"

    REVERSED_SET="$TEST_DIR/sets/reversed"
    mkdir -p "$REVERSED_SET"
    cp "$STATIC_SET"/Test-*.ttf "$STATIC_SET/test.codepoints" "$REVERSED_SET/"
    cat > "$REVERSED_SET/glyphset.conf" <<'MANIFEST'
codepoints = test.codepoints
face = black    Test-Black.ttf
face = bold     Test-Bold.ttf
face = regular  Test-Regular.ttf
MANIFEST

    echo ""
    echo "=== Test 19c: Static set - a light weight reaches the lightest face ==="
    # Asserted against the reversed fixture on purpose: there faces[0] is 'black',
    # so landing on 'regular' can only mean nearest-weight actually ran. The same
    # assertion on the main fixture would also pass if it silently fell back.
    "$BIN_DIR/glyphsvg" --set="$REVERSED_SET" letter_a 100 --weight=300 --output="$TEST_DIR/reversed_w300.svg"
    "$BIN_DIR/glyphsvg" --set="$REVERSED_SET" letter_a 100 --face=regular --output="$TEST_DIR/reversed_reg.svg"
    files_match "$TEST_DIR/reversed_w300.svg" "$TEST_DIR/reversed_reg.svg"

    echo ""
    echo "=== Test 19d: Static set - face order in the manifest does not matter ==="
    "$BIN_DIR/glyphsvg" --set="$REVERSED_SET" letter_a 100 --weight=800 --output="$TEST_DIR/reversed_w800.svg"
    files_match "$TEST_DIR/static_w800.svg" "$TEST_DIR/reversed_w800.svg"

    echo ""
    echo "=== Test 20: Static set - an unknown face is an error ==="
    if "$BIN_DIR/glyphsvg" --set="$STATIC_SET" letter_a 100 --face=nope \
        --output="$TEST_DIR/static_bogus.svg" 2>/dev/null; then report 1; else report 0; fi

    echo ""
    echo "=== Test 21: Static set - --fill is ignored, not fatal ==="
    "$BIN_DIR/glyphsvg" --set="$STATIC_SET" letter_a 100 --fill --output="$TEST_DIR/static_fill.svg"
    files_match "$TEST_DIR/static_regular.svg" "$TEST_DIR/static_fill.svg"

    echo ""
    echo "=== Test 22: --font-file with --codepoints matches the set render ==="
    "$BIN_DIR/glyphsvg" --font-file="$STATIC_SET/Test-Regular.ttf" \
        --codepoints="$STATIC_SET/test.codepoints" letter_a 100 --output="$TEST_DIR/fontfile_name.svg"
    files_match "$TEST_DIR/static_regular.svg" "$TEST_DIR/fontfile_name.svg"

    echo ""
    echo "=== Test 23: --font-file with character input matches the same glyph ==="
    "$BIN_DIR/glyphsvg" --font-file="$STATIC_SET/Test-Regular.ttf" A 100 --output="$TEST_DIR/fontfile_char.svg"
    files_match "$TEST_DIR/static_regular.svg" "$TEST_DIR/fontfile_char.svg"

    echo ""
    echo "=== Test 24: --font= given a path takes the by-path route ==="
    "$BIN_DIR/glyphsvg" --font="$STATIC_SET/Test-Regular.ttf" A 100 --output="$TEST_DIR/fontpath_char.svg"
    files_match "$TEST_DIR/static_regular.svg" "$TEST_DIR/fontpath_char.svg"

    echo ""
    echo "=== Test 25: Single-face set - 'font =' shorthand needs no face line ==="
    SINGLE_SET="$TEST_DIR/sets/single"
    mkdir -p "$SINGLE_SET"
    cp "$STATIC_REGULAR" "$SINGLE_SET/only.ttf"
    echo "letter_a 41" > "$SINGLE_SET/only.codepoints"
    cat > "$SINGLE_SET/glyphset.conf" <<'MANIFEST'
title      = Single Face Set
font       = only.ttf
codepoints = only.codepoints
MANIFEST
    "$BIN_DIR/glyphsvg" --set="$SINGLE_SET" letter_a 100 --output="$TEST_DIR/single.svg"
    files_match "$TEST_DIR/static_regular.svg" "$TEST_DIR/single.svg"

    echo ""
    echo "=== Test 26: A set is found by name under GLYPHSVG_SET_PATH ==="
    GLYPHSVG_SET_PATH="$TEST_DIR/sets" "$BIN_DIR/glyphsvg" --set=teststatic letter_a 100 \
        --output="$TEST_DIR/static_bypath.svg"
    files_match "$TEST_DIR/static_regular.svg" "$TEST_DIR/static_bypath.svg"

    echo ""
    echo "=== Test 27: An unknown set is an error ==="
    if "$BIN_DIR/glyphsvg" --set=nosuchset letter_a 100 \
        --output="$TEST_DIR/nosuch.svg" 2>/dev/null; then report 1; else report 0; fi

    echo ""
    echo "=== Test 27b: A bare name is NOT searched for beside the executable ==="
    # The set sits next to the binary and one level up from it. Neither is a search
    # root, so this must fail: the search is GLYPHSVG_SET_PATH and the current
    # directory, nothing else. A pass here means the search widened again.
    # Absolute paths throughout: the point of the test is where the SET is looked
    # for, so the binary and the output must not also depend on the working
    # directory, or the test passes for the wrong reason.
    ABS_BIN="$(cd "$BIN_DIR" && pwd)/glyphsvg"
    ABS_TEST_DIR="$(cd "$TEST_DIR" && pwd)"
    EXE_ADJACENT="$(cd "$BIN_DIR" && pwd)/exeset"
    mkdir -p "$EXE_ADJACENT"
    cp "$STATIC_SET/Test-Regular.ttf" "$STATIC_SET/test.codepoints" "$EXE_ADJACENT/"
    cat > "$EXE_ADJACENT/glyphset.conf" <<'MANIFEST'
font       = Test-Regular.ttf
codepoints = test.codepoints
MANIFEST
    # Sanity: the binary must actually run from here, or "it failed" proves nothing.
    "$ABS_BIN" --set="$EXE_ADJACENT" letter_a 100 --output="$ABS_TEST_DIR/exeset_direct.svg"
    if [ ! -f "$ABS_TEST_DIR/exeset_direct.svg" ]; then
        report 1
    elif (cd "$ABS_TEST_DIR" && "$ABS_BIN" --set=exeset letter_a 100 \
            --output="$ABS_TEST_DIR/exeset.svg" 2>/dev/null); then
        report 1
    else
        report 0
    fi

    echo ""
    echo "=== Test 27bb: A bare name is NOT searched for under ~/Library ==="
    FAKE_HOME="$ABS_TEST_DIR/fakehome"
    mkdir -p "$FAKE_HOME/Library/Application Support/glyphsvg/homeset"
    cp "$STATIC_SET/Test-Regular.ttf" "$STATIC_SET/test.codepoints" \
        "$FAKE_HOME/Library/Application Support/glyphsvg/homeset/"
    cat > "$FAKE_HOME/Library/Application Support/glyphsvg/homeset/glyphset.conf" <<'MANIFEST'
font       = Test-Regular.ttf
codepoints = test.codepoints
MANIFEST
    if (cd "$FAKE_HOME" && HOME="$FAKE_HOME" "$ABS_BIN" --set=homeset letter_a 100 \
            --output="$ABS_TEST_DIR/homeset.svg" 2>/dev/null); then report 1; else report 0; fi

    echo ""
    echo "=== Test 27c: ... but naming it through GLYPHSVG_SET_PATH works ==="
    GLYPHSVG_SET_PATH="$(cd "$BIN_DIR" && pwd)" "$BIN_DIR/glyphsvg" --set=exeset letter_a 100 \
        --output="$TEST_DIR/exeset_env.svg"
    files_match "$TEST_DIR/static_regular.svg" "$TEST_DIR/exeset_env.svg"
else
    echo ""
    echo "=== Static font tests skipped (Arial not found in /System/Library/Fonts/Supplemental) ==="
fi

# A variable font whose wght axis does not span 400 catches the tool substituting
# its own idea of "regular" for the font's declared default.
ODD_AXIS_FONT="/System/Library/Fonts/Supplemental/Skia.ttf"
if [ -f "$ODD_AXIS_FONT" ]; then
    echo ""
    echo "=== Test 28: A variable font with no weight asked for renders its default instance ==="
    "$BIN_DIR/glyphsvg" --font-file="$ODD_AXIS_FONT" --info > "$TEST_DIR/odd_info.txt"
    DEFAULT_WGHT="$(awk '$2 == "wght" { print $4 }' "$TEST_DIR/odd_info.txt")"
    MAX_WGHT="$(awk '$2 == "wght" { print $5 }' "$TEST_DIR/odd_info.txt")"
    if [ -n "$DEFAULT_WGHT" ] && [ -n "$MAX_WGHT" ]; then
        "$BIN_DIR/glyphsvg" --font-file="$ODD_AXIS_FONT" A 100 --output="$TEST_DIR/odd_none.svg"
        "$BIN_DIR/glyphsvg" --font-file="$ODD_AXIS_FONT" A 100 --axis="wght=$DEFAULT_WGHT" \
            --output="$TEST_DIR/odd_default.svg"
        files_match "$TEST_DIR/odd_none.svg" "$TEST_DIR/odd_default.svg"

        echo ""
        echo "=== Test 29: ... and an explicit weight still moves that axis ==="
        "$BIN_DIR/glyphsvg" --font-file="$ODD_AXIS_FONT" A 100 --axis="wght=$MAX_WGHT" \
            --output="$TEST_DIR/odd_max.svg"
        files_differ "$TEST_DIR/odd_none.svg" "$TEST_DIR/odd_max.svg"
    else
        report 1
        echo "(--info reported no wght axis for $ODD_AXIS_FONT)"
    fi
else
    echo ""
    echo "=== Variable-default tests skipped (Skia.ttf not found) ==="
fi

# Supplementary-plane codepoints take the surrogate-pair branch, which every
# Material Design Icons glyph uses (MDI lives in U+F0001..U+F1D17).
# U+1D400 is MATHEMATICAL BOLD CAPITAL A, which STIXGeneral covers.
ASTRAL_FONT="/System/Library/Fonts/Supplemental/STIXGeneral.otf"
if [ -f "$ASTRAL_FONT" ]; then
    echo ""
    echo "=== Test 30: A supplementary-plane codepoint renders (surrogate pair path) ==="
    "$BIN_DIR/glyphsvg" --font-file="$ASTRAL_FONT" U+1D400 100 --output="$TEST_DIR/astral.svg"
    files_exist "$TEST_DIR/astral.svg"

    echo ""
    echo "=== Test 31: A supplementary-plane glyph differs from its BMP namesake ==="
    "$BIN_DIR/glyphsvg" --font-file="$ASTRAL_FONT" U+0041 100 --output="$TEST_DIR/bmp_a.svg"
    files_differ "$TEST_DIR/astral.svg" "$TEST_DIR/bmp_a.svg"
else
    echo ""
    echo "=== Supplementary-plane tests skipped (STIXGeneral.otf not found) ==="
fi

# Exit-status tests, from the remote branch. Renumbered to follow the tests above
# and routed through report() so a failure actually fails the suite. The suite runs
# under `set -e`, so every command expected to fail is wrapped in an `if !` so the
# nonzero status is consumed rather than aborting the run.

echo ""
echo "=== Test 32: --version prints the version on stdout and exits 0 ==="
VER="$("$BIN_DIR/glyphsvg" --version 2>/dev/null || true)"
if [ "$VER" = "1.1" ]; then report 0; else report 1; echo "(got '$VER')"; fi

echo ""
echo "=== Test 33: missing glyph exits nonzero and writes no file ==="
if "$BIN_DIR/glyphsvg" --font=Helvetica U+1F600 100 --output="$TEST_DIR/missing.svg" 2>/dev/null; then
    report 1
    echo "(exited 0)"
elif [ -f "$TEST_DIR/missing.svg" ]; then
    report 1
    echo "(wrote a file)"
else
    report 0
fi

echo ""
echo "=== Test 34: unknown font exits nonzero instead of substituting ==="
if "$BIN_DIR/glyphsvg" --font=NoSuchFontXYZ A 100 --output="$TEST_DIR/nofont.svg" 2>/dev/null; then
    report 1
    echo "(exited 0)"
else
    report 0
fi

echo ""
echo "=== Test 35: installed fonts still resolve by family and PostScript name ==="
FONT_OK=1
for f in "Helvetica" "Helvetica-Bold" "Menlo" "Menlo-Regular"; do
    "$BIN_DIR/glyphsvg" --font="$f" A 50 >/dev/null 2>&1 || FONT_OK=0
done
if [ "$FONT_OK" = "1" ]; then report 0; else report 1; fi

echo ""
echo "=== Test 36: blank glyphs in a string are tolerated, exit stays 0 ==="
mkdir -p "$TEST_DIR/spaced"
if "$BIN_DIR/glyphsvg" --font=Helvetica "Hi there" 100 --output="$TEST_DIR/spaced/" >/dev/null 2>&1; then
    files_exist "$TEST_DIR/spaced/U+48_0.svg"
else
    report 1
    echo "(exited nonzero)"
fi

echo ""
echo "=== Test 37: unknown SF Symbols name exits nonzero ==="
if "$BIN_DIR/glyphsvg" no_such_symbol_xyz bold 64 >/dev/null 2>&1; then
    report 1
    echo "(exited 0)"
else
    report 0
fi

echo ""
echo "=== Test 38: partial failure exits nonzero but keeps the glyphs it got ==="
mkdir -p "$TEST_DIR/partial"
# The middle character (U+6F22) has no glyph in Helvetica, the outer two do.
# ANSI-C quoting, not "..." - a plain double-quoted string leaves \x unexpanded.
if "$BIN_DIR/glyphsvg" --font=Helvetica $'A\xe6\xbc\xa2B' 100 --output="$TEST_DIR/partial/" >/dev/null 2>&1; then
    report 1
    echo "(exited 0)"
elif [ -f "$TEST_DIR/partial/U+41_0.svg" ] && [ -f "$TEST_DIR/partial/U+42_2.svg" ]; then
    report 0
else
    report 1
    echo "(lost the glyphs that did resolve)"
fi

echo ""
echo "=== Test 39: an all-blank request exits nonzero ==="
if "$BIN_DIR/glyphsvg" --font=Helvetica " " 100 --output="$TEST_DIR/blank.svg" >/dev/null 2>&1; then
    report 1
    echo "(exited 0)"
elif [ -f "$TEST_DIR/blank.svg" ]; then
    report 1
    echo "(wrote a file)"
else
    report 0
fi

echo ""
echo "=== Test 40: multiple glyphs to a single file is refused, not overwritten ==="
# Distinct filename on purpose: the single-face set test above writes single.svg,
# and a leftover from it would read here as "this run wrote a file".
if "$BIN_DIR/glyphsvg" --font=Helvetica "Hi" 100 --output="$TEST_DIR/multi_into_one.svg" >/dev/null 2>&1; then
    report 1
    echo "(exited 0)"
elif [ -f "$TEST_DIR/multi_into_one.svg" ]; then
    report 1
    echo "(wrote a file)"
else
    report 0
fi

echo ""
if [ "$FAILURES" -eq 0 ]; then
    echo "=== All tests passed ==="
else
    echo "=== $FAILURES test(s) FAILED ==="
fi
exit "$FAILURES"
