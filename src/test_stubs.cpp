#ifdef UNIT_TESTING

#include "alert.h"
#include "scene_engine.h"

void alertSourceLost(int outIdx, const char* sourceIp)
{
    (void)outIdx;
    (void)sourceIp;
}
void alertSourceRestored(int outIdx)
{
    (void)outIdx;
}
void sceneRecall(int presetIdx, uint16_t fadeMs, int outIdx)
{
    (void)presetIdx;
    (void)fadeMs;
    (void)outIdx;
}
void sceneRecallHome(int outIdx)
{
    (void)outIdx;
}

#endif