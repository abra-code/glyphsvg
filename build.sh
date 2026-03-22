#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SOURCE_DIR="$SCRIPT_DIR/source"
BUILD_DIR="$SCRIPT_DIR/build"
BIN_DIR="$BUILD_DIR/bin"

CLANG="/usr/bin/clang"
STRIP="/usr/bin/strip"
VERSION="7.2"

MODE="${1:-release}"
MODE="$(echo "$MODE" | tr '[:upper:]' '[:lower:]')"

if [ "$MODE" != "debug" ] && [ "$MODE" != "release" ]; then
    echo "Usage: $0 [debug|release]" >&2
    exit 1
fi

/bin/mkdir -p "$BIN_DIR"

ARCHS="-arch arm64 -arch x86_64"

if [ "$MODE" = "release" ]; then
    CFLAGS="-O3"
else
    CFLAGS="-O0 -g"
fi

echo "Building plist_generator ($MODE)..."
"$CLANG" $ARCHS -o "$BIN_DIR/plist_generator" \
    "$SOURCE_DIR/plist_generator.c" \
    -framework CoreFoundation \
    $CFLAGS

if [ "$MODE" = "release" ]; then
    "$STRIP" "$BIN_DIR/plist_generator"
fi

echo "Generating sfmap.plist..."
"$BIN_DIR/plist_generator" \
    "$SCRIPT_DIR/sfmap/names_$VERSION.txt" \
    "$SCRIPT_DIR/sfmap/symbols_$VERSION.txt" \
    "$BIN_DIR/sfmap.plist"

echo "Building glyphsvg ($MODE)..."
"$CLANG" $ARCHS -o "$BIN_DIR/glyphsvg" \
    "$SOURCE_DIR/glyphsvg.c" \
    -framework CoreText \
    -framework CoreFoundation \
    -framework CoreGraphics \
    $CFLAGS

if [ "$MODE" = "release" ]; then
    "$STRIP" "$BIN_DIR/glyphsvg"
fi

echo "Done. Binaries in $BIN_DIR/"
