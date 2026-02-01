#!/bin/bash
# make-icns.sh — Convert app-icon.svg to .icns for macOS app bundle
#
# Usage: bash scripts/make-icns.sh
# Requires: rsvg-convert (from librsvg) or sips + iconutil (macOS built-in)
#
# Output: resources/Mcaster1DSPEncoder.icns

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SRC_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
SVG="$SRC_DIR/resources/icons/app-icon.svg"
ICNS="$SRC_DIR/resources/Mcaster1DSPEncoder.icns"
ICONSET_DIR="$(mktemp -d)/Mcaster1DSPEncoder.iconset"

if [ ! -f "$SVG" ]; then
    echo "ERROR: $SVG not found" >&2
    exit 1
fi

mkdir -p "$ICONSET_DIR"

# Required sizes for .iconset: 16, 32, 64, 128, 256, 512, 1024
# Each has a @1x and @2x variant
sizes=(16 32 64 128 256 512)

render_png() {
    local size=$1
    local output=$2
    if command -v rsvg-convert &>/dev/null; then
        rsvg-convert -w "$size" -h "$size" "$SVG" -o "$output"
    elif command -v sips &>/dev/null; then
        # sips can't read SVG directly — use qlmanage as fallback
        # First try to render a large PNG, then scale with sips
        local tmp_png="$(mktemp).png"
        if command -v qlmanage &>/dev/null; then
            qlmanage -t -s 1024 -o "$(dirname "$tmp_png")" "$SVG" 2>/dev/null
            local ql_out="$(dirname "$tmp_png")/$(basename "$SVG").png"
            if [ -f "$ql_out" ]; then
                sips -z "$size" "$size" "$ql_out" --out "$output" &>/dev/null
                rm -f "$ql_out" "$tmp_png"
                return 0
            fi
        fi
        echo "WARNING: Cannot render SVG at ${size}px (no rsvg-convert or qlmanage)" >&2
        return 1
    else
        echo "ERROR: No SVG renderer found. Install librsvg: brew install librsvg" >&2
        exit 1
    fi
}

echo "Generating iconset from $SVG ..."

for size in "${sizes[@]}"; do
    double=$((size * 2))
    render_png "$size"   "$ICONSET_DIR/icon_${size}x${size}.png"
    render_png "$double" "$ICONSET_DIR/icon_${size}x${size}@2x.png"
done

# 512@2x = 1024
render_png 1024 "$ICONSET_DIR/icon_512x512@2x.png"

echo "Converting iconset to .icns ..."
iconutil -c icns "$ICONSET_DIR" -o "$ICNS"

rm -rf "$(dirname "$ICONSET_DIR")"

echo "Created: $ICNS"
ls -la "$ICNS"
