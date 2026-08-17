#pragma once
#include <stddef.h>
#include <stdint.h>

// IP-based token-bucket rate limiter for HTTP endpoints.
// Tracks per-client-IP buckets with a fixed-size table (no heap allocation).
// All methods are called from the AsyncWebServer task on core 0.
// Expired entries are lazily evicted during allow().

struct RateLimitEntry
{
    uint32_t ip;      // client IP (network byte order)
    uint32_t tokens;  // current token count (scaled by TOKEN_SCALE)
    uint32_t lastMs;  // last refill timestamp (ms)
    uint8_t  hits;    // consecutive over-limit count (for diagnostics)
    bool     used;    // entry is active
};

static constexpr int      TOKEN_SCALE            = 100;  // sub-token precision
static constexpr int      MAX_RATE_LIMIT_ENTRIES = 32;
static constexpr uint32_t RATE_LIMIT_TTL_MS      = 300000;  // 5 min expiry

class RateLimiter
{
public:
    RateLimiter(int ratePerMin, int burst);

    // Returns true if the request from this IP should be allowed (token available).
    // Refills tokens based on elapsed time. Evicts expired entries lazily.
    bool allow(uint32_t ip);

    // Returns the consecutive-hit count for diagnostics (0 if allowed, >0 if blocked).
    uint8_t getHits(uint32_t ip);

private:
    RateLimitEntry entries[MAX_RATE_LIMIT_ENTRIES];
    int            ratePerMin;  // tokens added per minute
    int            burst;       // max tokens (burst capacity)
    int            curSize;

    RateLimitEntry* findOrCreate(uint32_t ip, uint32_t nowMs);
};

// Global instances — configured in rate_limiter.cpp.
extern RateLimiter g_otaRateLimiter;     // 5 req/min, burst 10
extern RateLimiter g_configRateLimiter;  // 30 req/min, burst 60
