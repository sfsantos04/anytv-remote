/* test_crypto.c - verifies SHA-1 and RSA against independent references.
 * Emits a vector file so a Python/OpenSSL checker can confirm the signature. */
#include "../core/anytv_bn.h"
#include "../core/anytv_rsa.h"
#include "../core/anytv_sha1.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

static int fails = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL %s\n", msg); fails++; } } while (0)

static void hex(const uint8_t *b, size_t n, char *out) {
    static const char *H = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) { out[i*2] = H[b[i] >> 4]; out[i*2+1] = H[b[i] & 15]; }
    out[n*2] = 0;
}

int main(int argc, char **argv) {
    /* SHA-1 known answers */
    uint8_t h[20]; char hs[64];
    sha1_digest("abc", 3, h); hex(h, 20, hs);
    CHECK(strcmp(hs, "a9993e364706816aba3e25717850c26c9cd0d89d") == 0, "sha1(abc)");
    sha1_digest("", 0, h); hex(h, 20, hs);
    CHECK(strcmp(hs, "da39a3ee5e6b4b0d3255bfef95601890afd80709") == 0, "sha1(empty)");
    const char *longmsg = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    sha1_digest(longmsg, strlen(longmsg), h); hex(h, 20, hs);
    CHECK(strcmp(hs, "84983e441c3bd26ebaae4aa1f95129e5e54670f1") == 0, "sha1(long)");

    /* bignum sanity */
    bn a, b, r;
    bn_from_bytes_be(&a, (const uint8_t *)"\x01\x00", 2);      /* 256 */
    bn_from_u32(&b, 255);
    bn_add(&r, &a, &b);
    CHECK(bn_cmp(&r, &b) > 0 && bn_bitlen(&r) == 9, "bn add");
    bn_mul(&r, &a, &b);
    CHECK(bn_mod_small(&r, 7) == (256u * 255u) % 7u, "bn mul/mod");

    /* modular exponentiation: 4^13 mod 497 == 445 (classic example) */
    bn base, exp, mod, res;
    bn_from_u32(&base, 4); bn_from_u32(&exp, 13); bn_from_u32(&mod, 497);
    CHECK(bn_mod_exp(&res, &base, &exp, &mod) == 0, "modexp runs");
    CHECK(bn_mod_small(&res, 1000) == 445u, "modexp 4^13 mod 497 = 445");

    /* RSA generation + signature */
    rsa_key key;
    clock_t t0 = clock();
    CHECK(rsa_generate(&key) == 0, "rsa_generate");
    double secs = (double)(clock() - t0) / CLOCKS_PER_SEC;
    char nb[600], eb[600], db[600], pb[1200];
    hex((const uint8_t *)"", 0, nb);
    uint8_t buf[RSA_BYTES];
    bn_to_bytes_be(&key.n, buf, RSA_BYTES); hex(buf, RSA_BYTES, nb);
    bn_to_bytes_be(&key.e, buf, RSA_BYTES); hex(buf, RSA_BYTES, eb);
    bn_to_bytes_be(&key.d, buf, RSA_BYTES); hex(buf, RSA_BYTES, db);
    CHECK(rsa_public_adb_b64(&key, pb, sizeof(pb)) > 0, "pubkey base64");
    CHECK(key.pub_blob_len == 524, "android pubkey blob is 524 bytes");
    CHECK(key.pub_blob[0] == 64 && key.pub_blob[1] == 0, "modulus_size_words = 64");
    CHECK(key.pub_blob[520] == 0x01 && key.pub_blob[521] == 0x00 && key.pub_blob[522] == 0x01,
          "exponent 65537 little endian");

    const char *token_hex = "de ad be ef 01 23 45 67 89 ab cd ef fe dc ba 98 76 54 32 10";
    uint8_t token[20];
    for (int i = 0; i < 20; i++) token[i] = (uint8_t)strtol((const char[]){token_hex[i*3], token_hex[i*3+1], 0}, NULL, 16);

    uint8_t sig[RSA_BYTES];
    CHECK(rsa_sign_sha1(&key, token, sizeof(token), sig) == 0, "rsa_sign_sha1");

    /* self-verify: sig^e mod n must equal the PKCS#1 v1.5 block */
    bn sig_bn, em_bn, back;
    bn_from_bytes_be(&sig_bn, sig, RSA_BYTES);
    CHECK(bn_mod_exp(&back, &sig_bn, &key.e, &key.n) == 0, "verify modexp");
    uint8_t emb[RSA_BYTES];
    bn_to_bytes_be(&back, emb, RSA_BYTES);
    CHECK(emb[0] == 0x00 && emb[1] == 0x01 && emb[2] == 0xff, "pkcs1 header");
    sha1_digest(token, sizeof(token), h);
    char hash_hex[64]; hex(h, 20, hash_hex);
    CHECK(emb[RSA_BYTES - 1] == h[19], "digest tail");
    char dec_hex[600]; hex(emb, RSA_BYTES, dec_hex);
    CHECK(strstr(dec_hex, hash_hex) == dec_hex + strlen(dec_hex) - 40, "digest inside pkcs1 block");
    CHECK(bn_bitlen(&key.n) == 2048, "2048-bit modulus");

    /* export/import roundtrip keeps the same public blob */
    uint8_t blob[768]; char pb2[1200];
    CHECK(rsa_export(&key, blob, sizeof(blob)) == 768, "export size");
    rsa_key key2;
    CHECK(rsa_import(&key2, blob, sizeof(blob)) == 0, "import");
    rsa_public_adb_b64(&key2, pb2, sizeof(pb2));
    CHECK(strcmp(pb, pb2) == 0, "import keeps pubkey");

    /* dump vectors for the independent Python check */
    const char *path = argc > 1 ? argv[1] : "/tmp/anytv_rsa_vector.json";
    FILE *f = fopen(path, "w");
    if (f) {
        char sh[1200];
        hex(sig, RSA_BYTES, sh);
        char th[64]; hex(token, 20, th);
        fprintf(f, "{\"n\":\"%s\",\"d\":\"%s\",\"token\":\"%s\",\"sig\":\"%s\",\"pub\":\"%s\"}\n",
                nb, db, th, sh, pb);
        fclose(f);
    } else {
        printf("FAIL could not write vector file\n"); fails++;
    }
    (void)em_bn;
    (void)hs;

    printf("keygen_seconds=%.3f\n", secs);
    printf("pub_blob_len=%zu\n", key.pub_blob_len);
    printf("fails=%d\n", fails);
    return fails ? 1 : 0;
}
