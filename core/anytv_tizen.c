/* anytv_tizen.c - Samsung Tizen remote: ws://tv:8001/api/v2/channels/samsung.remote.control
 * The TV shows "permitir" on first use and returns an 8-char token we must keep. */
#include "anytv_remote.h"
#include "anytv_net.h"
#include "anytv_rsa.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdio.h>

#define TIZEN_CHANNEL "/api/v2/channels/samsung.remote.control"

static const char *TIZEN_KEY[ANYTV_CMD_COUNT] = {
    "KEY_UP", "KEY_DOWN", "KEY_LEFT", "KEY_RIGHT", "KEY_ENTER", "KEY_RETURN",
    "KEY_HOME", "KEY_MENU", "KEY_VOLUP", "KEY_VOLDOWN", "KEY_MUTE", "KEY_PLAY_BACK",
    "KEY_FF", "KEY_REWIND", "KEY_STOP", "KEY_REWIND", "KEY_FF", "KEY_POWER",
    "KEY_INFO", "KEY_TOOLS", "KEY_SOURCE", "KEY_CHUP", "KEY_CHDOWN",
    "KEY_RED", "KEY_GREEN", "KEY_YELLOW", "KEY_BLUE",
    "KEY_0", "KEY_1", "KEY_2", "KEY_3", "KEY_4",
    "KEY_5", "KEY_6", "KEY_7", "KEY_8", "KEY_9"
};

uint32_t tizen_caps(void) {
    /* the channel lists apps through ed.installedApp.get; launching one needs the
     * development bridge, and the MAC comes from http://host:8001/api/v2/ */
    return ANYTV_CAP_KEY | ANYTV_CAP_TEXT | ANYTV_CAP_APPS | ANYTV_CAP_LAUNCH |
           ANYTV_CAP_POWER_OFF | ANYTV_CAP_POWER_ON | ANYTV_CAP_INFO | ANYTV_CAP_COLORS;
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


/* Ultimo "token" do JSON, tolerando espaco depois dos dois-pontos (a TV manda
 * compacto, servidores de teste mandam com espaco). */
static void json_last_token(const char *json, char *out, size_t outlen) {
    out[0] = 0;
    const char *p = json;
    const char *hit;
    while ((hit = strstr(p, "\"token\"")) != NULL) {
        const char *q = hit + 7;
        while (*q == ' ' || *q == '\t') q++;
        if (*q == ':') {
            q++;
            while (*q == ' ' || *q == '\t') q++;
            if (*q == '"') {
                q++;
                const char *end = strchr(q, '"');
                if (end && (size_t)(end - q) < outlen - 1) {
                    size_t n = (size_t)(end - q);
                    memcpy(out, q, n);
                    out[n] = 0;
                }
            }
        }
        p = hit + 7;
    }
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
    /* 2017+ Tizen TVs only accept the remote channel over wss://8002; the plain
     * 8001 path answers the upgrade and then closes with "notack". A candidate
     * only counts when the TV greets us with ms.channel.connect, so we validate
     * each attempt instead of trusting the upgrade alone. */
    int cand_port[4] = {c->port, c->port, 8002, 8001};
    int cand_tls[4] = {1, 0, 1, 0};
    char reply[2048];
    for (int i = 0; i < 4; i++) {
        int dup = 0;
        for (int j = 0; j < i; j++) {
            if (cand_port[j] == cand_port[i] && cand_tls[j] == cand_tls[i]) dup = 1;
        }
        if (dup) continue;
        int attempt_timeout = cand_tls[i] ? 2500 : timeout_ms;
        int rc = ws_connect(&c->ws, c->host, cand_port[i], path, cand_tls[i],
                            attempt_timeout, err, sizeof(err));
        if (getenv("ANYTV_DEBUG")) {
            fprintf(stderr, "[tizen] tentativa porta=%d tls=%d rc=%d err=%s\n",
                    cand_port[i], cand_tls[i], rc, err);
        }
        if (rc != WS_OK) {
            if (cand_tls[i]) snprintf(c->last_error, sizeof(c->last_error), "%s", err);
            continue;
        }
        /* the greeting arrives right away when the TV already trusts us; it can
         * take up to a minute when the user still has to tap "Permitir" */
        int wait_ms = (i == 0) ? 60000 : 8000;
        int got = ws_recv_text(&c->ws, reply, sizeof(reply), wait_ms);
        if (got < 0) {
            if (getenv("ANYTV_DEBUG")) {
                fprintf(stderr, "[tizen] sem cumprimento em %d ms\n", wait_ms);
            }
            ws_close(&c->ws);
            continue;
        }
        char event[64] = "";
        anytv_json_string(reply, "event", event, sizeof(event));
        if (getenv("ANYTV_DEBUG")) {
            fprintf(stderr, "[tizen] cumprimento (%.400s)\n", reply);
        }
        if (strcmp(event, "ms.channel.connect") != 0) {
            if (getenv("ANYTV_DEBUG")) {
                fprintf(stderr, "[tizen] evento inesperado: %s (%.60s)\n", event, reply);
            }
            snprintf(c->last_error, sizeof(c->last_error), "%s",
                     event[0] ? event : "resposta sem ms.channel.connect");
            ws_close(&c->ws);
            continue;
        }
        /* O cumprimento traz DOIS tokens: o do cliente dentro de clients[]
         * (credencial ja conhecida, possivelmente velha) e o da sessao nova em
         * data.token, sempre no fim do JSON. Procurar "token" pegava o primeiro e
         * congelava um token velho, e a TV pedia "Permitir" a cada conexao
         * (medido: token atual conecta em 91 ms, token velho em 5,9 s). */
        char tok[64];
        json_last_token(reply, tok, sizeof(tok));
        if (tok[0]) snprintf(c->token, sizeof(c->token), "%s", tok);
        c->connected = 1;
        return NET_OK;
    }
    if (!c->last_error[0]) snprintf(c->last_error, sizeof(c->last_error), "%s", err);
    return NET_ERR_CONNECT;
}


/* The remote channel can list the installed apps: emitting ed.installedApp.get
 * makes the TV answer with appId/name for every app. This works without the
 * development bridge (and therefore without the host-IP restriction). */

/* The TV reports the icon as a device path that no service exposes (probed on
 * every open port: 404, echo, or silence; sdb refuses file pull). For the big
 * brands the official artwork comes from the store page instead. */
static const char *tizen_store_package(const char *app_id) {
    static const struct { const char *app_id; const char *pkg; } known[] = {
        {"11101200001", "com.netflix.ninja"},
        {"111299001912", "com.google.android.youtube.tv"},
        {"3201512006785", "com.amazon.avod.thirdpartyclient"},
        {"3201901017640", "com.disney.disneyplus"},
        {"3201512006963", "com.plexapp.android"},
        {"3201606009684", "com.spotify.tv.android"},
        {"3201808016802", "tv.pluto.android"},
        {"3201608010191", "deezer.android.app"},
        {"3202106024097", "com.apple.atve.androidtv.appletv"},
        {"3202301029760", "com.wbd.stream"},
        {"3202110025305", "com.cbs.ott"},
        {"111299001962", "com.tid.enjoy"},
        {NULL, NULL}
    };
    for (int i = 0; known[i].app_id; i++) {
        if (strcmp(known[i].app_id, app_id) == 0) return known[i].pkg;
    }
    return NULL;
}

int tizen_apps(proto_ctx *c, char *out, size_t outlen) {
    static char reply[96 * 1024];
    if (send_json(c, "{\"method\":\"ms.channel.emit\",\"params\":"
                     "{\"event\":\"ed.installedApp.get\",\"to\":\"host\"}}") != NET_OK) {
        return NET_ERR_IO;
    }
    int have = 0;
    for (int i = 0; i < 6 && !have; i++) {
        int n = ws_recv_text(&c->ws, reply, sizeof(reply), 5000);
        if (n < 0) {
            snprintf(c->last_error, sizeof(c->last_error), "sem resposta ao ed.installedApp.get");
            return n == WS_ERR_TIMEOUT ? NET_ERR_TIMEOUT : NET_ERR_IO;
        }
        if (strstr(reply, "appId")) have = 1;
    }
    if (!have) {
        snprintf(c->last_error, sizeof(c->last_error),
                 "a TV nao devolveu a lista (ed.installedApp.get)");
        return NET_ERR_PROTO;
    }

    size_t used = 0;
    const char *p = reply;
    int count = 0;
    while ((p = strstr(p, "\"appId\"")) != NULL) {
        char id[96], name[192];
        if (anytv_json_string(p, "appId", id, sizeof(id)) != 0 || !id[0]) { p += 7; continue; }
        name[0] = 0;
        const char *np = strstr(p, "\"name\"");
        if (np) anytv_json_string(np, "name", name, sizeof(name));
        char icon[224] = "";
        const char *ip = strstr(p, "\"icon\"");
        if (ip && anytv_json_string(ip, "icon", icon, sizeof(icon)) != 0) icon[0] = 0;
        if (!icon[0]) {
            const char *pkg = tizen_store_package(id);
            if (pkg) {
                snprintf(icon, sizeof(icon),
                         "https://play.google.com/store/apps/details?id=%s", pkg);
            }
        }
        int wrote = snprintf(out + used, outlen - used, "%s\t%s\t%s\n", id, name, icon);
        if (wrote < 0 || (size_t)wrote >= outlen - used) break;
        used += (size_t)wrote;
        count++;
        p += 7;
    }
    if (outlen) out[used < outlen ? used : outlen - 1] = 0;
    return count > 0 ? NET_OK : NET_ERR_PROTO;
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

/* The TV publishes model, wifi MAC and resolution on http://host:8001/api/v2/ */
int tizen_details(proto_ctx *c, char *out, size_t outlen) {
    char body[8192];
    http_response resp;
    int rc = http_request(c->host, 8001, "GET", "/api/v2/", NULL, NULL, 5000,
                          body, sizeof(body), &resp);
    if (rc != NET_OK || resp.status != 200) {
        snprintf(c->last_error, sizeof(c->last_error), "a TV nao respondeu em /api/v2/");
        return NET_ERR_IO;
    }
    struct { const char *key, *label; } fields[] = {
        {"name", "Nome"}, {"modelName", "Modelo"}, {"model", "Placa"},
        {"resolution", "Resolucao"}, {"firmwareVersion", "Firmware"},
        {"networkType", "Rede"}, {"ip", "IP"}, {"wifiMac", "MAC"},
        {"countryCode", "Pais"}, {"developerMode", "Modo desenvolvedor"},
        {"developerIP", "Host PC IP"}, {"version", "API"},
        {NULL, NULL}
    };
    size_t used = 0;
    for (int i = 0; fields[i].key; i++) {
        char value[192] = "";
        if (anytv_json_string(body, fields[i].key, value, sizeof(value)) != 0 || !value[0]) continue;
        if (strcmp(fields[i].key, "networkType") == 0) {
            if (strncmp(value, "wireless", 8) == 0) snprintf(value, sizeof(value), "Wi-Fi");
            else if (strncmp(value, "wired", 5) == 0) snprintf(value, sizeof(value), "Cabo");
        }
        if (strcmp(fields[i].key, "developerMode") == 0) {
            snprintf(value, sizeof(value), "%s", strcmp(value, "1") == 0 ? "ligado" : "desligado");
        }
        int wrote = snprintf(out + used, outlen - used, "%s\t%s\n", fields[i].label, value);
        if (wrote < 0 || (size_t)wrote >= outlen - used) break;
        used += (size_t)wrote;
    }
    if (outlen) out[used < outlen ? used : outlen - 1] = 0;
    return used > 0 ? NET_OK : NET_ERR_PROTO;
}

int tizen_info(proto_ctx *c, char *out, size_t outlen) {
    char body[8192];
    http_response resp;
    int rc = http_request(c->host, 8001, "GET", "/api/v2/", NULL, NULL, 5000,
                          body, sizeof(body), &resp);
    if (rc != NET_OK || resp.status != 200) {
        snprintf(out, outlen, "%s|Tizen|", c->name[0] ? c->name : "Samsung TV");
        return NET_OK;
    }
    char model[64] = "", mac[40] = "", name[128] = "", res[32] = "";
    anytv_json_string(body, "modelName", model, sizeof(model));
    anytv_json_string(body, "wifiMac", mac, sizeof(mac));
    anytv_json_string(body, "name", name, sizeof(name));
    anytv_json_string(body, "resolution", res, sizeof(res));
    if (mac[0]) snprintf(c->mac, sizeof(c->mac), "%s", mac);
    if (name[0]) snprintf(c->name, sizeof(c->name), "%s", name);
    snprintf(out, outlen, "%s|Tizen|%s", model[0] ? model : "Samsung TV", res);
    return NET_OK;
}
