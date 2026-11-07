/* anytv_sha1.h - SHA-1 (required by the ADB auth signature scheme). */
#ifndef ANYTV_SHA1_H
#define ANYTV_SHA1_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint32_t h[5];
    uint64_t total;
    uint8_t buf[64];
    size_t buflen;
} sha1_ctx;

void sha1_init(sha1_ctx *c);
void sha1_update(sha1_ctx *c, const void *data, size_t len);
void sha1_final(sha1_ctx *c, uint8_t out[20]);
void sha1_digest(const void *data, size_t len, uint8_t out[20]);

#endif
