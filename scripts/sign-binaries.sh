#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════════════════
# sign-binaries.sh — GPG sign all Mcaster1 compiled binaries
#
# Usage: bash scripts/sign-binaries.sh [--verify-only]
#
# Signs:
#   src/linux/mcaster1-dsp-encoder-admin
#   src/linux/mcaster1-dsp-encoder
#   src/linux/mcaster1-voictune
#
# Keys:
#   Binary signing: 6C07628DF4D94C20 (MCaster1 Binary Code Signing)
#   Package signing: A29A09463F34D8D5 (MCaster1 LLC Package Signing)
#
# Copyright (c) 2026 David St. John <davestj@mcaster1.com>
# ═══════════════════════════════════════════════════════════════════════════════

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BINARY_KEY="6C07628DF4D94C20"
PACKAGE_KEY="A29A09463F34D8D5"

BINARIES=(
    "src/linux/mcaster1-dsp-encoder-admin"
    "src/linux/mcaster1-dsp-encoder"
    "src/linux/mcaster1-voictune"
)

VERIFY_ONLY=false
if [[ "${1:-}" == "--verify-only" ]]; then
    VERIFY_ONLY=true
fi

cd "$REPO_ROOT"

echo "═══════════════════════════════════════════════════════════════════"
echo "  Mcaster1 Binary Signing — $(date '+%Y-%m-%d %H:%M:%S')"
echo "  Binary key : $BINARY_KEY"
echo "  Package key: $PACKAGE_KEY"
echo "═══════════════════════════════════════════════════════════════════"
echo ""

# ── Check GPG key availability ──────────────────────────────────────────
if ! gpg --list-keys "$BINARY_KEY" &>/dev/null; then
    echo "ERROR: Binary signing key $BINARY_KEY not found in keyring"
    exit 1
fi

# ── Sign or verify each binary ──────────────────────────────────────────
SIGNED=0
FAILED=0

for bin in "${BINARIES[@]}"; do
    if [[ ! -f "$bin" ]]; then
        echo "  SKIP  $bin (not built)"
        continue
    fi

    sig="${bin}.asc"

    if $VERIFY_ONLY; then
        if [[ -f "$sig" ]]; then
            if gpg --verify "$sig" "$bin" 2>/dev/null; then
                echo "  OK    $bin — signature valid"
                SIGNED=$((SIGNED + 1))
            else
                echo "  FAIL  $bin — signature INVALID"
                FAILED=$((FAILED + 1))
            fi
        else
            echo "  MISS  $bin — no signature file"
            FAILED=$((FAILED + 1))
        fi
    else
        echo -n "  SIGN  $bin ... "
        gpg --local-user "$BINARY_KEY" --armor --detach-sign --yes --output "$sig" "$bin" 2>/dev/null
        echo "done → $sig"
        SIGNED=$((SIGNED + 1))
    fi
done

echo ""

# ── Generate SHA256 checksums ───────────────────────────────────────────
if ! $VERIFY_ONLY; then
    VERSION=$(sed -n 's/^AC_INIT(\[.*\], \[\(.*\)\],.*/\1/p' configure.ac)
    CHECKSUM_FILE="checksums-${VERSION:-unknown}.sha256"
    echo "  SHA256 checksums → $CHECKSUM_FILE"
    sha256sum "${BINARIES[@]}" 2>/dev/null > "$CHECKSUM_FILE" || true

    # Sign the checksum file with the package key
    if gpg --list-keys "$PACKAGE_KEY" &>/dev/null; then
        gpg --local-user "$PACKAGE_KEY" --armor --detach-sign --yes \
            --output "${CHECKSUM_FILE}.asc" "$CHECKSUM_FILE" 2>/dev/null
        echo "  SHA256 signature → ${CHECKSUM_FILE}.asc"
    fi
fi

echo ""
echo "── Summary ──────────────────────────────────────────────────────"
if $VERIFY_ONLY; then
    echo "  Verified: $SIGNED   Failed: $FAILED"
else
    echo "  Signed: $SIGNED binaries"
fi
echo ""
