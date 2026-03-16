#!/bin/bash
# SAST: CSRF Protection Detection
# Finds POST handlers missing CSRF token validation
APP_ROOT="${1:-.}"
FAILURES=0

echo "  Checking for CSRF protection..."

# Find PHP files that handle POST but don't verify CSRF
for f in $(find "$APP_ROOT/web" -path "*/api/*.php" -type f 2>/dev/null); do
    if grep -qE "\\\$_POST|\\\$method\s*===\s*'POST'|REQUEST_METHOD.*POST" "$f" 2>/dev/null; then
        if ! grep -qE 'verify_csrf|csrf_token|X-Requested-With|X-CSRF' "$f" 2>/dev/null; then
            echo "    WARN: POST handler without CSRF check: $f"
            ((FAILURES++))
        fi
    fi
done

if [ $FAILURES -eq 0 ]; then
    echo "  All POST handlers have CSRF protection."
    exit 0
else
    echo "  Found $FAILURES POST handlers without CSRF protection."
    exit 1
fi
