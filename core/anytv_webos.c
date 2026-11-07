/* anytv_webos.c - LG webOS: SSAP over ws://host:3000, buttons on the pointer
 * input socket, apps via com.webos.applicationManager. */
#include "anytv_remote.h"
#include "anytv_net.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *WEBOS_BUTTON[ANYTV_CMD_COUNT] = {
    "UP", "DOWN", "LEFT", "RIGHT", "ENTER", "BACK", "HOME", "MENU",
    "VOLUMEUP", "VOLUMEDOWN", "MUTE", "PLAY", "FF", "REW",
    "STOP", "REW", "FF", "POWER", "INFO", "MENU", "INPUT_HUB",
    "CHANNELUP", "CHANNELDOWN"
};

static int request_id = 0;

uint32_t webos_caps(void) {
    return ANYTV_CAP_KEY | ANYTV_CAP_TEXT | ANYTV_CAP_APPS | ANYTV_CAP_LAUNCH |
           ANYTV_CAP_POWER_OFF | ANYTV_CAP_POWER_ON | ANYTV_CAP_INFO;
}

static int json_string(const char *json, const char *key, char *out, size_t outlen) {
    return anytv_json_string(json, key, out, outlen);
}

/* Sends one SSAP request and returns the reply payload (JSON text). */
static int ssap(proto_ctx *c, const char *uri, const char *payload_json,
                char *out, size_t outlen, int timeout_ms) {
    char msg[2048];
    request_id++;
    int n;
    if (payload_json && payload_json[0]) {
        n = snprintf(msg, sizeof(msg),
                     "{\"id\":\"anytv_%d\",\"type\":\"request\",\"uri\":\"%s\",\"payload\":%s}",
                     request_id, uri, payload_json);
    } else {
        n = snprintf(msg, sizeof(msg),
                     "{\"id\":\"anytv_%d\",\"type\":\"request\",\"uri\":\"%s\"}",
                     request_id, uri);
    }
    if (n < 0 || (size_t)n >= sizeof(msg)) return NET_ERR_PROTO;
    if (ws_send_text(&c->ws, msg) != WS_OK) return NET_ERR_IO;

    for (int attempt = 0; attempt < 6; attempt++) {
        int rc = ws_recv_text(&c->ws, out, outlen, timeout_ms);
        if (rc < 0) return rc == WS_ERR_TIMEOUT ? NET_ERR_TIMEOUT : NET_ERR_IO;
        char kind[32];
        if (json_string(out, "type", kind, sizeof(kind)) != 0) continue;
        if (strcmp(kind, "error") == 0) return NET_ERR_PROTO;
        if (strcmp(kind, "response") == 0 || strcmp(kind, "registered") == 0) return NET_OK;
    }
    return NET_ERR_PROTO;
}

static int open_pointer(proto_ctx *c, int timeout_ms) {
    char reply[4096];
    if (ssap(c, "ssap://com.webos.service.networkinput/getPointerInputSocket", NULL,
             reply, sizeof(reply), timeout_ms) != NET_OK) {
        return NET_ERR_PROTO;
    }
    char socket_path[512];
    if (json_string(reply, "socketPath", socket_path, sizeof(socket_path)) != 0) {
        return NET_ERR_PROTO;
    }
    /* ws://host:3000/resources/... -> "/resources/..." */
    const char *scheme = strstr(socket_path, "://");
    const char *path = scheme ? strchr(scheme + 3, '/') : NULL;
    if (!path) return NET_ERR_PROTO;
    return ws_connect(&c->pointer, c->host, c->port, path, timeout_ms, NULL, 0) == WS_OK
           ? NET_OK : NET_ERR_IO;
}

int webos_connect(proto_ctx *c, int timeout_ms) {
    if (ws_connect(&c->ws, c->host, c->port, "/", timeout_ms, NULL, 0) != WS_OK) {
        return NET_ERR_CONNECT;
    }
    char manifest[512];
    snprintf(manifest, sizeof(manifest),
             "\"manifest\":{\"manifestVersion\":1,\"appVersion\":\"1.0\","
             "\"signed\":{\"created\":\"20240101\",\"appId\":\"com.samflix.anytv\","
             "\"vendorId\":\"com.samflix\",\"localizedAppNames\":{},\"permissions\":[]}}");

    char msg[1024];
    snprintf(msg, sizeof(msg),
             "{\"id\":\"register_0\",\"type\":\"register\",\"payload\":{"
             "\"forcePairing\":false,\"pairingType\":\"PROMPT\",\"client-key\":\"%s\",%s}}",
             c->token, manifest);
    if (ws_send_text(&c->ws, msg) != WS_OK) return NET_ERR_IO;

    char reply[4096];
    int got = ws_recv_text(&c->ws, reply, sizeof(reply), timeout_ms);
    if (got < 0) return got == WS_ERR_TIMEOUT ? NET_ERR_TIMEOUT : NET_ERR_IO;

    if (strstr(reply, "registered")) {
        char key[256];
        if (json_string(reply, "client-key", key, sizeof(key)) == 0 && key[0]) {
            snprintf(c->token, sizeof(c->token), "%s", key);
        }
    } else {
        /* 401 = needs the pairing prompt; the TV shows it and answers later */
        return NET_ERR_CONNECT;
    }
    c->connected = 1;
    if (open_pointer(c, timeout_ms) != NET_OK) {
        return NET_ERR_PROTO;
    }
    return NET_OK;
}

int webos_cmd(proto_ctx *c, anytv_cmd cmd) {
    if ((int)cmd < 0 || cmd >= ANYTV_CMD_COUNT) return NET_ERR_PROTO;
    if (c->pointer.fd < 0) return NET_ERR_IO;
    char msg[64];
    snprintf(msg, sizeof(msg), "type:button\nname:%s\n\n", WEBOS_BUTTON[cmd]);
    return ws_send_text(&c->pointer, msg) == WS_OK ? NET_OK : NET_ERR_IO;
}

int webos_text(proto_ctx *c, const char *text) {
    /* escape the minimum needed for a JSON string */
    char escaped[1024];
    size_t o = 0;
    for (const char *p = text; *p && o + 2 < sizeof(escaped); p++) {
        if (*p == '"' || *p == '\\') { escaped[o++] = '\\'; }
        escaped[o++] = *p;
    }
    escaped[o] = 0;
    char payload[1100];
    snprintf(payload, sizeof(payload), "{\"text\":\"%s\",\"replace\":0}", escaped);
    char reply[1024];
    return ssap(c, "ssap://com.webos.service.ime/insertText", payload, reply, sizeof(reply), 4000);
}

int webos_apps(proto_ctx *c, char *out, size_t outlen) {
    char reply[32768];
    int rc = ssap(c, "ssap://com.webos.applicationManager/listApps", NULL, reply, sizeof(reply), 8000);
    if (rc != NET_OK) return rc;

    size_t used = 0;
    const char *p = reply;
    while ((p = strstr(p, "\"id\"")) != NULL) {
        char id[96];
        if (anytv_json_string(p, "id", id, sizeof(id)) != 0 || !id[0]) { p += 4; continue; }
        char title[96] = {0};
        const char *t = strstr(p, "\"title\"");
        if (t) {
            if (anytv_json_string(t, "title", title, sizeof(title)) != 0) title[0] = 0;
        }
        if (!title[0]) snprintf(title, sizeof(title), "%s", id);
        int wrote = snprintf(out + used, outlen - used, "%s\t%s\n", id, title);
        if (wrote < 0 || (size_t)wrote >= outlen - used) break;
        used += (size_t)wrote;
        p += 4;
    }
    if (outlen) out[used < outlen ? used : outlen - 1] = 0;
    return used > 0 ? NET_OK : NET_ERR_PROTO;
}

int webos_launch(proto_ctx *c, const char *app_id) {
    char payload[160];
    snprintf(payload, sizeof(payload), "{\"id\":\"%s\"}", app_id);
    char reply[1024];
    return ssap(c, "ssap://system.launcher/launch", payload, reply, sizeof(reply), 6000);
}

int webos_power_off(proto_ctx *c) {
    char reply[512];
    return ssap(c, "ssap://system/turnOff", NULL, reply, sizeof(reply), 4000);
}

int webos_info(proto_ctx *c, char *out, size_t outlen) {
    char reply[2048];
    int rc = ssap(c, "ssap://com.webos.service.tv.systemproperty/getSystemInfo",
                  "{\"keys\":[\"modelName\",\"sdkVersion\",\"firmwareVersion\"]}",
                  reply, sizeof(reply), 5000);
    if (rc != NET_OK) return rc;
    char model[64] = {0}, firmware[64] = {0};
    json_string(reply, "modelName", model, sizeof(model));
    json_string(reply, "firmwareVersion", firmware, sizeof(firmware));
    snprintf(out, outlen, "%s|%s", model[0] ? model : "LG webOS", firmware);
    return NET_OK;
}
