/*
 * Test: Logger ring buffer
 * Verifies that log entries are correctly stored in the ring buffer
 * and that the buffer wraps around correctly.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "shim.h"
#include "logger.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    static void test_##name(void); \
    static void run_##name(void) { \
        tests_run++; \
        printf("  [RUN ] %s\n", "test_" #name); \
        test_##name(); \
        tests_passed++; \
        printf("  [PASS] %s\n", "test_" #name); \
    } \
    static void test_##name(void)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("  [FAIL] %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        tests_passed--; \
        return; \
    } \
} while(0)

TEST(ring_buffer_initial_state) {
    logger_init();
    size_t count;
    const log_entry_t* entries = logger_get_ring_buffer(&count);
    ASSERT(entries != NULL);
    ASSERT(count == 0);
}

TEST(ring_buffer_single_entry) {
    logger_init();
    LOG_INFO("test", "Hello World");
    size_t count;
    const log_entry_t* entries = logger_get_ring_buffer(&count);
    ASSERT(count == 1);
    ASSERT(strcmp(entries[0].tag, "test") == 0);
    ASSERT(strcmp(entries[0].message, "Hello World") == 0);
    ASSERT(entries[0].level == LOG_LEVEL_INFO);
}

TEST(ring_buffer_wraps_at_capacity) {
    logger_init();
    size_t count;

    /* Fill beyond capacity */
    for (int i = 0; i < LOG_RING_CAPACITY + 10; i++) {
        LOG_INFO("wrap", "Entry %d", i);
    }

    const log_entry_t* entries = logger_get_ring_buffer(&count);
    ASSERT(count == LOG_RING_CAPACITY);
    /* Oldest entry should be the one that overwrote the oldest */
    ASSERT(strcmp(entries[0].tag, "wrap") == 0);
    /* The message should be "Entry 10" (first 10 were overwritten) */
    ASSERT(strstr(entries[0].message, "Entry 10") != NULL);
}

TEST(ring_buffer_preserves_order) {
    logger_init();
    for (int i = 0; i < 5; i++) {
        LOG_INFO("order", "Msg %d", i);
    }
    size_t count;
    const log_entry_t* entries = logger_get_ring_buffer(&count);
    ASSERT(count == 5);
    for (size_t i = 0; i < count; i++) {
        char expected[64];
        snprintf(expected, sizeof(expected), "Msg %d", (int)i);
        ASSERT(strcmp(entries[i].message, expected) == 0);
    }
}

TEST(ring_buffer_multiple_tags) {
    logger_init();
    LOG_ERROR("tag1", "First error");
    LOG_WARN("tag2", "Second warning");
    LOG_INFO("tag3", "Third info");
    size_t count;
    const log_entry_t* entries = logger_get_ring_buffer(&count);
    ASSERT(count == 3);
    ASSERT(entries[0].level == LOG_LEVEL_ERROR);
    ASSERT(entries[1].level == LOG_LEVEL_WARN);
    ASSERT(entries[2].level == LOG_LEVEL_INFO);
}

int main(void) {
    printf("=== Logger Ring Buffer Tests ===\n\n");

    run_ring_buffer_initial_state();
    run_ring_buffer_single_entry();
    run_ring_buffer_wraps_at_capacity();
    run_ring_buffer_preserves_order();
    run_ring_buffer_multiple_tags();

    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
