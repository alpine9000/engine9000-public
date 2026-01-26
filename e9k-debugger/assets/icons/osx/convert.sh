#!/usr/bin/env bash
set -euo pipefail

SRC="engine9000.png"
APP="Engine9000"

ICONSET="${APP}.iconset"
ICNS="${APP}.icns"
ICO="engine9000.ico"

# --- Sanity checks ----------------------------------------------------------

if [ ! -f "$SRC" ]; then
  echo "❌ Source image not found: $SRC"
  exit 1
fi

# --- macOS ICONSET ----------------------------------------------------------

echo "🧱 Building macOS iconset…"
rm -rf "$ICONSET"
mkdir "$ICONSET"

sips -z 16   16   "$SRC" --out "$ICONSET/icon_16x16.png" >/dev/null
sips -z 32   32   "$SRC" --out "$ICONSET/icon_16x16@2x.png" >/dev/null
sips -z 32   32   "$SRC" --out "$ICONSET/icon_32x32.png" >/dev/null
sips -z 64   64   "$SRC" --out "$ICONSET/icon_32x32@2x.png" >/dev/null
sips -z 128  128  "$SRC" --out "$ICONSET/icon_128x128.png" >/dev/null
sips -z 256  256  "$SRC" --out "$ICONSET/icon_128x128@2x.png" >/dev/null
sips -z 256  256  "$SRC" --out "$ICONSET/icon_256x256.png" >/dev/null
sips -z 512  512  "$SRC" --out "$ICONSET/icon_256x256@2x.png" >/dev/null
sips -z 512  512  "$SRC" --out "$ICONSET/icon_512x512.png" >/dev/null
sips -z 1024 1024 "$SRC" --out "$ICONSET/icon_512x512@2x.png" >/dev/null

echo "📦 Building ICNS…"
iconutil -c icns "$ICONSET" -o "$ICNS"

# --- Done -------------------------------------------------------------------

echo "✅ Done!"
echo "  macOS : $ICNS"
echo "  Windows: $ICO"
