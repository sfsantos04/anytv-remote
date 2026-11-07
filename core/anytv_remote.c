/* anytv_remote.c - names, shared context and the command map. */
#include "anytv_remote.h"
#include <string.h>
#include <stdio.h>

static const char *CMD_NAMES[ANYTV_CMD_COUNT] = {
    "UP", "DOWN", "LEFT", "RIGHT", "OK", "BACK", "HOME", "MENU",
    "VOL_UP", "VOL_DOWN", "MUTE", "PLAY_PAUSE", "NEXT", "PREV",
    "STOP", "REWIND", "FORWARD", "POWER", "INFO", "SETTINGS", "INPUT",
    "CH_UP", "CH_DOWN", "RED", "GREEN", "YELLOW", "BLUE",
    "NUM_0", "NUM_1", "NUM_2", "NUM_3", "NUM_4",
    "NUM_5", "NUM_6", "NUM_7", "NUM_8", "NUM_9"
};

const char *anytv_cmd_name(anytv_cmd cmd) {
    if ((int)cmd < 0 || cmd >= ANYTV_CMD_COUNT) return "?";
    return CMD_NAMES[cmd];
}

int anytv_cmd_from_name(const char *name) {
    if (!name) return -1;
    for (int i = 0; i < (int)ANYTV_CMD_COUNT; i++) {
        if (strcmp(CMD_NAMES[i], name) == 0) return i;
    }
    return -1;
}

void proto_ctx_init(proto_ctx *c, const char *host, int port) {
    memset(c, 0, sizeof(*c));
    c->ws.fd = -1;
    c->pointer.fd = -1;
    c->port = port;
    if (host) snprintf(c->host, sizeof(c->host), "%s", host);
}

int anytv_json_string(const char *json, const char *key, char *out, size_t outlen) {
    if (!json || !key || !out || outlen == 0) return -1;
    char pattern[96];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (!p) { out[0] = 0; return -1; }
    p += strlen(pattern);
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    if (*p != ':') { out[0] = 0; return -1; }
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    if (*p != '"') { out[0] = 0; return -1; }
    p++;
    size_t o = 0;
    while (*p && *p != '"' && o + 1 < outlen) {
        if (*p == '\\' && p[1]) p++;
        out[o++] = *p++;
    }
    out[o] = 0;
    return 0;
}
