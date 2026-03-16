#!/bin/bash
# DAST: Authentication Enforcement Check
# Verifies protected endpoints reject unauthenticated requests
APP_ROOT="${1:-.}"
APP_NAME=$(basename "$APP_ROOT")
FAILURES=0

echo "  Checking authentication enforcement..."

case "$APP_NAME" in
    Mcaster1YPMan)
        BASE="https://yp.casterclub.com:9689"
        # These should require auth (401/403)
        PROTECTED=("/api/status" "/api/stations" "/api/users" "/api/sync/trigger" "/api/sources")
        # These should be public (200)
        PUBLIC=("/api/health")
        ;;
    Mcaster1Chatter)
        BASE="https://mcaster1.com:9560"
        PROTECTED=("/app/api/admin/index.php?action=get_stats" "/app/api/messages/index.php?action=list" "/app/api/users/index.php?action=online")
        PUBLIC=("/login.php")
        ;;
    Mcaster1ADZMan)
        BASE="https://mcaster1.com:9555"
        PROTECTED=("/app/api/campaigns.php" "/app/api/users.php")
        PUBLIC=("/login.php")
        ;;
    Mcaster1StreamProxy)
        BASE="https://127.0.0.1:9877"
        PROTECTED=("/stream?id=1")
        PUBLIC=("/health")
        ;;
    *)
        echo "    SKIP: No auth tests for $APP_NAME"
        exit 0
        ;;
esac

# Test protected endpoints reject unauthenticated requests
for ep in "${PROTECTED[@]}"; do
    code=$(curl -sk -o /dev/null -w "%{http_code}" "${BASE}${ep}" 2>/dev/null)
    if [ "$code" = "200" ]; then
        echo "    FAIL: $ep returned 200 without auth (should be 401/403)"
        ((FAILURES++))
    else
        echo "    OK: $ep returned $code (auth required)"
    fi
done

# Test public endpoints are accessible
for ep in "${PUBLIC[@]}"; do
    code=$(curl -sk -o /dev/null -w "%{http_code}" "${BASE}${ep}" 2>/dev/null)
    if [ "$code" = "200" ] || [ "$code" = "302" ]; then
        echo "    OK: $ep returned $code (public)"
    else
        echo "    WARN: Public endpoint $ep returned $code"
    fi
done

if [ $FAILURES -eq 0 ]; then
    echo "  Auth enforcement OK."
    exit 0
else
    echo "  Found $FAILURES auth bypass issues."
    exit 1
fi
