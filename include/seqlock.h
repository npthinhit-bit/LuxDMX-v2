#pragma once
#include <Arduino.h>
#include <string.h>

// Generic single-writer / single-reader seqlock (issue #64).
//
// Writer wraps mutations in writeBegin()/writeEnd(); each bumps a 32-bit
// ticket (odd = mid-write). Reader takes a memcpy and re-reads the ticket,
// retrying if it moved or was odd. Nothing blocks, no interrupts are
// disabled, and the writer is never delayed by the reader.
struct SeqLock {
    volatile uint32_t seq = 0;

    inline void writeBegin() { seq++; __sync_synchronize(); }
    inline void writeEnd()   { __sync_synchronize(); seq++; }

    // Take a consistent snapshot of `n` bytes from `src` into `dst`.
    // Returns false if the writer kept winning the race (8 retries).
    // The caller must NOT transmit on failure: holding the previous
    // frame one more tick is always safe; sending a torn one never is.
    inline bool snapshot(const volatile void* src, void* dst, size_t n) const {
        for (int tries = 0; tries < 8; tries++) {
            uint32_t s1 = seq;
            if (s1 & 1u) continue;                       // writer is inside the buffer
            __sync_synchronize();
            memcpy(dst, (const void*)src, n);
            __sync_synchronize();
            if (seq == s1) return true;                  // consistent copy
        }
        return false;
    }
};
