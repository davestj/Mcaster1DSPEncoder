#!/bin/bash
# SAST: XSS Detection
# Finds unescaped output in PHP files (missing h() or htmlspecialchars())
APP_ROOT="${1:-.}"
FAILURES=0

echo "  Checking for XSS vulnerabilities..."

# Find echo/print statements with raw variables (not wrapped in h() or htmlspecialchars)
while IFS= read -r match; do
    echo "$match" | grep -qE '^\s*//' && continue
    echo "$match" | grep -qE 'json_encode' && continue
    echo "$match" | grep -qE 'header\(' && continue
    echo "$match" | grep -qE "Content-Type.*json" && continue
    echo "    FAIL: Potential XSS: $match"
    ((FAILURES++))
done < <(grep -rn --include="*.php" -E 'echo\s+\$|print\s+\$' "$APP_ROOT/web/" 2>/dev/null | grep -vE 'h\(|htmlspecialchars|json_encode|header\(|Content-Type|api_respond|mc1_api_respond' | head -20)

# Find <?= without h() wrapper
while IFS= read -r match; do
    echo "$match" | grep -qE 'h\(' && continue
    echo "$match" | grep -qE 'json_encode' && continue
    echo "$match" | grep -qE 'htmlspecialchars' && continue
    echo "$match" | grep -qE '\$_mc1c_theme' && continue  # theme class is safe
    echo "$match" | grep -qE 'mc1_authed|active_org|CHATTER_' && continue  # int/const
    echo "    WARN: Unescaped short tag: $match"
    ((FAILURES++))
done < <(grep -rn --include="*.php" '<\?=' "$APP_ROOT/web/" 2>/dev/null | grep -vE 'h\(|htmlspecialchars|json_encode|int\)|number_format|count\(|date\(|mc1_authed|active_org|CHATTER_|_POLL' | head -20)

if [ $FAILURES -eq 0 ]; then
    echo "  No XSS patterns detected."
    exit 0
else
    echo "  Found $FAILURES potential XSS issues."
    exit 1
fi
