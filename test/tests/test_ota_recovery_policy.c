#include "ota_recovery_policy.h"

#include <assert.h>
#include <stdio.h>

static void test_allows_three_pending_boots(void)
{
    uint8_t next = 0;
    assert(!otaRecoveryShouldRollback(0, &next));
    assert(next == 1);
    assert(!otaRecoveryShouldRollback(1, &next));
    assert(next == 2);
    assert(!otaRecoveryShouldRollback(2, &next));
    assert(next == 3);
}

static void test_rolls_back_at_cap(void)
{
    uint8_t next = 0;
    assert(otaRecoveryShouldRollback(OTA_BOOT_RETRY_MAX, &next));
    assert(next == OTA_BOOT_RETRY_MAX);
    assert(otaRecoveryShouldRollback(255, &next));
    assert(next == 255);
}

static void test_null_output_is_safe(void)
{
    assert(!otaRecoveryShouldRollback(0, NULL));
    assert(!otaRecoveryShouldRollback(OTA_BOOT_RETRY_MAX - 1u, NULL));
    assert(otaRecoveryShouldRollback(OTA_BOOT_RETRY_MAX, NULL));
}

int main(void)
{
    test_allows_three_pending_boots();
    test_rolls_back_at_cap();
    test_null_output_is_safe();
    puts("ota_recovery_policy_test: 3 tests passed");
    return 0;
}
