#!/bin/bash
# SAST: Authentication Bypass Detection
# Finds PHP API endpoints missing auth checks
APP_ROOT="${1:-.}"
FAILURES=0

echo "  Checking for authentication bypass..."

# Find PHP files in api/ that don't call require_auth or require_admin
for f in $(find "$APP_ROOT/web" -path "*/api/*.php" -type f 2>/dev/null); do
    # Skip non-API files
    basename "$f" | grep -qE '^(index|\.ht)' && continue

    if ! grep -qE 'require_auth|require_admin|mc1_is_authed' "$f" 2>/dev/null; then
        # Check if it's a public endpoint (health, login)
        if grep -qE 'health|login|auth\.php' <<< "$f"; then
            continue
        fi
        echo "    FAIL: Missing auth check: $f"
        ((FAILURES++))
    fi
done

if [ $FAILURES -eq 0 ]; then
    echo "  All API endpoints have auth checks."
    exit 0
else
    echo "  Found $FAILURES API endpoints without auth checks."
    exit 1
fi
