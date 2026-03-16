#!/bin/bash
# SAST: Command Injection Detection
# Finds shell execution functions with user-controlled input
APP_ROOT="${1:-.}"
FAILURES=0

echo "  Checking for command injection vulnerabilities..."

# PHP: system(), exec(), shell_exec(), passthru(), popen(), proc_open() with variables
while IFS= read -r match; do
    echo "$match" | grep -qE '^\s*//' && continue
    echo "$match" | grep -qE '^\s*\*' && continue
    echo "    FAIL: Potential command injection: $match"
    ((FAILURES++))
done < <(grep -rn --include="*.php" -E '(system|exec|shell_exec|passthru|popen|proc_open)\s*\(' "$APP_ROOT/web/" 2>/dev/null | grep -vE 'escapeshellarg|escapeshellcmd' | head -20)

# C++: system(), popen() with string concat
while IFS= read -r match; do
    echo "$match" | grep -qE '^\s*//' && continue
    echo "    WARN: C++ shell call: $match"
    ((FAILURES++))
done < <(grep -rn --include="*.cpp" --include="*.h" -E '(system|popen)\s*\(' "$APP_ROOT/src/" 2>/dev/null | grep -vE 'include|#define|operating.system' | head -20)

if [ $FAILURES -eq 0 ]; then
    echo "  No command injection patterns detected."
    exit 0
else
    echo "  Found $FAILURES potential command injection issues."
    exit 1
fi
