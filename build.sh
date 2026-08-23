#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SOURCE_DIR="$SCRIPT_DIR/source"
BUILD_DIR="$SCRIPT_DIR/build"
BIN_DIR="$BUILD_DIR/bin"

CLANG="/usr/bin/clang"
STRIP="/usr/bin/strip"
VERSION="7.2"

# Minimum macOS version the binaries will run on. Without this, clang defaults
# to the build machine's OS version, producing binaries that refuse to launch on
# older systems. Override with: MIN_MACOS=12.0 ./build.sh
# 11.0 is the floor for a universal binary: arm64 macOS starts there, so a lower
# value only drops the x86_64 slice while arm64 silently clamps back to 11.0.
MIN_MACOS="${MIN_MACOS:-11.0}"

MODE="${1:-release}"
MODE="$(echo "$MODE" | tr '[:upper:]' '[:lower:]')"

if [ "$MODE" != "debug" ] && [ "$MODE" != "release" ]; then
    echo "Usage: $0 [debug|release]" >&2
    exit 1
fi

/bin/mkdir -p "$BIN_DIR"

ARCHS="-arch arm64 -arch x86_64"
DEPLOYMENT="-mmacosx-version-min=$MIN_MACOS"

if [ "$MODE" = "release" ]; then
    CFLAGS="-O3"
else
    CFLAGS="-O0 -g"
fi

echo "Building for macOS $MIN_MACOS and later"
echo "Building plist_generator ($MODE)..."
"$CLANG" $ARCHS "$DEPLOYMENT" -o "$BIN_DIR/plist_generator" \
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
"$CLANG" $ARCHS "$DEPLOYMENT" -o "$BIN_DIR/glyphsvg" \
    "$SOURCE_DIR/glyphsvg.c" \
    -framework CoreText \
    -framework CoreFoundation \
    -framework CoreGraphics \
    $CFLAGS

if [ "$MODE" = "release" ]; then
    "$STRIP" "$BIN_DIR/glyphsvg"
fi

echo "Done. Binaries in $BIN_DIR/"
