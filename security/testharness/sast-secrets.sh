#!/bin/bash
# SAST: Secrets/Credential Detection
# Finds hardcoded passwords, API keys, tokens in source code
APP_ROOT="${1:-.}"
FAILURES=0

echo "  Checking for hardcoded secrets..."

# Check for common secret patterns in PHP/C++/Go source (not config files)
while IFS= read -r match; do
    # Skip config files, CLAUDE.md, test files, YAML
    echo "$match" | grep -qiE '\.(yaml|yml|md|txt|conf|ini|example)' && continue
    echo "$match" | grep -qiE 'CLAUDE\.md|README|CHANGELOG' && continue
    echo "$match" | grep -qiE '/config/' && continue
    echo "$match" | grep -qiE '/etc/' && continue
    echo "$match" | grep -qiE 'test|spec|mock' && continue
    echo "    WARN: Possible hardcoded secret: $match"
    ((FAILURES++))
done < <(grep -rn --include="*.php" --include="*.cpp" --include="*.h" --include="*.go" \
    -iE '(password|passwd|secret|api_key|api_token|auth_token)\s*=\s*["\x27][^"\x27]{6,}' \
    "$APP_ROOT/src/" "$APP_ROOT/web/" 2>/dev/null | grep -vE 'password_hash|password_verify|input\[|post\[|\$input|config\.' | head -20)

if [ $FAILURES -eq 0 ]; then
    echo "  No hardcoded secrets detected in source."
    exit 0
else
    echo "  Found $FAILURES potential hardcoded secrets."
    exit 1
fi
