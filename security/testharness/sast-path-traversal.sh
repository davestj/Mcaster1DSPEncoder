#!/bin/bash
# SAST: Path Traversal Detection
# Finds file operations using user input without sanitization
APP_ROOT="${1:-.}"
FAILURES=0

echo "  Checking for path traversal vulnerabilities..."

# PHP: file operations with $_GET/$_POST/$_REQUEST without realpath
while IFS= read -r match; do
    echo "$match" | grep -qE 'realpath|basename' && continue
    echo "    WARN: File operation with user input: $match"
    ((FAILURES++))
done < <(grep -rn --include="*.php" -E '(file_get_contents|fopen|include|require|readfile|unlink|rename|copy|move_uploaded_file)\s*\(.*\$_(GET|POST|REQUEST|FILES)' "$APP_ROOT/web/" 2>/dev/null | head -20)

# C++: Check for ".." in path handling without sanitization
while IFS= read -r match; do
    echo "$match" | grep -qE 'realpath|canonical' && continue
    echo "    WARN: C++ path operation: $match"
    ((FAILURES++))
done < <(grep -rn --include="*.cpp" -E 'open\(.*path|ifstream.*path' "$APP_ROOT/src/" 2>/dev/null | grep -E '\+|append|concat' | head -10)

if [ $FAILURES -eq 0 ]; then
    echo "  No path traversal patterns detected."
    exit 0
else
    echo "  Found $FAILURES potential path traversal issues."
    exit 1
fi
