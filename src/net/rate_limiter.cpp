#include "rate_limiter.h"
#include <Arduino.h>

RateLimiter::RateLimiter(int ratePerMin, int burst)
    : ratePerMin(ratePerMin), burst(burst), curSize(0) {
    for (int i = 0; i < MAX_RATE_LIMIT_ENTRIES; i++) {
        entries[i].used = false;
        entries[i].ip = 0;
        entries[i].tokens = (uint32_t)(burst * TOKEN_SCALE);
        entries[i].lastMs = 0;
        entries[i].hits = 0;
    }
}

RateLimitEntry* RateLimiter::findOrCreate(uint32_t ip, uint32_t nowMs) {
    for (int i = 0; i < MAX_RATE_LIMIT_ENTRIES; i++) {
        if (entries[i].used && entries[i].ip == ip) {
            return &entries[i];
        }
    }
    for (int i = 0; i < MAX_RATE_LIMIT_ENTRIES; i++) {
        if (!entries[i].used) {
            entries[i].used = true;
            entries[i].ip = ip;
            entries[i].tokens = (uint32_t)(burst * TOKEN_SCALE);
            entries[i].lastMs = nowMs;
            entries[i].hits = 0;
            if (i >= curSize) curSize = i + 1;
            return &entries[i];
        }
    }
    uint32_t oldest = nowMs;
    int oldestIdx = 0;
    for (int i = 0; i < MAX_RATE_LIMIT_ENTRIES; i++) {
        if (entries[i].lastMs < oldest) {
            oldest = entries[i].lastMs;
            oldestIdx = i;
        }
    }
    entries[oldestIdx].ip = ip;
    entries[oldestIdx].tokens = (uint32_t)(burst * TOKEN_SCALE);
    entries[oldestIdx].lastMs = nowMs;
    entries[oldestIdx].hits = 0;
    return &entries[oldestIdx];
}

bool RateLimiter::allow(uint32_t ip) {
    uint32_t nowMs = millis();

    RateLimitEntry* e = findOrCreate(ip, nowMs);

    uint32_t elapsed = nowMs - e->lastMs;
    if (elapsed > 0) {
        uint32_t refilled = (elapsed * (uint32_t)ratePerMin * TOKEN_SCALE) / 60000;
        if (refilled > 0) {
            e->tokens += refilled;
            uint32_t maxTokens = (uint32_t)(burst * TOKEN_SCALE);
            if (e->tokens > maxTokens) e->tokens = maxTokens;
            e->lastMs = nowMs;
        }
    }

    for (int i = 0; i < MAX_RATE_LIMIT_ENTRIES; i++) {
        if (entries[i].used && (nowMs - entries[i].lastMs > RATE_LIMIT_TTL_MS)) {
            entries[i].used = false;
        }
    }

    if (e->tokens >= TOKEN_SCALE) {
        e->tokens -= TOKEN_SCALE;
        e->hits = 0;
        return true;
    }
    e->hits++;
    return false;
}

uint8_t RateLimiter::getHits(uint32_t ip) {
    for (int i = 0; i < MAX_RATE_LIMIT_ENTRIES; i++) {
        if (entries[i].used && entries[i].ip == ip) {
            return entries[i].hits;
        }
    }
    return 0;
}

RateLimiter g_otaRateLimiter(5, 10);
RateLimiter g_configRateLimiter(30, 60);
