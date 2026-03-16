#!/bin/bash
# =============================================================================
# Mcaster1 Security Test Harness — Master Runner
# MCaster1 LLC / David St John <davestj@mcaster1.com>
# Created: 2026-03-22
#
# Runs all SAST and DAST security tests for a given application.
# Exit code 0 = all tests passed, non-zero = failures detected.
#
# Usage:
#   ./run-all.sh                    # Run all tests
#   ./run-all.sh --sast-only        # Static analysis only
#   ./run-all.sh --dast-only        # Dynamic analysis only
#   ./run-all.sh --json             # Output JSON results
#   ./run-all.sh --ci               # CI mode (non-interactive, exit code)
#
# Jenkins/GitHub Actions integration:
#   stage('Security') { sh './security/testharness/run-all.sh --ci' }
# =============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
APP_NAME="$(basename "$APP_ROOT")"
RESULTS_DIR="$SCRIPT_DIR/results"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
PASS=0
FAIL=0
WARN=0
SKIP=0
TOTAL=0
MODE="all"
FORMAT="text"

# Parse args
for arg in "$@"; do
    case $arg in
        --sast-only) MODE="sast" ;;
        --dast-only) MODE="dast" ;;
        --json)      FORMAT="json" ;;
        --ci)        FORMAT="ci" ;;
    esac
done

mkdir -p "$RESULTS_DIR"

# Colors (disabled in CI)
if [ "$FORMAT" = "ci" ]; then
    GREEN="" RED="" YELLOW="" CYAN="" NC=""
else
    GREEN='\033[0;32m' RED='\033[0;31m' YELLOW='\033[1;33m' CYAN='\033[0;36m' NC='\033[0m'
fi

log_pass() { ((PASS++)); ((TOTAL++)); echo -e "${GREEN}[PASS]${NC} $1"; }
log_fail() { ((FAIL++)); ((TOTAL++)); echo -e "${RED}[FAIL]${NC} $1"; }
log_warn() { ((WARN++)); ((TOTAL++)); echo -e "${YELLOW}[WARN]${NC} $1"; }
log_skip() { ((SKIP++)); ((TOTAL++)); echo -e "${CYAN}[SKIP]${NC} $1"; }
log_info() { echo -e "${CYAN}[INFO]${NC} $1"; }

echo "============================================================"
echo "  Mcaster1 Security Test Harness"
echo "  App: $APP_NAME"
echo "  Mode: $MODE | Format: $FORMAT"
echo "  Timestamp: $TIMESTAMP"
echo "============================================================"
echo ""

# ─── SAST Tests ─────────────────────────────────────────────────
if [ "$MODE" = "all" ] || [ "$MODE" = "sast" ]; then
    echo "── SAST (Static Analysis) ──────────────────────────────────"

    # Run individual SAST test scripts
    for test_script in "$SCRIPT_DIR"/sast-*.sh; do
        [ -f "$test_script" ] || continue
        test_name=$(basename "$test_script" .sh)
        log_info "Running $test_name..."
        if bash "$test_script" "$APP_ROOT" 2>&1; then
            log_pass "$test_name"
        else
            log_fail "$test_name"
        fi
    done
    echo ""
fi

# ─── DAST Tests ─────────────────────────────────────────────────
if [ "$MODE" = "all" ] || [ "$MODE" = "dast" ]; then
    echo "── DAST (Dynamic Analysis) ─────────────────────────────────"

    for test_script in "$SCRIPT_DIR"/dast-*.sh; do
        [ -f "$test_script" ] || continue
        test_name=$(basename "$test_script" .sh)
        log_info "Running $test_name..."
        if bash "$test_script" "$APP_ROOT" 2>&1; then
            log_pass "$test_name"
        else
            log_fail "$test_name"
        fi
    done
    echo ""
fi

# ─── Summary ────────────────────────────────────────────────────
echo "============================================================"
echo "  Results: $PASS passed, $FAIL failed, $WARN warnings, $SKIP skipped ($TOTAL total)"
echo "============================================================"

# Write results file
cat > "$RESULTS_DIR/run-${TIMESTAMP}.txt" << EOF
App: $APP_NAME
Timestamp: $TIMESTAMP
Mode: $MODE
Pass: $PASS
Fail: $FAIL
Warn: $WARN
Skip: $SKIP
Total: $TOTAL
Exit: $([ $FAIL -eq 0 ] && echo 0 || echo 1)
EOF

exit $([ $FAIL -eq 0 ] && echo 0 || echo 1)
