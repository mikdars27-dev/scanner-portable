#ifndef UTILS_CRC_H
#define UTILS_CRC_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * CRC32 (ISO 3309, polynomial 0xEDB88320 reflected).
 *
 * Used for integrity verification of ECU backup images and firmware update
 * payloads before they are written to the ECU / flashed. A zero init value
 * with final XOR 0xFFFFFFFF is the standard CRC-32 used by ZIP, PNG, etc.
 * ========================================================================= */

/* Compute CRC32 over `len` bytes starting at `data`, optionally continuing
 * from a previous `crc` value (for streaming). Returns the running CRC. */
uint32_t crc32_compute(const uint8_t *data, size_t len, uint32_t crc);

/* Convenience: compute CRC32 from scratch (init = 0, final XOR applied). */
static inline uint32_t crc32(const uint8_t *data, size_t len)
{
    return crc32_compute(data, len, 0UL);
}

#ifdef __cplusplus
}
#endif

#endif /* UTILS_CRC_H */
