#!/bin/bash
# SAST: SQL Injection Detection
# Finds raw string concatenation in SQL queries (PHP + C++)
APP_ROOT="${1:-.}"
FAILURES=0

echo "  Checking for SQL injection vulnerabilities..."

# PHP: Find string concat in queries (not using prepared statements)
# Pattern: "SELECT|INSERT|UPDATE|DELETE" . $var or "...{$var}..."
while IFS= read -r match; do
    # Skip comments and known-safe patterns
    echo "$match" | grep -qE '^\s*//' && continue
    echo "$match" | grep -qE '^\s*\*' && continue
    echo "$match" | grep -qE 'prepare\(' && continue
    echo "    FAIL: Potential SQL injection: $match"
    ((FAILURES++))
done < <(grep -rn --include="*.php" -E "(SELECT|INSERT|UPDATE|DELETE|WHERE).*\\\$[a-zA-Z_]" "$APP_ROOT/web/" 2>/dev/null | grep -vE 'scope_sql|where_sql|sort_sql|order_sql|fields\[\]|implode|db_rows|db_run|db_scalar|db_row|prepare' | head -20)

# C++: Find string concat in SQL (not using prepare/MYSQL_BIND)
while IFS= read -r match; do
    echo "$match" | grep -qE '^\s*//' && continue
    echo "$match" | grep -qE 'prepare' && continue
    echo "    FAIL: Potential C++ SQL injection: $match"
    ((FAILURES++))
done < <(grep -rn --include="*.cpp" -E '(SELECT|INSERT|UPDATE|DELETE).*\+\s*(std::to_string|v\.|entry\.)' "$APP_ROOT/src/" 2>/dev/null | head -20)

if [ $FAILURES -eq 0 ]; then
    echo "  No SQL injection patterns detected."
    exit 0
else
    echo "  Found $FAILURES potential SQL injection issues."
    exit 1
fi
