/* anytv_bn.h - minimal big integer arithmetic for RSA-2048, no external deps. */
#ifndef ANYTV_BN_H
#define ANYTV_BN_H

#include <stdint.h>
#include <stddef.h>

#define BN_MAX_LIMBS 128 /* 4096 bits: room for n*m and phi */

typedef struct {
    uint32_t d[BN_MAX_LIMBS];
    int n; /* used limbs, d[0] is least significant */
} bn;

void bn_zero(bn *a);
void bn_from_u32(bn *a, uint32_t v);
int  bn_is_zero(const bn *a);
int  bn_is_odd(const bn *a);
int  bn_cmp(const bn *a, const bn *b);           /* <0, 0, >0 */
void bn_copy(bn *dst, const bn *src);
void bn_trim(bn *a);
int  bn_bit(const bn *a, int i);
void bn_set_bit(bn *a, int i);

void bn_add(bn *r, const bn *a, const bn *b);
void bn_sub(bn *r, const bn *a, const bn *b);     /* requires a >= b */
void bn_lshift1(bn *a);
void bn_rshift1(bn *a);
void bn_mul(bn *r, const bn *a, const bn *b);     /* schoolbook */
void bn_mul_small(bn *r, const bn *a, uint32_t m);
uint32_t bn_mod_small(const bn *a, uint32_t m);
uint32_t bn_divmod_small(bn *q, const bn *a, uint32_t m);
int  bn_bitlen(const bn *a);

void bn_from_bytes_be(bn *a, const uint8_t *buf, size_t len);
void bn_to_bytes_be(const bn *a, uint8_t *buf, size_t len);

/* Montgomery arithmetic, modulus odd */
typedef struct {
    bn n;
    uint32_t n0inv;   /* -n^-1 mod 2^32 */
    bn rr;            /* R^2 mod n, R = 2^(32*limbs) */
    int limbs;
} mont;

int  mont_init(mont *m, const bn *modulus);
void mont_mul(const mont *m, const bn *a, const bn *b, bn *out);
void mont_modexp(const mont *m, const bn *base, const bn *exp, bn *out);
int  bn_mod_exp(bn *out, const bn *base, const bn *exp, const bn *mod);

/* random bytes from the platform entropy source */
int  bn_random_bytes(uint8_t *buf, size_t len);

#endif
