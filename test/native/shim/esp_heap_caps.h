#pragma once
// Minimal shim for ESP-IDF heap caps API — unused in host tests.
inline void* heap_caps_malloc(size_t sz, int cap)
{
    return malloc(sz);
}
inline void* heap_caps_calloc(size_t n, size_t sz, int cap)
{
    return calloc(n * sz, 1);
}
inline void heap_caps_free(void* p)
{
    free(p);
}
#define MALLOC_CAP_8BIT 1
#define MALLOC_CAP_SPIRAM 2
