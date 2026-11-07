/* anytv_bn.c - big integer + Montgomery arithmetic, portable C99 (iOS + Linux). */
#ifdef ANYTV_HOST_LINUX
#define _GNU_SOURCE
#endif
#include "anytv_bn.h"
#include <string.h>
#include <stdio.h>

#ifdef ANYTV_HOST_LINUX
#include <fcntl.h>
#include <unistd.h>
#include <sys/random.h>
#endif

void bn_zero(bn *a) { memset(a->d, 0, sizeof(a->d)); a->n = 0; }

void bn_from_u32(bn *a, uint32_t v) { bn_zero(a); a->d[0] = v; a->n = v ? 1 : 0; }

void bn_trim(bn *a) { int n = a->n; while (n > 0 && a->d[n - 1] == 0) n--; a->n = n; }

int bn_is_zero(const bn *a) { return a->n == 0; }
int bn_is_odd(const bn *a) { return a->n > 0 && (a->d[0] & 1u); }

void bn_copy(bn *dst, const bn *src) {
    if (dst == src) return;
    memcpy(dst->d, src->d, sizeof(uint32_t) * (size_t)(src->n > 0 ? src->n : 1));
    dst->n = src->n;
    memset(dst->d + dst->n, 0, sizeof(uint32_t) * (size_t)(BN_MAX_LIMBS - dst->n));
}

int bn_cmp(const bn *a, const bn *b) {
    if (a->n != b->n) return a->n < b->n ? -1 : 1;
    for (int i = a->n - 1; i >= 0; i--) {
        if (a->d[i] != b->d[i]) return a->d[i] < b->d[i] ? -1 : 1;
    }
    return 0;
}

int bn_bit(const bn *a, int i) {
    if (i < 0 || i >= BN_MAX_LIMBS * 32) return 0;
    int limb = i / 32, off = i % 32;
    if (limb >= a->n) return 0;
    return (int)((a->d[limb] >> off) & 1u);
}

void bn_set_bit(bn *a, int i) {
    if (i < 0 || i >= BN_MAX_LIMBS * 32) return;
    int limb = i / 32;
    a->d[limb] |= 1u << (i % 32);
    if (limb + 1 > a->n) a->n = limb + 1;
}

int bn_bitlen(const bn *a) {
    if (a->n == 0) return 0;
    uint32_t top = a->d[a->n - 1];
    int bits = 0;
    while (top) { bits++; top >>= 1; }
    return (a->n - 1) * 32 + bits;
}

void bn_add(bn *r, const bn *a, const bn *b) {
    int n = a->n > b->n ? a->n : b->n;
    uint64_t carry = 0;
    for (int i = 0; i < n; i++) {
        uint64_t s = carry;
        if (i < a->n) s += a->d[i];
        if (i < b->n) s += b->d[i];
        r->d[i] = (uint32_t)s;
        carry = s >> 32;
    }
    r->n = n;
    if (carry && n < BN_MAX_LIMBS) r->d[r->n++] = (uint32_t)carry;
    bn_trim(r);
}

void bn_sub(bn *r, const bn *a, const bn *b) {
    int64_t borrow = 0;
    int n = a->n;
    for (int i = 0; i < n; i++) {
        int64_t d = (int64_t)a->d[i] - borrow - (i < b->n ? (int64_t)b->d[i] : 0);
        borrow = 0;
        if (d < 0) { d += ((int64_t)1 << 32); borrow = 1; }
        r->d[i] = (uint32_t)d;
    }
    for (int i = n; i < BN_MAX_LIMBS; i++) r->d[i] = 0;
    r->n = n;
    bn_trim(r);
}

void bn_lshift1(bn *a) {
    uint32_t carry = 0;
    for (int i = 0; i < a->n; i++) {
        uint32_t nc = a->d[i] >> 31;
        a->d[i] = (a->d[i] << 1) | carry;
        carry = nc;
    }
    if (carry && a->n < BN_MAX_LIMBS) a->d[a->n++] = carry;
}

void bn_rshift1(bn *a) {
    uint32_t carry = 0;
    for (int i = a->n - 1; i >= 0; i--) {
        uint32_t nc = a->d[i] & 1u;
        a->d[i] = (a->d[i] >> 1) | (carry << 31);
        carry = nc;
    }
    bn_trim(a);
}

void bn_mul(bn *r, const bn *a, const bn *b) {
    bn acc;
    bn_zero(&acc);
    if (a->n == 0 || b->n == 0) { bn_copy(r, &acc); return; }
    int n = a->n + b->n;
    if (n > BN_MAX_LIMBS) n = BN_MAX_LIMBS;
    uint32_t tmp[BN_MAX_LIMBS + 2];
    memset(tmp, 0, sizeof(tmp));
    for (int i = 0; i < a->n; i++) {
        uint64_t carry = 0;
        for (int j = 0; j < b->n && (i + j) < (int)(sizeof(tmp) / sizeof(tmp[0])); j++) {
            uint64_t cur = (uint64_t)tmp[i + j] + (uint64_t)a->d[i] * b->d[j] + carry;
            tmp[i + j] = (uint32_t)cur;
            carry = cur >> 32;
        }
        int k = i + b->n;
        while (carry && k < (int)(sizeof(tmp) / sizeof(tmp[0]))) {
            uint64_t cur = (uint64_t)tmp[k] + carry;
            tmp[k] = (uint32_t)cur;
            carry = cur >> 32;
            k++;
        }
    }
    for (int i = 0; i < BN_MAX_LIMBS; i++) acc.d[i] = tmp[i];
    acc.n = n;
    bn_trim(&acc);
    bn_copy(r, &acc);
}

void bn_mul_small(bn *r, const bn *a, uint32_t m) {
    uint64_t carry = 0;
    for (int i = 0; i < a->n; i++) {
        uint64_t cur = (uint64_t)a->d[i] * m + carry;
        r->d[i] = (uint32_t)cur;
        carry = cur >> 32;
    }
    r->n = a->n;
    if (carry && r->n < BN_MAX_LIMBS) r->d[r->n++] = (uint32_t)carry;
    bn_trim(r);
}

uint32_t bn_mod_small(const bn *a, uint32_t m) {
    uint64_t r = 0;
    for (int i = a->n - 1; i >= 0; i--) {
        r = ((r << 32) | a->d[i]) % m;
    }
    return (uint32_t)r;
}

uint32_t bn_divmod_small(bn *q, const bn *a, uint32_t m) {
    uint64_t r = 0;
    bn out;
    bn_zero(&out);
    out.n = a->n;
    for (int i = a->n - 1; i >= 0; i--) {
        uint64_t cur = (r << 32) | a->d[i];
        out.d[i] = (uint32_t)(cur / m);
        r = cur % m;
    }
    bn_trim(&out);
    if (q) bn_copy(q, &out);
    return (uint32_t)r;
}

void bn_from_bytes_be(bn *a, const uint8_t *buf, size_t len) {
    bn_zero(a);
    size_t i = 0;
    while (i < len && buf[i] == 0) i++;
    size_t rem = len - i;
    int limbs = (int)((rem + 3) / 4);
    if (limbs > BN_MAX_LIMBS) limbs = BN_MAX_LIMBS;
    for (int l = 0; l < limbs; l++) {
        uint32_t v = 0;
        for (int b = 0; b < 4; b++) {
            size_t idx = rem - (size_t)l * 4 - (size_t)b;
            if (idx == 0) break;
            size_t pos = i + idx - 1;
            if (pos < len) v |= (uint32_t)buf[pos] << (8 * b);
        }
        a->d[l] = v;
    }
    a->n = limbs;
    bn_trim(a);
}

void bn_to_bytes_be(const bn *a, uint8_t *buf, size_t len) {
    memset(buf, 0, len);
    for (size_t i = 0; i < len; i++) {
        size_t from_end = i; /* i-th byte from the end */
        int limb = (int)(from_end / 4);
        int off = (int)(from_end % 4);
        if (limb >= a->n) continue;
        buf[len - 1 - i] = (uint8_t)((a->d[limb] >> (8 * off)) & 0xff);
    }
}

int mont_init(mont *m, const bn *modulus) {
    if (!bn_is_odd(modulus) || modulus->n == 0) return -1;
    int limbs = modulus->n;
    if (limbs < 1 || limbs > BN_MAX_LIMBS - 2) return -1;
    bn_copy(&m->n, modulus);
    m->limbs = limbs;

    /* n0inv = -n0^-1 mod 2^32 via Newton iteration */
    uint32_t inv = 1;
    for (int i = 0; i < 6; i++) inv *= 2u - modulus->d[0] * inv;
    m->n0inv = (uint32_t)(0u - inv);

    /* R mod n by doubling 1 -> 2^(32*limbs) */
    bn r;
    bn_from_u32(&r, 1);
    for (int i = 0; i < limbs * 32; i++) {
        bn_lshift1(&r);
        if (bn_cmp(&r, &m->n) >= 0) bn_sub(&r, &r, &m->n);
    }
    /* R^2 mod n by doubling R another 32*limbs times */
    bn rr;
    bn_copy(&rr, &r);
    for (int i = 0; i < limbs * 32; i++) {
        bn_lshift1(&rr);
        if (bn_cmp(&rr, &m->n) >= 0) bn_sub(&rr, &rr, &m->n);
    }
    bn_copy(&m->rr, &rr);
    return 0;
}

/* CIOS Montgomery multiplication */
void mont_mul(const mont *m, const bn *a, const bn *b, bn *out) {
    int s = m->limbs;
    uint32_t t[BN_MAX_LIMBS + 2];
    memset(t, 0, sizeof(t));
    for (int i = 0; i < s; i++) {
        uint64_t c = 0;
        uint32_t bi = (i < b->n) ? b->d[i] : 0;
        for (int j = 0; j < s; j++) {
            uint64_t aj = (j < a->n) ? a->d[j] : 0;
            uint64_t cur = (uint64_t)t[j] + (uint64_t)aj * bi + c;
            t[j] = (uint32_t)cur;
            c = cur >> 32;
        }
        uint64_t cur = (uint64_t)t[s] + c;
        t[s] = (uint32_t)cur;
        t[s + 1] = (uint32_t)(cur >> 32);

        uint32_t mm = t[0] * m->n0inv;
        c = ((uint64_t)t[0] + (uint64_t)mm * m->n.d[0]) >> 32;
        for (int j = 1; j < s; j++) {
            uint64_t cur2 = (uint64_t)t[j] + (uint64_t)mm * m->n.d[j] + c;
            t[j - 1] = (uint32_t)cur2;
            c = cur2 >> 32;
        }
        uint64_t cur3 = (uint64_t)t[s] + c;
        t[s - 1] = (uint32_t)cur3;
        t[s] = t[s + 1] + (uint32_t)(cur3 >> 32);
        t[s + 1] = 0;
    }
    bn res;
    bn_zero(&res);
    for (int i = 0; i < s; i++) res.d[i] = t[i];
    if (t[s]) { res.d[s] = t[s]; res.n = s + 1; }
    else res.n = s;
    bn_trim(&res);
    while (bn_cmp(&res, &m->n) >= 0) bn_sub(&res, &res, &m->n);
    bn_copy(out, &res);
}

void mont_modexp(const mont *m, const bn *base, const bn *exp, bn *out) {
    bn one, acc, bm;
    bn_from_u32(&one, 1);
    mont_mul(m, base, &m->rr, &bm);   /* a*R mod n */
    mont_mul(m, &one, &m->rr, &acc);  /* R mod n = 1 in montgomery domain */
    int bits = bn_bitlen(exp);
    for (int i = bits - 1; i >= 0; i--) {
        mont_mul(m, &acc, &acc, &acc);
        if (bn_bit(exp, i)) mont_mul(m, &acc, &bm, &acc);
    }
    mont_mul(m, &acc, &one, out);     /* back to normal domain */
}

int bn_mod_exp(bn *out, const bn *base, const bn *exp, const bn *mod) {
    mont m;
    if (mont_init(&m, mod) != 0) return -1;
    bn b;
    bn_copy(&b, base);
    bn r;
    bn_zero(&r);
    if (bn_cmp(&b, mod) >= 0) {
        /* reduce by repeated subtraction is too slow; require base < mod caller side */
        return -1;
    }
    mont_modexp(&m, &b, exp, &r);
    bn_copy(out, &r);
    return 0;
}

int bn_random_bytes(uint8_t *buf, size_t len) {
#ifdef ANYTV_HOST_LINUX
    size_t got = 0;
    while (got < len) {
        ssize_t r = getrandom(buf + got, len - got, 0);
        if (r <= 0) {
            int fd = open("/dev/urandom", O_RDONLY);
            if (fd < 0) return -1;
            r = read(fd, buf + got, len - got);
            close(fd);
            if (r <= 0) return -1;
        }
        got += (size_t)r;
    }
    return 0;
#else
    /* iOS: anytv_ios_random is provided by the Swift side / Security framework */
    extern int anytv_ios_random(uint8_t *buf, size_t len);
    return anytv_ios_random(buf, len);
#endif
}
