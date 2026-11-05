#!/usr/bin/env bash
# Cross-compile DisplaceBox for Windows from Linux (MinGW-w64).
set -euo pipefail

cd "$(dirname "$0")"

CC="${CC:-x86_64-w64-mingw32-gcc}"
WINDRES="${WINDRES:-x86_64-w64-mingw32-windres}"
AR="${AR:-x86_64-w64-mingw32-ar}"
RANLIB="${RANLIB:-x86_64-w64-mingw32-ranlib}"

RAYLIB_DIR="../raylib-6.2"
BUILD_DIR="build-win"
RAYLIB_BUILD="$BUILD_DIR/raylib-src"
RAYLIB_LIB="$RAYLIB_BUILD/src/libraylib.a"

mkdir -p "$BUILD_DIR"

if [ ! -f "$RAYLIB_LIB" ]; then
    echo "== building raylib for Windows =="
    rm -rf "$RAYLIB_BUILD"
    mkdir -p "$RAYLIB_BUILD"
    cp -a "$RAYLIB_DIR/src" "$RAYLIB_BUILD/src"
    make -C "$RAYLIB_BUILD/src" -j"$(nproc)" \
        OS=Windows_NT PLATFORM=PLATFORM_DESKTOP RAYLIB_LIBTYPE=STATIC \
        CC="$CC" AR="$AR" RANLIB="$RANLIB"
fi

echo "== building DisplaceBox.exe =="
make -f Makefile.win \
    CC="$CC" WINDRES="$WINDRES" \
    RAYLIB_DIR="$RAYLIB_DIR" RAYLIB_LIB="$RAYLIB_LIB" \
    BUILD_DIR="$BUILD_DIR/app" \
    TARGET="dist/windows/displacebox.exe"

mkdir -p dist/windows/assets
cp -r assets/. dist/windows/assets/
echo "== done: dist/windows/displacebox.exe =="
