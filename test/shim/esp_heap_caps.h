/*
 * Native test shim - Heap Caps
 */
#pragma once

#include <stddef.h>

#define MALLOC_CAP_SPIRAM 0x100
#define MALLOC_CAP_8BIT 0x1
#define MALLOC_CAP_32BIT 0x2

size_t heap_caps_get_total_size(int caps);
size_t heap_caps_get_free_size(int caps);
