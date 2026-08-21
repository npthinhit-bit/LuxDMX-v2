#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* 32-bit ticket seqlock: odd = writer mid-update, even = stable.
   Spec 03 §4.4: snapshot retries up to 8 times.
   Spec 45 §11: memory barrier at every read and write. */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    volatile uint32_t seq;
} SeqLock;

/* Writer: begin a write (bump to odd + barrier) */
static inline void seqlock_write_begin(SeqLock* lock) {
    __sync_synchronize();
    lock->seq = (lock->seq + 1) | 1;
    __sync_synchronize();
}

/* Writer: end a write (barrier + bump to even) */
static inline void seqlock_write_end(SeqLock* lock) {
    __sync_synchronize();
    lock->seq = (lock->seq + 1) & ~1u;
    __sync_synchronize();
}

/* Reader: attempt a snapshot. Returns true on success (consistent copy).
   Out buffer must be >= len bytes. Retries up to 8 times per spec 03 §8.3. */
static inline bool seqlock_snapshot(SeqLock* lock, void* out, const void* src, size_t len) {
    for (int retry = 0; retry < 8; retry++) {
        uint32_t s1 = lock->seq;
        __sync_synchronize();
        if (s1 & 1u) continue;  /* writer mid-update, retry */
        memcpy(out, src, len);
        __sync_synchronize();
        uint32_t s2 = lock->seq;
        if (s1 == s2 && !(s1 & 1u)) return true;  /* stable snapshot */
    }
    return false;  /* torn read after 8 retries */
}

#ifdef __cplusplus
}
#endif
