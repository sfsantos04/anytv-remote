/* anytv_facade.h - one C surface for Swift: ADB, Roku ECP, webOS and Tizen. */
#ifndef ANYTV_FACADE_H
#define ANYTV_FACADE_H

#include <stdint.h>
#include <stddef.h>
#include "anytv_remote.h"

typedef struct anytv_client anytv_client;

anytv_client *anytv_client_new(void);              /* ADB */
anytv_client *anytv_client_new_proto(int proto);   /* anytv_proto */
void anytv_client_free(anytv_client *c);

/* Key material for the ADB transport: 768 bytes = n|e|d, big endian. */
#define ANYTV_KEY_SIZE 768
int anytv_client_keygen(anytv_client *c);
int anytv_client_key_load(anytv_client *c, const uint8_t *blob, size_t len);
int anytv_client_key_export(const anytv_client *c, uint8_t *out, size_t len);

/* Connect. auth_wait_ms is how long the user has to accept the prompt on the TV. */
int anytv_client_connect(anytv_client *c, const char *host, int port,
                         int timeout_ms, int auth_wait_ms);
void anytv_client_disconnect(anytv_client *c);
int anytv_client_connected(const anytv_client *c);

uint32_t anytv_client_caps(const anytv_client *c);
int anytv_client_proto(const anytv_client *c);
const char *anytv_client_proto_name(const anytv_client *c);
const char *anytv_client_model(const anytv_client *c);
const char *anytv_client_android(const anytv_client *c);
const char *anytv_client_error(const anytv_client *c);

/* Tokens: webOS client-key, Tizen token. Persist it or the TV asks again. */
const char *anytv_client_token(const anytv_client *c);
void anytv_client_set_token(anytv_client *c, const char *token);
void anytv_client_set_mac(anytv_client *c, const char *mac);
const char *anytv_client_mac(const anytv_client *c);

/* Canonical commands, mapped per protocol. */
int anytv_client_cmd(anytv_client *c, int cmd, int repeat);
/* Raw Android keycode (ADB only). */
int anytv_client_key(anytv_client *c, int code, int repeat);
int anytv_client_text(anytv_client *c, const char *utf8);
/* ADB only. */
int anytv_client_shell(anytv_client *c, const char *command, char *out, size_t outlen,
                       int timeout_ms);
/* "id\tname" lines (ADB emits the bare package id without a tab). */
int anytv_client_apps(anytv_client *c, char *out, size_t outlen);
/* ADB only: PNG/WebP bytes of the app icon, extracted inside the device with
 * busybox when the app is not on a store. Returns bytes copied or < 0. */
int anytv_client_app_icon(anytv_client *c, const char *app_id, uint8_t *out, size_t outlen);
/* Tries the transport that can list apps (on Samsung, the dev bridge) and
 * updates the capability mask. 1 = lista disponivel. */
int anytv_client_probe_apps(anytv_client *c);
int anytv_client_launch(anytv_client *c, const char *app_id);
/* Live screen: starts the device encoder and returns ADB_OK. read hands back
 * H.264 Annex-B bytes as they arrive; stop closes the pipe and kills the child. */
int anytv_client_stream_start(anytv_client *c, int width, int height, int bitrate_bps, int seconds);
int anytv_client_stream_read(anytv_client *c, uint8_t *out, size_t outlen, int timeout_ms);
void anytv_client_stream_stop(anytv_client *c);
/* scrcpy transport (Android): pushes the server jar supplied by the app, starts
 * it and opens the video socket. Afterwards use stream_read/stream_stop. */
int anytv_client_scrcpy_start(anytv_client *c, const uint8_t *jar, size_t jar_len,
                              int bitrate_bps, int max_fps, int max_size, int timeout_ms);
int anytv_client_scrcpy_width(const anytv_client *c);
int anytv_client_scrcpy_height(const anytv_client *c);
/* ADB only: PNG bytes. */
int anytv_client_screenshot(anytv_client *c, uint8_t *out, size_t outlen);
int anytv_client_info(anytv_client *c, char *out, size_t outlen);
int anytv_client_wake(anytv_client *c);
int anytv_client_power_on(anytv_client *c);   /* Wake-on-LAN */
int anytv_client_sleep(anytv_client *c);      /* power off / sleep */

/* Port probe used by the discovery screen (1 = something answered). */
int anytv_probe_port(const char *host, int port, int timeout_ms);

#endif
