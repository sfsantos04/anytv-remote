/* anytv_tizen.c - Samsung Tizen remote: ws://tv:8001/api/v2/channels/samsung.remote.control
 * The TV shows "permitir" on first use and returns an 8-char token we must keep. */
#include "anytv_remote.h"
#include "anytv_net.h"
#include "anytv_rsa.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define TIZEN_CHANNEL "/api/v2/channels/samsung.remote.control"

static const char *TIZEN_KEY[ANYTV_CMD_COUNT] = {
    "KEY_UP", "KEY_DOWN", "KEY_LEFT", "KEY_RIGHT", "KEY_ENTER", "KEY_RETURN",
    "KEY_HOME", "KEY_MENU", "KEY_VOLUP", "KEY_VOLDOWN", "KEY_MUTE", "KEY_PLAY",
    "KEY_FF", "KEY_REWIND", "KEY_STOP", "KEY_REWIND", "KEY_FF", "KEY_POWER",
    "KEY_INFO", "KEY_TOOLS", "KEY_SOURCE", "KEY_CHUP", "KEY_CHDOWN"
};

uint32_t tizen_caps(void) {
    /* no public app list and no app launch on this channel */
    return ANYTV_CAP_KEY | ANYTV_CAP_TEXT | ANYTV_CAP_POWER_OFF | ANYTV_CAP_INFO;
}

static void url_encode(const char *in, char *out, size_t outlen) {
    static const char *hex = "0123456789ABCDEF";
    size_t o = 0;
    for (const char *p = in; *p && o + 4 < outlen; p++) {
        unsigned char ch = (unsigned char)*p;
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            out[o++] = (char)ch;
        } else {
            out[o++] = '%';
            out[o++] = hex[ch >> 4];
            out[o++] = hex[ch & 0x0f];
        }
    }
    out[o] = 0;
}

static int send_json(proto_ctx *c, const char *json) {
    if (c->ws.fd < 0) return NET_ERR_IO;
    return ws_send_text(&c->ws, json) == WS_OK ? NET_OK : NET_ERR_IO;
}

int tizen_connect(proto_ctx *c, int timeout_ms) {
    char name_b64[128], name_enc[256], token_enc[800];
    if (anytv_base64_encode((const uint8_t *)"AnyTV", 5, name_b64, sizeof(name_b64)) < 0)
        return NET_ERR_PROTO;
    url_encode(name_b64, name_enc, sizeof(name_enc));
    url_encode(c->token, token_enc, sizeof(token_enc));

    char path[1200];
    if (c->token[0]) {
        snprintf(path, sizeof(path), "%s?name=%s&token=%s", TIZEN_CHANNEL, name_enc, token_enc);
    } else {
        snprintf(path, sizeof(path), "%s?name=%s", TIZEN_CHANNEL, name_enc);
    }

    char err[160];
    int rc = ws_connect(&c->ws, c->host, c->port, path, timeout_ms, err, sizeof(err));
    if (rc != WS_OK) {
        snprintf(c->last_error, sizeof(c->last_error), "%s", err);
        return rc == WS_ERR_TIMEOUT ? NET_ERR_TIMEOUT : NET_ERR_CONNECT;
    }

    char reply[2048];
    int got = ws_recv_text(&c->ws, reply, sizeof(reply), timeout_ms);
    if (got < 0) return got == WS_ERR_TIMEOUT ? NET_ERR_TIMEOUT : NET_ERR_IO;

    char event[64];
    if (anytv_json_string(reply, "event", event, sizeof(event)) == 0) {
        if (strcmp(event, "ms.channel.unauthorized") == 0) return NET_ERR_PROTO;
        if (strcmp(event, "ms.channel.connect") == 0) c->connected = 1;
    }
    char tok[64];
    if (anytv_json_string(reply, "token", tok, sizeof(tok)) == 0 && tok[0]) {
        snprintf(c->token, sizeof(c->token), "%s", tok);
    }
    return NET_OK;
}

int tizen_cmd(proto_ctx *c, anytv_cmd cmd) {
    if ((int)cmd < 0 || cmd >= ANYTV_CMD_COUNT) return NET_ERR_PROTO;
    char json[512];
    snprintf(json, sizeof(json),
             "{\"method\":\"ms.remote.control\",\"params\":{\"Cmd\":\"Click\","
             "\"DataOfCmd\":\"%s\",\"Option\":\"false\",\"TypeOfRemote\":\"SendRemoteKey\"}}",
             TIZEN_KEY[cmd]);
    return send_json(c, json);
}

int tizen_text(proto_ctx *c, const char *text) {
    char b64[1024];
    if (anytv_base64_encode((const uint8_t *)text, strlen(text), b64, sizeof(b64)) < 0)
        return NET_ERR_PROTO;
    char json[1400];
    snprintf(json, sizeof(json),
             "{\"method\":\"ms.remote.control\",\"params\":{\"Cmd\":\"%s\","
             "\"DataOfCmd\":\"base64\",\"TypeOfRemote\":\"SendInputString\"}}", b64);
    return send_json(c, json);
}

int tizen_power_off(proto_ctx *c) {
    return tizen_cmd(c, ANYTV_CMD_POWER);
}
