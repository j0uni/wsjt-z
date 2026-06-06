#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

APP_BUNDLE="build/wsjtx.app"
APP_MACOS_DIR="$APP_BUNDLE/Contents/MacOS"

# Match the higher runtime limits already documented by this repo's macOS build helper.
REQUIRED_SHMMAX=134217728
REQUIRED_SHMALL=32768

current_shmmax="$(sysctl -n kern.sysv.shmmax 2>/dev/null || echo 0)"
current_shmall="$(sysctl -n kern.sysv.shmall 2>/dev/null || echo 0)"

if [ "$current_shmmax" -lt "$REQUIRED_SHMMAX" ] || [ "$current_shmall" -lt "$REQUIRED_SHMALL" ]; then
  echo "Raising macOS shared-memory limits for WSJT-Z..."
  sudo sysctl -w "kern.sysv.shmmax=$REQUIRED_SHMMAX"
  sudo sysctl -w "kern.sysv.shmall=$REQUIRED_SHMALL"
fi

if [ -d build ]; then
  echo "Cleaning build directory..."
  cmake --build build --target clean
fi

echo "Building app..."
cmake --build build -j"$(sysctl -n hw.ncpu)"

if [ ! -d "$APP_BUNDLE" ]; then
  echo "Missing app bundle after build: $APP_BUNDLE"
  exit 1
fi

mkdir -p "$APP_MACOS_DIR"

if [ -f build/jt9 ]; then
  cp -f build/jt9 "$APP_MACOS_DIR/jt9"
else
  echo "Warning: build/jt9 not found; continuing without copying it."
fi

for f in ALLCALL7.TXT cty.dat jt9.txt; do
  if [ -f "$f" ]; then
    cp -f "$f" "$APP_MACOS_DIR/"
  else
    echo "Warning: $f not found; skipping."
  fi
done

open "$APP_BUNDLE"
