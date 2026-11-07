/* anytv_facade.h - small C surface so Swift never touches the protocol structs. */
#ifndef ANYTV_FACADE_H
#define ANYTV_FACADE_H

#include <stdint.h>
#include <stddef.h>

typedef struct anytv_client anytv_client;

anytv_client *anytv_client_new(void);
void anytv_client_free(anytv_client *c);

/* Key material: 768 bytes = n|e|d, big endian. */
#define ANYTV_KEY_SIZE 768

/* Generate a fresh RSA-2048 key inside the client (takes ~1-3 s). */
int anytv_client_keygen(anytv_client *c);
int anytv_client_key_load(anytv_client *c, const uint8_t *blob, size_t len);
int anytv_client_key_export(const anytv_client *c, uint8_t *out, size_t len);

/* Connect + handshake + auth. auth_wait_ms is how long the user has to accept
 * the prompt on the TV. Returns 0 on success, negative ADB_ERR_*. */
int anytv_client_connect(anytv_client *c, const char *host, int port,
                         int timeout_ms, int auth_wait_ms);
void anytv_client_disconnect(anytv_client *c);
int anytv_client_connected(const anytv_client *c);

const char *anytv_client_model(const anytv_client *c);
const char *anytv_client_android(const anytv_client *c);
const char *anytv_client_error(const anytv_client *c);

/* Commands. All return 0 on success or a negative ADB_ERR_*. */
int anytv_client_key(anytv_client *c, int code, int repeat);
int anytv_client_text(anytv_client *c, const char *utf8);
int anytv_client_shell(anytv_client *c, const char *command, char *out, size_t outlen,
                       int timeout_ms);
/* Newline separated package list, compact (fits in ~32 KiB). */
int anytv_client_apps(anytv_client *c, char *out, size_t outlen);
int anytv_client_launch(anytv_client *c, const char *package);
/* PNG screenshot into out; returns byte count or negative error. */
int anytv_client_screenshot(anytv_client *c, uint8_t *out, size_t outlen);
int anytv_client_wake(anytv_client *c);
int anytv_client_sleep(anytv_client *c);

#endif
