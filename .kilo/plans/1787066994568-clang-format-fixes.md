# Clang-Format Violations Fix Plan

## Summary
The CI Static Analysis job failed due to clang-format violations in 6 source files. This plan details the fixes needed to pass `clang-format --dry-run --Werror`.

## Files with Violations

| File | Lines | Issue Types |
|------|-------|-------------|
| `src/frontend/web_frontend.cpp` | 31-90 | Lambda formatting, ternary operator, auto/shared_ptr spacing |
| `src/sys/firmware_version.cpp` | 31-32 | Long line breaking in Serial.printf |
| `test/embedded_tests/test_suite/test_config.cpp` | 3 | Include brace spacing |
| `test/embedded_tests/test_suite/test_dmx_rmt.cpp` | 13-94 | Assignment spacing, ternary, TEST_ASSERT formatting |
| `test/embedded_tests/test_suite/test_integration.cpp` | 4, 62-63, 159-160 | Include spacing, function brace style |
| `test/embedded_tests/test_suite/test_rdm_disc.cpp` | 18-19 | Initialization spacing |
| `test/embedded_tests/test_suite/test_rdm_transport.cpp` | 14-110 | Initialization spacing, brace style, TEST_ASSERT formatting |

## Fix Strategy

### 1. Apply clang-format directly (Recommended)
Run clang-format on all affected files to auto-fix:
```bash
clang-format -i --style=file src/frontend/web_frontend.cpp
clang-format -i --style=file src/sys/firmware_version.cpp
clang-format -i --style=file test/embedded_tests/test_suite/test_config.cpp
clang-format -i --style=file test/embedded_tests/test_suite/test_dmx_rmt.cpp
clang-format -i --style=file test/embedded_tests/test_suite/test_integration.cpp
clang-format -i --style=file test/embedded_tests/test_suite/test_rdm_disc.cpp
clang-format -i --style=file test/embedded_tests/test_suite/test_rdm_transport.cpp
```

### 2. Manual fixes if needed
If auto-format doesn't resolve all issues, manual edits needed:

**web_frontend.cpp** (lines 31, 64-71, 79, 84-91):
- Fix spacing in `auto sp = std::make_shared<String>();`
- Fix lambda capture and parameter spacing
- Fix ternary operator formatting

**firmware_version.cpp** (lines 31-32):
- Break long Serial.printf line at 120 column limit

**Test files**:
- Fix include directive spacing (`#include "header.h"`)
- Fix brace style for empty functions (Allman style - brace on new line)
- Fix TEST_ASSERT macro argument spacing

## Validation
After fixes, verify:
```bash
# From repo root
find src include test -name '*.cpp' -o -name '*.h' -o -name '*.hpp' | xargs clang-format --style=file --dry-run --Werror
```

## Out of Scope
- `.clang-format` config changes (current config is intentional)
- Generated files in `src/generated/` (excluded from CI check)
- Native test files in `test/native/` (not checked in CI)

## Next Step
Execute the clang-format commands above, then verify with the validation command.