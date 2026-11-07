/* anytv_rsa.c - RSA-2048 generation and PKCS#1 v1.5 / SHA-1 signing. */
#include "anytv_rsa.h"
#include "anytv_sha1.h"
#include <string.h>

static const int SMALL_PRIMES[] = {
    3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53, 59, 61, 67, 71, 73,
    79, 83, 89, 97, 101, 103, 107, 109, 113, 127, 131, 137, 139, 149, 151, 157,
    163, 167, 173, 179, 181, 191, 193, 197, 199, 211, 223, 227, 229, 233, 239,
    241, 251, 257, 263, 269, 271, 277, 281, 283, 293, 307, 311, 313, 317, 331,
    337, 347, 349, 353, 359, 367, 373, 379, 383, 389, 397, 401, 409, 419, 421,
    431, 433, 439, 443, 449, 457, 461, 463, 467, 479, 487, 491, 499, 503, 509,
    521, 523, 541, 547, 557, 563, 569, 571, 577, 587, 593, 599, 601, 607, 613,
    617, 619, 631, 641, 643, 647, 653, 659, 661, 673, 677, 683, 691, 701, 709,
    719, 727, 733, 739, 743, 751, 757, 761, 769, 773, 787, 797, 809, 811, 821,
    823, 827, 829, 839, 853, 857, 859, 863, 877, 881, 883, 887, 907, 911, 919,
    929, 937, 941, 947, 953, 967, 971, 977, 983, 991, 997
};
#define N_SMALL_PRIMES ((int)(sizeof(SMALL_PRIMES) / sizeof(SMALL_PRIMES[0])))

static int is_probable_prime(const bn *cand, int rounds) {
    bn one, nm1, d, two;
    bn_from_u32(&one, 1);
    bn_from_u32(&two, 2);
    bn_copy(&nm1, cand);
    bn_sub(&nm1, cand, &one);

    /* d = nm1 >> s */
    int s = 0;
    bn_copy(&d, &nm1);
    while (!bn_is_odd(&d)) { bn_rshift1(&d); s++; }

    mont m;
    if (mont_init(&m, cand) != 0) return 0;

    uint32_t bases[12];
    int nb = 0;
    for (int i = 0; i < 8; i++) bases[nb++] = (uint32_t)SMALL_PRIMES[i] + 2; /* 5,7,9.. not ideal */
    (void)rounds;
    /* small fixed bases (2,3,5,7,11,13,17,19,23,29,31,37) */
    uint32_t fixed[12] = {2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37};
    nb = 12;
    for (int i = 0; i < nb; i++) bases[i] = fixed[i];

    for (int b = 0; b < nb; b++) {
        bn base, x;
        bn_from_u32(&base, bases[b]);
        if (bn_cmp(&base, &nm1) >= 0) continue;
        mont_modexp(&m, &base, &d, &x);
        if (bn_cmp(&x, &one) == 0 || bn_cmp(&x, &nm1) == 0) continue;
        int composite = 1;
        for (int r = 1; r < s; r++) {
            mont_mul(&m, &x, &x, &x);
            if (bn_cmp(&x, &nm1) == 0) { composite = 0; break; }
            if (bn_cmp(&x, &one) == 0) { return 0; }
        }
        if (composite) return 0;
    }
    return 1;
}

static void random_candidate(bn *out, int bits) {
    uint8_t buf[128];
    memset(buf, 0, sizeof(buf));
    if (bn_random_bytes(buf, (size_t)bits / 8) != 0) {
        /* fallback: derive from a counter so we never loop forever */
        static uint32_t seed = 0x1234567u;
        for (size_t i = 0; i < (size_t)bits / 8; i++) {
            seed = seed * 1103515245u + 12345u;
            buf[i] = (uint8_t)(seed >> 16);
        }
    }
    int bytes = bits / 8;
    buf[0] |= 0xC0;              /* top two bits: guarantee exact bit length */
    buf[bytes - 1] |= 0x01;      /* odd */
    bn_from_bytes_be(out, buf, (size_t)bytes);
}

static int gen_prime(bn *p, int bits) {
    for (int attempts = 0; attempts < 20000; attempts++) {
        random_candidate(p, bits);
        int divisible = 0;
        for (int i = 0; i < N_SMALL_PRIMES; i++) {
            if (bn_mod_small(p, (uint32_t)SMALL_PRIMES[i]) == 0) { divisible = 1; break; }
        }
        if (divisible) continue;
        if (bn_mod_small(p, 65537u) == 1) continue; /* e=65537 must be invertible */
        if (!is_probable_prime(p, 12)) continue;
        return 0;
    }
    return -1;
}

static int build_pub_blob(rsa_key *k) {
    /* Android's RSAPublicKey wire format (ANDROID_PUBKEY_ENCODED_SIZE = 524):
     *   uint32 modulus_size_words | uint32 n0inv | uint8 modulus[256] |
     *   uint8 rr[256] | uint32 exponent
     * All little-endian. rr is R^2 mod n with R = 2^2048; n0inv is -1/n[0] mod 2^32.
     * This is exactly what adbd's android_pubkey_decode accepts. */
    mont m;
    if (mont_init(&m, &k->n) != 0) return -1;
    uint8_t *o = k->pub_blob;
    const bn *n = &k->n;

    uint32_t words = 256 / 4; /* 64 words for a 2048-bit modulus */
    o[0] = (uint8_t)(words & 0xff);
    o[1] = (uint8_t)((words >> 8) & 0xff);
    o[2] = (uint8_t)((words >> 16) & 0xff);
    o[3] = (uint8_t)((words >> 24) & 0xff);

    uint32_t n0inv = m.n0inv;
    o[4] = (uint8_t)(n0inv & 0xff);
    o[5] = (uint8_t)((n0inv >> 8) & 0xff);
    o[6] = (uint8_t)((n0inv >> 16) & 0xff);
    o[7] = (uint8_t)((n0inv >> 24) & 0xff);

    for (size_t i = 0; i < 256; i++) {
        int limb = (int)(i / 4), off = (int)(i % 4);
        o[8 + i] = (limb < n->n) ? (uint8_t)((n->d[limb] >> (8 * off)) & 0xff) : 0;
    }
    for (size_t i = 0; i < 256; i++) {
        int limb = (int)(i / 4), off = (int)(i % 4);
        o[264 + i] = (limb < m.rr.n) ? (uint8_t)((m.rr.d[limb] >> (8 * off)) & 0xff) : 0;
    }
    uint32_t e = 65537;
    if (k->e.n <= 1) e = k->e.n ? k->e.d[0] : 0;
    o[520] = (uint8_t)(e & 0xff);
    o[521] = (uint8_t)((e >> 8) & 0xff);
    o[522] = (uint8_t)((e >> 16) & 0xff);
    o[523] = (uint8_t)((e >> 24) & 0xff);

    k->pub_blob_len = 524;
    k->pub_legacy_len = 524;
    k->pub_spki_len = 0;
    return 0;
}

int rsa_generate(rsa_key *k) {
    bn p, q, phi, pm1, qm1, one, tmp;
    bn_from_u32(&one, 1);
    bn_from_u32(&k->e, 65537u);

    for (;;) {
        if (gen_prime(&p, 1024) != 0) return -1;
        if (gen_prime(&q, 1024) != 0) return -1;
        if (bn_cmp(&p, &q) == 0) continue;
        bn_mul(&k->n, &p, &q);
        if (bn_bitlen(&k->n) != 2048) continue;
        break;
    }
    bn_sub(&pm1, &p, &one);
    bn_sub(&qm1, &q, &one);
    bn_mul(&phi, &pm1, &qm1);

    /* d = (1 + k*phi) / e, with k chosen so the numerator divides by e */
    uint32_t r = bn_mod_small(&phi, 65537u);
    /* modular inverse of r mod 65537 via Fermat (65537 is prime) */
    uint64_t inv = 1, b = r, e = 65535; /* r^(65535) mod 65537 */
    while (e) {
        if (e & 1) inv = (inv * b) % 65537u;
        b = (b * b) % 65537u;
        e >>= 1;
    }
    uint32_t kk = (uint32_t)((65537u - inv) % 65537u);
    if (kk == 0) kk = 65537u;
    bn_mul_small(&tmp, &phi, kk);
    bn_add(&tmp, &tmp, &one);
    bn q2;
    bn_divmod_small(&q2, &tmp, 65537u);
    bn_copy(&k->d, &q2);

    if (build_pub_blob(k) != 0) return -1;
    return 0;
}

int rsa_sign_sha1(const rsa_key *k, const uint8_t *data, size_t len, uint8_t sig_be[RSA_BYTES]) {
    static const uint8_t digest_info_prefix[15] = {
        0x30, 0x21, 0x30, 0x09, 0x06, 0x05, 0x2b, 0x0e,
        0x03, 0x02, 0x1a, 0x05, 0x00, 0x04, 0x14
    };
    uint8_t hash[20];
    sha1_digest(data, len, hash);

    uint8_t em[RSA_BYTES];
    size_t tlen = sizeof(digest_info_prefix) + sizeof(hash);
    em[0] = 0x00;
    em[1] = 0x01;
    size_t ps_end = RSA_BYTES - tlen - 1;
    for (size_t i = 2; i < ps_end; i++) em[i] = 0xff;
    em[ps_end] = 0x00;
    memcpy(em + ps_end + 1, digest_info_prefix, sizeof(digest_info_prefix));
    memcpy(em + ps_end + 1 + sizeof(digest_info_prefix), hash, sizeof(hash));

    bn m, s;
    bn_from_bytes_be(&m, em, sizeof(em));
    if (bn_cmp(&m, &k->n) >= 0) return -1;
    if (bn_mod_exp(&s, &m, &k->d, &k->n) != 0) return -1;
    bn_to_bytes_be(&s, sig_be, RSA_BYTES);
    return 0;
}

static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int anytv_base64_encode(const uint8_t *in, size_t inlen, char *out, size_t outlen) {
    size_t need = ((inlen + 2) / 3) * 4;
    if (outlen < need + 1) return -1;
    size_t o = 0;
    for (size_t i = 0; i < inlen; i += 3) {
        uint32_t v = (uint32_t)in[i] << 16;
        if (i + 1 < inlen) v |= (uint32_t)in[i + 1] << 8;
        if (i + 2 < inlen) v |= in[i + 2];
        out[o++] = B64[(v >> 18) & 63];
        out[o++] = B64[(v >> 12) & 63];
        out[o++] = (i + 1 < inlen) ? B64[(v >> 6) & 63] : '=';
        out[o++] = (i + 2 < inlen) ? B64[v & 63] : '=';
    }
    out[o] = '\0';
    return (int)o;
}

size_t rsa_public_adb_b64(const rsa_key *k, char *out, size_t outlen) {
    int n = anytv_base64_encode(k->pub_blob, k->pub_blob_len, out, outlen);
    return n < 0 ? 0 : (size_t)n;
}

size_t rsa_public_adb_b64_legacy(const rsa_key *k, char *out, size_t outlen) {
    return rsa_public_adb_b64(k, out, outlen);
}

size_t rsa_export(const rsa_key *k, uint8_t *out, size_t outlen) {
    if (outlen < RSA_BYTES * 3) return 0;
    bn_to_bytes_be(&k->n, out, RSA_BYTES);
    bn_to_bytes_be(&k->e, out + RSA_BYTES, RSA_BYTES);
    bn_to_bytes_be(&k->d, out + RSA_BYTES * 2, RSA_BYTES);
    return RSA_BYTES * 3;
}

int rsa_import(rsa_key *k, const uint8_t *in, size_t inlen) {
    if (inlen < RSA_BYTES * 3) return -1;
    bn_from_bytes_be(&k->n, in, RSA_BYTES);
    bn_from_bytes_be(&k->e, in + RSA_BYTES, RSA_BYTES);
    bn_from_bytes_be(&k->d, in + RSA_BYTES * 2, RSA_BYTES);
    return build_pub_blob(k);
}
