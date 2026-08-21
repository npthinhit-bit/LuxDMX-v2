#include "logger.h"
/* Scene engine stubs (spec 08) — not yet implemented, referenced by merge_engine.c */
static const char* TAG = "scene";

void sceneRecall(int idx, uint16_t fadeMs, int outIdx) {
    (void)idx; (void)fadeMs; (void)outIdx;
    LOG_WARN(TAG, "sceneRecall(%d) stub", idx);
}

void sceneRecallHome(int outIdx) {
    (void)outIdx;
    LOG_WARN(TAG, "sceneRecallHome() stub");
}
