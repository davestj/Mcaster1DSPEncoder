#!/usr/bin/env bash
# make-dmg.sh — Create a distributable DMG for Mcaster1 DSP Encoder
#
# Usage: bash make-dmg.sh <path-to-.app> [version]
#   e.g. bash scripts/make-dmg.sh Mcaster1DSPEncoder.app 1.0.0
#
# Copyright (c) 2026 David St. John <davestj@gmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later

set -euo pipefail

APP_BUNDLE="${1:?Usage: make-dmg.sh <AppBundle.app> [version]}"
VERSION="${2:-1.0.0}"
VOL_NAME="Mcaster1 DSP Encoder ${VERSION}"
DMG_NAME="Mcaster1DSPEncoder-${VERSION}.dmg"

if [ ! -d "${APP_BUNDLE}" ]; then
    echo "ERROR: ${APP_BUNDLE} not found. Run 'make app-bundle' first." >&2
    exit 1
fi

TMPDIR_DMG=$(mktemp -d)
trap 'rm -rf "${TMPDIR_DMG}"' EXIT

echo "=== Creating DMG: ${DMG_NAME} ==="

# Copy .app bundle into temp staging directory
cp -R "${APP_BUNDLE}" "${TMPDIR_DMG}/"

# Create Applications symlink for drag-to-install
ln -s /Applications "${TMPDIR_DMG}/Applications"

# Remove any existing DMG
rm -f "${DMG_NAME}"

# Create the DMG
hdiutil create \
    -volname "${VOL_NAME}" \
    -srcfolder "${TMPDIR_DMG}" \
    -ov \
    -format UDZO \
    "${DMG_NAME}"

echo "=== DMG created: ${DMG_NAME} ==="
ls -lh "${DMG_NAME}"
