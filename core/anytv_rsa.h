/* anytv_rsa.h - RSA-2048 keygen + PKCS#1 v1.5 SHA-1 signatures for ADB auth. */
#ifndef ANYTV_RSA_H
#define ANYTV_RSA_H

#include "anytv_bn.h"
#include <stdint.h>
#include <stddef.h>

#define RSA_BYTES 256
#define RSA_PUB_BLOB_MAX 600

typedef struct {
    bn n;
    bn e;
    bn d;
    uint8_t pub_blob[RSA_PUB_BLOB_MAX];      /* legacy uint32-len form */
    size_t pub_blob_len;
    uint8_t pub_blob_spki[RSA_PUB_BLOB_MAX]; /* X.509 SubjectPublicKeyInfo DER */
    size_t pub_spki_len;
    size_t pub_legacy_len;
} rsa_key;

/* Generate a 2048-bit key. Returns 0 on success. Slow-ish on first launch,
 * so the UI must run it off the main thread. */
int rsa_generate(rsa_key *k);

/* Clear the private material (keeps public). */
int rsa_sign_sha1(const rsa_key *k, const uint8_t *data, size_t len, uint8_t sig_be[RSA_BYTES]);

/* base64 of the ADB public key blob (X.509 SPKI DER, no trailing NUL). */
size_t rsa_public_adb_b64(const rsa_key *k, char *out, size_t outlen);
/* pre-2017 adbd only understood this one. */
size_t rsa_public_adb_b64_legacy(const rsa_key *k, char *out, size_t outlen);

/* Serialize / load private key material as big-endian fixed-width bytes. */
#define RSA_BLOB_ARGS 3
size_t rsa_export(const rsa_key *k, uint8_t *out, size_t outlen); /* n|e|d, 256+256+256 */
int rsa_import(rsa_key *k, const uint8_t *in, size_t inlen);

int anytv_base64_encode(const uint8_t *in, size_t inlen, char *out, size_t outlen);

#endif
