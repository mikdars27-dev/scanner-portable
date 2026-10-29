/*
 * utils/crc.c — CRC32 implementation (ISO 3309, reflected polynomial).
 *
 * Toolchain note: C++ (Arduino). Compile as C++.
 */

#include "utils/crc.h"

#include <stdint.h>
#include <stddef.h>

/* Reflected CRC-32 table, polynomial 0xEDB88320. Built lazily on first use. */
static uint32_t crc32_table[256];
static bool     crc32_table_init = false;

static void crc32_build_table(void)
{
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (uint32_t k = 0; k < 8; k++) {
            c = (c & 1U) ? (0xEDB88320UL ^ (c >> 1)) : (c >> 1);
        }
        crc32_table[i] = c;
    }
    crc32_table_init = true;
}

uint32_t crc32_compute(const uint8_t *data, size_t len, uint32_t crc)
{
    if (!crc32_table_init) {
        crc32_build_table();
    }

    crc = ~crc; /* running CRC is complemented */
    for (size_t i = 0; i < len; i++) {
        crc = crc32_table[(crc ^ data[i]) & 0xFFU] ^ (crc >> 8);
    }
    return ~crc; /* final complement */
}
