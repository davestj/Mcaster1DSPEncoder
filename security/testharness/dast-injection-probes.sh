#!/bin/bash
# DAST: Injection Probe Tests
# Sends common injection payloads and verifies they're rejected/escaped
APP_ROOT="${1:-.}"
APP_NAME=$(basename "$APP_ROOT")
FAILURES=0

echo "  Running injection probe tests..."

case "$APP_NAME" in
    Mcaster1YPMan)      BASE="https://yp.casterclub.com:9689" ;;
    Mcaster1Chatter)    BASE="https://mcaster1.com:9560" ;;
    Mcaster1ADZMan)     BASE="https://mcaster1.com:9555" ;;
    Mcaster1DSPEncoder) BASE="https://127.0.0.1:8344" ;;
    mcaster1dnas)       BASE="https://dnas.mcaster1.com:9443" ;;
    Mcaster1StreamProxy) BASE="https://127.0.0.1:9877" ;;
    *)                  echo "    SKIP: Unknown app"; exit 0 ;;
esac

# SQL injection probes — should not return 200 with data or cause errors
SQL_PAYLOADS=("' OR '1'='1" "1; DROP TABLE users--" "' UNION SELECT 1,2,3--" "1' AND SLEEP(5)--")
for payload in "${SQL_PAYLOADS[@]}"; do
    encoded=$(python3 -c "import urllib.parse; print(urllib.parse.quote('$payload'))" 2>/dev/null || echo "$payload")
    code=$(curl -sk -o /dev/null -w "%{http_code}" --max-time 8 "${BASE}/api/health?id=${encoded}" 2>/dev/null)
    if [ "$code" = "500" ]; then
        echo "    FAIL: SQL injection probe caused 500: $payload"
        ((FAILURES++))
    fi
done

# XSS probes — response should not contain unescaped payload
XSS_PAYLOADS=("<script>alert(1)</script>" "<img onerror=alert(1) src=x>" "javascript:alert(1)")
for payload in "${XSS_PAYLOADS[@]}"; do
    encoded=$(python3 -c "import urllib.parse; print(urllib.parse.quote('$payload'))" 2>/dev/null || echo "$payload")
    body=$(curl -sk --max-time 5 "${BASE}/?q=${encoded}" 2>/dev/null)
    if echo "$body" | grep -qF "$payload"; then
        echo "    FAIL: XSS payload reflected unescaped: $payload"
        ((FAILURES++))
    fi
done

# Path traversal probes
TRAVERSAL_PAYLOADS=("../../../etc/passwd" "..%2f..%2f..%2fetc%2fpasswd" "....//....//etc/passwd")
for payload in "${TRAVERSAL_PAYLOADS[@]}"; do
    body=$(curl -sk --max-time 5 "${BASE}/${payload}" 2>/dev/null)
    if echo "$body" | grep -q "root:x:0:0"; then
        echo "    FAIL: Path traversal successful: $payload"
        ((FAILURES++))
    fi
done

if [ $FAILURES -eq 0 ]; then
    echo "  All injection probes handled safely."
    exit 0
else
    echo "  Found $FAILURES injection vulnerabilities."
    exit 1
fi
