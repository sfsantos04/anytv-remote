/* anytv_remote.h - one interface over the TV control protocols we speak. */
#ifndef ANYTV_REMOTE_H
#define ANYTV_REMOTE_H

#include <stdint.h>
#include <stddef.h>
#include "anytv_ws.h"

typedef enum {
    ANYTV_PROTO_ADB = 0,   /* Android TV / box, debugging enabled (5555) */
    ANYTV_PROTO_ROKU,      /* Roku ECP over HTTP (8060) - no pairing at all */
    ANYTV_PROTO_WEBOS,     /* LG webOS SSAP over websocket (3000) */
    ANYTV_PROTO_TIZEN      /* Samsung Tizen remote over websocket (8001) */
} anytv_proto;

/* capabilities */
#define ANYTV_CAP_KEY      (1u << 0)
#define ANYTV_CAP_TEXT     (1u << 1)
#define ANYTV_CAP_APPS     (1u << 2)
#define ANYTV_CAP_LAUNCH   (1u << 3)
#define ANYTV_CAP_SCREEN   (1u << 4)
#define ANYTV_CAP_POWER_OFF (1u << 5)
#define ANYTV_CAP_POWER_ON (1u << 6)
#define ANYTV_CAP_INFO     (1u << 7)
#define ANYTV_CAP_COLORS   (1u << 8)

typedef enum {
    ANYTV_CMD_UP = 0, ANYTV_CMD_DOWN, ANYTV_CMD_LEFT, ANYTV_CMD_RIGHT, ANYTV_CMD_OK,
    ANYTV_CMD_BACK, ANYTV_CMD_HOME, ANYTV_CMD_MENU, ANYTV_CMD_VOL_UP, ANYTV_CMD_VOL_DOWN,
    ANYTV_CMD_MUTE, ANYTV_CMD_PLAY_PAUSE, ANYTV_CMD_NEXT, ANYTV_CMD_PREV,
    ANYTV_CMD_STOP, ANYTV_CMD_REWIND, ANYTV_CMD_FORWARD, ANYTV_CMD_POWER,
    ANYTV_CMD_INFO, ANYTV_CMD_SETTINGS, ANYTV_CMD_INPUT, ANYTV_CMD_CH_UP, ANYTV_CMD_CH_DOWN,
    ANYTV_CMD_RED, ANYTV_CMD_GREEN, ANYTV_CMD_YELLOW, ANYTV_CMD_BLUE,
    ANYTV_CMD_NUM_0, ANYTV_CMD_NUM_1, ANYTV_CMD_NUM_2, ANYTV_CMD_NUM_3,
    ANYTV_CMD_NUM_4, ANYTV_CMD_NUM_5, ANYTV_CMD_NUM_6, ANYTV_CMD_NUM_7,
    ANYTV_CMD_NUM_8, ANYTV_CMD_NUM_9, ANYTV_CMD_VOICE, ANYTV_CMD_GUIDE,
    ANYTV_CMD_COUNT
} anytv_cmd;

const char *anytv_cmd_name(anytv_cmd cmd);
int anytv_cmd_from_name(const char *name); /* -1 if unknown */

/* Tolerant JSON string reader: finds "key", skips whitespace, reads the string.
 * Returns 0 and fills out when the key exists. */
int anytv_json_string(const char *json, const char *key, char *out, size_t outlen);

/* Shared context for the websocket/http protocols. */
typedef struct {
    char host[64];
    int port;
    ws_conn ws;
    ws_conn pointer;      /* webOS extra socket */
    char token[256];      /* webOS client-key or Tizen token */
    char mac[32];         /* for Wake-on-LAN */
    char name[64];
    char last_error[192]; /* raw handshake answer, shown in the UI */
    int connected;
} proto_ctx;

void proto_ctx_init(proto_ctx *c, const char *host, int port);

uint32_t roku_caps(void);
int roku_connect(proto_ctx *c, int timeout_ms);
int roku_cmd(proto_ctx *c, anytv_cmd cmd);
int roku_text(proto_ctx *c, const char *text);
int roku_apps(proto_ctx *c, char *out, size_t outlen);
int roku_launch(proto_ctx *c, const char *app_id);
int roku_info(proto_ctx *c, char *out, size_t outlen);

uint32_t webos_caps(void);
int webos_connect(proto_ctx *c, int timeout_ms);
int webos_cmd(proto_ctx *c, anytv_cmd cmd);
int webos_text(proto_ctx *c, const char *text);
int webos_apps(proto_ctx *c, char *out, size_t outlen);
int webos_launch(proto_ctx *c, const char *app_id);
int webos_info(proto_ctx *c, char *out, size_t outlen);
int webos_power_off(proto_ctx *c);

uint32_t tizen_caps(void);
int tizen_connect(proto_ctx *c, int timeout_ms);
int tizen_cmd(proto_ctx *c, anytv_cmd cmd);
int tizen_text(proto_ctx *c, const char *text);
int tizen_power_off(proto_ctx *c);
int tizen_info(proto_ctx *c, char *out, size_t outlen);
int tizen_details(proto_ctx *c, char *out, size_t outlen);
int tizen_apps(proto_ctx *c, char *out, size_t outlen);

#endif
