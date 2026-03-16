#!/bin/bash
# DAST: Directory Listing / Sensitive File Access Check
# Verifies directories don't list contents and sensitive files are blocked
APP_ROOT="${1:-.}"
APP_NAME=$(basename "$APP_ROOT")
FAILURES=0

echo "  Checking directory listing and sensitive file access..."

case "$APP_NAME" in
    Mcaster1YPMan)      BASE="https://yp.casterclub.com:9689" ;;
    Mcaster1Chatter)    BASE="https://mcaster1.com:9560" ;;
    Mcaster1ADZMan)     BASE="https://mcaster1.com:9555" ;;
    Mcaster1DSPEncoder) BASE="https://127.0.0.1:8344" ;;
    mcaster1dnas)       BASE="https://dnas.mcaster1.com:9443" ;;
    Mcaster1StreamProxy) BASE="https://127.0.0.1:9877" ;;
    *)                  echo "    SKIP: Unknown app"; exit 0 ;;
esac

# Check directory listing is disabled
DIRS=("/uploads/" "/app/" "/app/inc/" "/app/api/" "/static/" "/config/" "/logs/")
for dir in "${DIRS[@]}"; do
    body=$(curl -sk --max-time 5 "${BASE}${dir}" 2>/dev/null)
    code=$(curl -sk -o /dev/null -w "%{http_code}" --max-time 5 "${BASE}${dir}" 2>/dev/null)
    if echo "$body" | grep -qiE '<title>Index of|Directory listing|Parent Directory'; then
        echo "    FAIL: Directory listing enabled: ${dir}"
        ((FAILURES++))
    fi
done

# Check sensitive files are blocked
SENSITIVE=("/.env" "/.git/config" "/CLAUDE.md" "/etc/config.yaml" "/web.config" "/.htaccess" "/composer.json")
for file in "${SENSITIVE[@]}"; do
    code=$(curl -sk -o /dev/null -w "%{http_code}" --max-time 5 "${BASE}${file}" 2>/dev/null)
    if [ "$code" = "200" ]; then
        echo "    WARN: Sensitive file accessible: ${file} (${code})"
        ((FAILURES++))
    fi
done

if [ $FAILURES -eq 0 ]; then
    echo "  No directory listing or sensitive file exposure."
    exit 0
else
    echo "  Found $FAILURES exposure issues."
    exit 1
fi
