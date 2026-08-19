#include "common.h"
#include <string.h>
#include <stdlib.h>

// Delay for specified milliseconds
void lux_delay_ms(uint32_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
}

// Hex dump utility
void lux_hexdump(const void *mem, uint32_t len, uint8_t cols) {
    const uint8_t *src = (const uint8_t *)mem;
    printf("\n[HEXDUMP] Address: 0x%08X len: 0x%X (%u)", (unsigned int)(ptrdiff_t)src, (unsigned int)len, (unsigned int)len);

    for (uint32_t i = 0; i < len; i++) {
        if (i % cols == 0) {
            printf("\n[0x%08X] 0x%08X: ", (unsigned int)(ptrdiff_t)src, (unsigned int)i);
        }
        printf("%02X ", *src);
        src++;
    }
    printf("\n");
}

// String duplication utility
char* lux_strdup(const char* str) {
    if (str == NULL) {
        return NULL;
    }

    size_t len = strlen(str) + 1;
    char* copy = malloc(len);
    if (copy) {
        memcpy(copy, str, len);
    }
    return copy;
}