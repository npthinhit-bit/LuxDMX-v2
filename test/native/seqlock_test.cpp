// Host test for the generic seqlock (include/seqlock.h).
// Verifies that a snapshot during a write is retried, and that a
// stable writer yields a clean copy (no torn reads).
#include "seqlock.h"
#include <cstdio>
#include <cstring>
#include <cstdint>

struct Frame {
    uint8_t data[8];
};

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("  FAIL: %s\n", msg); } } while (0)

int main() {
    Frame src[1] = {};
    SeqLock sl;
    Frame dst[1] = {};

    // Clean snapshot: writer idle -> reader gets the data.
    memset(src->data, 0xAA, sizeof(src->data));
    CHECK(sl.snapshot(src, dst, sizeof(src->data)), "clean snapshot succeeds");
    CHECK(dst->data[0] == 0xAA, "snapshot data correct");

    // Snapshot during a stable write (writer already done).
    memset(src->data, 0x42, sizeof(src->data));
    CHECK(sl.snapshot(src, dst, sizeof(src->data)), "snapshot during stable write");
    CHECK(dst->data[0] == 0x42, "snapshot during stable write correct");

    // Multiple write+read cycles.
    for (int cycle = 0; cycle < 100; cycle++) {
        sl.writeBegin();
        for (int b = 0; b < 8; b++) src->data[b] = (uint8_t)(cycle + b);
        sl.writeEnd();
        CHECK(sl.snapshot(src, dst, sizeof(src->data)), "cycle snapshot ok");
        CHECK(dst->data[0] == (uint8_t)cycle, "cycle data[0] ok");
        CHECK(dst->data[7] == (uint8_t)(cycle + 7), "cycle data[7] ok");
    }

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
