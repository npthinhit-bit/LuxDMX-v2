#!/bin/bash
# Run all native host tests for LuxDMX-v2.
# Must be run from the LuxDMX-v2/ directory (the v2 project root).
set -e
FAILURES=0
for t in config_test seqlock_test merge_test rdm_types_test; do
    echo ""
    echo "=== $t ==="
    if python3 build/test_native.py "$t"; then
        echo "$t: PASS"
    else
        echo "$t: FAIL"
        FAILURES=$((FAILURES + 1))
    fi
done
echo ""
if [ "$FAILURES" -eq 0 ]; then
    echo "ALL TESTS PASSED"
else
    echo "$FAILURES TEST SUITE(S) FAILED"
    exit 1
fi
