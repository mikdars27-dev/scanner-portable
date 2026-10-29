/*
 * utils/hmac.c — HMAC-SHA256 implementation (RFC 4231, FIPS 180-4).
 *
 * Digunakan untuk verifikasi tanda tangan (signature) sebelum ECU write /
 * flash, sebagaimana dijanjikan MASTER_PLAN.md Bab 9 dan
 * STRUKTUR_PROYEK_FIRMWARE.md Section 3.7.
 *
 * Toolchain note: C++ (Arduino). Compile as C++ (-x c++).
 */

#include "utils/hmac.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* =========================================================================
 * SHA-256 core (FIPS 180-4).
 * ========================================================================= */

#define SHA256_BLOCK_SIZE 64
#define SHA256_DIGEST_SIZE 32

/* First 32 bits of the fractional parts of the cube roots of the first 64
 * primes. */
static const uint32_t sha256_k[64] = {
    0x428a2f98UL, 0x71374491UL, 0xb5c0fbcfUL, 0xe9b5dba5UL,
    0x3956c25bUL, 0x59f111f1UL, 0x923f82a4UL, 0xab1c5ed5UL,
    0xd807aa98UL, 0x12835b01UL, 0x243185beUL, 0x550c7dc3UL,
    0x72be5d74UL, 0x80deb1feUL, 0x9bdc06a7UL, 0xc19bf174UL,
    0xe49b69c1UL, 0xefbe4786UL, 0x0fc19dc6UL, 0x240ca1ccUL,
    0x2de92c6fUL, 0x4a7484aaUL, 0x5cb0a9dcUL, 0x76f988daUL,
    0x983e5152UL, 0xa831c66dUL, 0xb00327c8UL, 0xbf597fc7UL,
    0xc6e00bf3UL, 0xd5a79147UL, 0x06ca6351UL, 0x14292967UL,
    0x27b70a85UL, 0x2e1b2138UL, 0x4d2c6dfcUL, 0x53380d13UL,
    0x650a7354UL, 0x766a0abbUL, 0x81c2c92eUL, 0x92722c85UL,
    0xa2bfe8a1UL, 0xa81a664bUL, 0xc24b8b70UL, 0xc76c51a3UL,
    0xd192e819UL, 0xd6990624UL, 0xf40e3585UL, 0x106aa070UL,
    0x19a4c116UL, 0x1e376c08UL, 0x2748774cUL, 0x34b0bcb5UL,
    0x391c0cb3UL, 0x4ed8aa4aUL, 0x5b9cca4fUL, 0x682e6ff3UL,
    0x748f82eeUL, 0x78a5636fUL, 0x84c87814UL, 0x8cc70208UL,
    0x90befffaUL, 0xa4506cebUL, 0xbef9a3f7UL, 0xc67178f2UL
};

/* First 32 bits of the fractional parts of the square roots of the first 8
 * primes (initial hash values). */
static const uint32_t sha256_h0[8] = {
    0x6a09e667UL, 0xbb67ae85UL, 0x3c6ef372UL, 0xa54ff53aUL,
    0x510e527fUL, 0x9b05688cUL, 0x1f83d9abUL, 0x5be0cd19UL
};

static uint32_t sha256_rotr(uint32_t x, uint32_t n)
{
    return (x >> n) | (x << (32 - n));
}

/* Process one 512-bit block. */
static void sha256_transform(uint32_t state[8], const uint8_t block[64])
{
    uint32_t w[64];

    for (int i = 0; i < 16; i++) {
        w[i] = ((uint32_t)block[i * 4 + 0] << 24) |
               ((uint32_t)block[i * 4 + 1] << 16) |
               ((uint32_t)block[i * 4 + 2] << 8)  |
               ((uint32_t)block[i * 4 + 3] << 0);
    }
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = sha256_rotr(w[i - 15], 7)  ^ sha256_rotr(w[i - 15], 18) ^
                      (w[i - 15] >> 3);
        uint32_t s1 = sha256_rotr(w[i - 2], 17) ^ sha256_rotr(w[i - 2], 19) ^
                      (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];

    for (int i = 0; i < 64; i++) {
        uint32_t S1 = sha256_rotr(e, 6) ^ sha256_rotr(e, 11) ^ sha256_rotr(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + S1 + ch + sha256_k[i] + w[i];
        uint32_t S0 = sha256_rotr(a, 2) ^ sha256_rotr(a, 13) ^ sha256_rotr(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = S0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

typedef struct {
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t  buffer[64];
    size_t   buffer_len;
} sha256_ctx_t;

static void sha256_init(sha256_ctx_t *ctx)
{
    for (int i = 0; i < 8; i++) {
        ctx->state[i] = sha256_h0[i];
    }
    ctx->bitlen = 0;
    ctx->buffer_len = 0;
}

static void sha256_update(sha256_ctx_t *ctx, const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        ctx->buffer[ctx->buffer_len++] = data[i];
        if (ctx->buffer_len == SHA256_BLOCK_SIZE) {
            sha256_transform(ctx->state, ctx->buffer);
            ctx->bitlen += SHA256_BLOCK_SIZE * 8;
            ctx->buffer_len = 0;
        }
    }
}

static void sha256_final(sha256_ctx_t *ctx, uint8_t digest[32])
{
    ctx->bitlen += ctx->buffer_len * 8;

    /* Pad: 0x80 then zeros until 56 mod 64, then 8-byte big-endian bitlen. */
    ctx->buffer[ctx->buffer_len++] = 0x80;
    if (ctx->buffer_len > 56) {
        while (ctx->buffer_len < 64) {
            ctx->buffer[ctx->buffer_len++] = 0x00;
        }
        sha256_transform(ctx->state, ctx->buffer);
        ctx->buffer_len = 0;
    }
    while (ctx->buffer_len < 56) {
        ctx->buffer[ctx->buffer_len++] = 0x00;
    }
    for (int i = 0; i < 8; i++) {
        ctx->buffer[56 + i] = (uint8_t)((ctx->bitlen >> (56 - i * 8)) & 0xFF);
    }
    sha256_transform(ctx->state, ctx->buffer);

    for (int i = 0; i < 8; i++) {
        digest[i * 4 + 0] = (uint8_t)((ctx->state[i] >> 24) & 0xFF);
        digest[i * 4 + 1] = (uint8_t)((ctx->state[i] >> 16) & 0xFF);
        digest[i * 4 + 2] = (uint8_t)((ctx->state[i] >> 8)  & 0xFF);
        digest[i * 4 + 3] = (uint8_t)((ctx->state[i] >> 0)  & 0xFF);
    }
}

/* =========================================================================
 * HMAC-SHA256 (RFC 4231).
 * ========================================================================= */

#define HMAC_BLOCK_SIZE 64  /* SHA-256 block size in bytes */

int hmac_sha256(const uint8_t *key, size_t key_len,
                const uint8_t *data, size_t data_len,
                uint8_t *out)
{
    if (key == NULL || data == NULL || out == NULL) {
        return -1;
    }

    uint8_t k[HMAC_BLOCK_SIZE];
    memset(k, 0, sizeof(k));

    /* If key longer than block size, hash it first. */
    if (key_len > HMAC_BLOCK_SIZE) {
        uint8_t key_hash[SHA256_DIGEST_SIZE];
        sha256_ctx_t ctx;
        sha256_init(&ctx);
        sha256_update(&ctx, key, key_len);
        sha256_final(&ctx, key_hash);
        memcpy(k, key_hash, SHA256_DIGEST_SIZE);
    } else {
        memcpy(k, key, key_len);
    }

    /* Inner pad: key XOR 0x36. */
    uint8_t ipad[HMAC_BLOCK_SIZE];
    for (size_t i = 0; i < HMAC_BLOCK_SIZE; i++) {
        ipad[i] = k[i] ^ 0x36U;
    }

    /* H((key XOR ipad) || message). */
    uint8_t inner_input[HMAC_BLOCK_SIZE + 1024]; /* stack, bounded by caller */
    /* Note: data_len may exceed 1024; use streaming instead for large data. */
    if (data_len > 1024 - HMAC_BLOCK_SIZE) {
        /* Streaming path for large messages. */
        sha256_ctx_t ctx;
        sha256_init(&ctx);
        sha256_update(&ctx, ipad, HMAC_BLOCK_SIZE);
        sha256_update(&ctx, data, data_len);
        uint8_t inner_digest[SHA256_DIGEST_SIZE];
        sha256_final(&ctx, inner_digest);

        /* Outer pad: key XOR 0x5c. */
        uint8_t opad[HMAC_BLOCK_SIZE];
        for (size_t i = 0; i < HMAC_BLOCK_SIZE; i++) {
            opad[i] = k[i] ^ 0x5cU;
        }

        sha256_init(&ctx);
        sha256_update(&ctx, opad, HMAC_BLOCK_SIZE);
        sha256_update(&ctx, inner_digest, SHA256_DIGEST_SIZE);
        sha256_final(&ctx, out);
        return 0;
    }

    /* Small message: single buffer. */
    size_t inner_len = HMAC_BLOCK_SIZE + data_len;
    uint8_t *inner_buf = (uint8_t *)alloca(inner_len);
    if (inner_buf == NULL) {
        return -1;
    }
    memcpy(inner_buf, ipad, HMAC_BLOCK_SIZE);
    memcpy(inner_buf + HMAC_BLOCK_SIZE, data, data_len);

    uint8_t inner_digest[SHA256_DIGEST_SIZE];
    sha256_ctx_t ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, inner_buf, inner_len);
    sha256_final(&ctx, inner_digest);

    /* Outer pad: key XOR 0x5c. */
    uint8_t opad[HMAC_BLOCK_SIZE];
    for (size_t i = 0; i < HMAC_BLOCK_SIZE; i++) {
        opad[i] = k[i] ^ 0x5cU;
    }

    uint8_t outer_input[HMAC_BLOCK_SIZE + SHA256_DIGEST_SIZE];
    memcpy(outer_input, opad, HMAC_BLOCK_SIZE);
    memcpy(outer_input + HMAC_BLOCK_SIZE, inner_digest, SHA256_DIGEST_SIZE);

    sha256_init(&ctx);
    sha256_update(&ctx, outer_input, sizeof(outer_input));
    sha256_final(&ctx, out);
    return 0;
}

int hmac_sha256_verify(const uint8_t *key, size_t key_len,
                       const uint8_t *data, size_t data_len,
                       const uint8_t *sig)
{
    if (key == NULL || data == NULL || sig == NULL) {
        return -1;
    }

    uint8_t computed[SHA256_DIGEST_SIZE];
    int rc = hmac_sha256(key, key_len, data, data_len, computed);
    if (rc != 0) {
        return rc;
    }

    /* Constant-time comparison. */
    uint8_t diff = 0;
    for (int i = 0; i < SHA256_DIGEST_SIZE; i++) {
        diff |= computed[i] ^ sig[i];
    }
    return (diff == 0) ? 1 : 0;
}
