#!/bin/bash
# DAST: HTTP Security Headers Check
# Tests live endpoints for security header presence
APP_ROOT="${1:-.}"
APP_NAME=$(basename "$APP_ROOT")
FAILURES=0

echo "  Checking HTTP security headers..."

# Determine the app's URL based on app name
case "$APP_NAME" in
    Mcaster1YPMan)       URL="https://yp.casterclub.com:9689" ;;
    Mcaster1Chatter)     URL="https://mcaster1.com:9560" ;;
    Mcaster1ADZMan)      URL="https://mcaster1.com:9555" ;;
    Mcaster1DSPEncoder)  URL="https://127.0.0.1:8344" ;;
    mcaster1dnas)        URL="https://dnas.mcaster1.com:9443" ;;
    Mcaster1StreamProxy) URL="https://127.0.0.1:9877" ;;
    *)                   echo "    SKIP: Unknown app $APP_NAME"; exit 0 ;;
esac

HEADERS=$(curl -sk -I "$URL/" 2>/dev/null)

if [ -z "$HEADERS" ]; then
    echo "    FAIL: Could not connect to $URL"
    exit 1
fi

# Check headers that SHOULD be present (safe headers per CLAUDE.md)
if echo "$HEADERS" | grep -qi "Strict-Transport-Security"; then
    echo "    OK: HSTS header present"
else
    echo "    INFO: HSTS header not set (acceptable for dev)"
fi

# Check headers that MUST NOT be present (stream-breaking per CLAUDE.md)
for bad_header in "Cross-Origin-Embedder-Policy" "Cross-Origin-Opener-Policy"; do
    if echo "$HEADERS" | grep -qi "$bad_header"; then
        echo "    FAIL: Stream-breaking header detected: $bad_header"
        ((FAILURES++))
    fi
done

# Check for server version disclosure
if echo "$HEADERS" | grep -qiE "^Server:.*/([\d]+)"; then
    echo "    WARN: Server version disclosed in headers"
    ((FAILURES++))
fi

# Check for information leakage
if echo "$HEADERS" | grep -qi "X-Powered-By"; then
    echo "    WARN: X-Powered-By header exposes technology stack"
    ((FAILURES++))
fi

if [ $FAILURES -eq 0 ]; then
    echo "  Headers OK."
    exit 0
else
    echo "  Found $FAILURES header issues."
    exit 1
fi
