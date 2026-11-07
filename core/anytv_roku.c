/* anytv_roku.c - Roku ECP (port 8060): HTTP, no pairing, works on Roku TVs
 * (TCL/Hisense/Sharp Roku TV, Streambars) and Roku players. */
#include "anytv_remote.h"
#include "anytv_net.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *ROKU_KEY[ANYTV_CMD_COUNT] = {
    "Up", "Down", "Left", "Right", "Select", "Back", "Home", "Info",
    "VolumeUp", "VolumeDown", "VolumeMute", "Play", "Fwd", "Rev",
    "Play", "Rev", "Fwd", "Power", "Info", "Info", "InputHDMI1",
    "ChannelUp", "ChannelDown", "Info", "Info", "Info", "Info",
    "Info", "Info", "Info", "Info", "Info",
    "Info", "Info", "Info", "Info", "Info"
};

uint32_t roku_caps(void) {
    return ANYTV_CAP_KEY | ANYTV_CAP_TEXT | ANYTV_CAP_APPS | ANYTV_CAP_LAUNCH |
           ANYTV_CAP_POWER_OFF | ANYTV_CAP_POWER_ON | ANYTV_CAP_INFO;
}

/* Decodes the five XML entities Roku actually emits. */
static void xml_unescape(const char *in, char *out, size_t outlen) {
    size_t o = 0;
    for (const char *p = in; *p && o + 1 < outlen;) {
        if (*p == '&') {
            if (!strncmp(p, "&amp;", 5)) { out[o++] = '&'; p += 5; continue; }
            if (!strncmp(p, "&lt;", 4)) { out[o++] = '<'; p += 4; continue; }
            if (!strncmp(p, "&gt;", 4)) { out[o++] = '>'; p += 4; continue; }
            if (!strncmp(p, "&quot;", 6)) { out[o++] = '"'; p += 6; continue; }
            if (!strncmp(p, "&apos;", 6)) { out[o++] = '\''; p += 6; continue; }
            if (!strncmp(p, "&#39;", 5)) { out[o++] = '\''; p += 5; continue; }
        }
        out[o++] = *p++;
    }
    out[o] = 0;
}

static void xml_tag(const char *xml, const char *tag, char *out, size_t outlen) {
    char open[64], close[64];
    snprintf(open, sizeof(open), "<%s>", tag);
    snprintf(close, sizeof(close), "</%s>", tag);
    const char *start = strstr(xml, open);
    if (!start) { if (outlen) out[0] = 0; return; }
    start += strlen(open);
    const char *end = strstr(start, close);
    if (!end) { if (outlen) out[0] = 0; return; }
    size_t len = (size_t)(end - start);
    char raw[256];
    if (len >= sizeof(raw)) len = sizeof(raw) - 1;
    memcpy(raw, start, len);
    raw[len] = 0;
    xml_unescape(raw, out, outlen);
}

int roku_connect(proto_ctx *c, int timeout_ms) {
    char body[4096];
    http_response resp;
    int rc = http_request(c->host, c->port, "GET", "/query/device-info", NULL, NULL,
                          timeout_ms, body, sizeof(body), &resp);
    if (rc != NET_OK) return rc;
    if (resp.status != 200) return NET_ERR_PROTO;
    xml_tag(body, "friendly-device-name", c->name, sizeof(c->name));
    char mac[32];
    xml_tag(body, "wifi-mac", mac, sizeof(mac));
    if (!mac[0]) xml_tag(body, "ethernet-mac", mac, sizeof(mac));
    if (mac[0]) snprintf(c->mac, sizeof(c->mac), "%s", mac);
    c->connected = 1;
    return NET_OK;
}

int roku_cmd(proto_ctx *c, anytv_cmd cmd) {
    if ((int)cmd < 0 || cmd >= ANYTV_CMD_COUNT) return NET_ERR_PROTO;
    char path[64];
    snprintf(path, sizeof(path), "/keypress/%s", ROKU_KEY[cmd]);
    http_response resp;
    int rc = http_request(c->host, c->port, "POST", path, NULL, NULL, 3000, NULL, 0, &resp);
    return rc == NET_OK && resp.status == 200 ? NET_OK : NET_ERR_PROTO;
}

int roku_text(proto_ctx *c, const char *text) {
    for (const char *p = text; *p; p++) {
        char encoded[16];
        size_t o = 0;
        unsigned char ch = (unsigned char)*p;
        if (ch >= 0x20 && ch < 0x7f && ch != '%' && ch != ' ' && ch != '&' && ch != '?') {
            encoded[o++] = (char)ch;
        } else {
            snprintf(encoded + o, sizeof(encoded) - o, "%%%02X", ch);
            o += 3;
        }
        encoded[o] = 0;
        char path[64];
        snprintf(path, sizeof(path), "/keypress/Lit_%s", encoded);
        http_response resp;
        if (http_request(c->host, c->port, "POST", path, NULL, NULL, 3000, NULL, 0, &resp) != NET_OK)
            return NET_ERR_IO;
    }
    return NET_OK;
}

int roku_apps(proto_ctx *c, char *out, size_t outlen) {
    char body[16384];
    http_response resp;
    int rc = http_request(c->host, c->port, "GET", "/query/apps", NULL, NULL, 5000,
                          body, sizeof(body), &resp);
    if (rc != NET_OK || resp.status != 200) return NET_ERR_PROTO;

    size_t used = 0;
    const char *p = body;
    while ((p = strstr(p, "<app ")) != NULL) {
        const char *id_attr = strstr(p, "id=\"");
        const char *gt = strchr(p, '>');
        if (!id_attr || !gt || id_attr > gt) { p = gt ? gt : p + 5; continue; }
        id_attr += 4;
        const char *id_end = strchr(id_attr, '"');
        if (!id_end) break;
        char id[32];
        size_t idlen = (size_t)(id_end - id_attr);
        if (idlen >= sizeof(id)) idlen = sizeof(id) - 1;
        memcpy(id, id_attr, idlen);
        id[idlen] = 0;

        const char *name_start = gt + 1;
        const char *name_end = strstr(name_start, "</app>");
        if (!name_end) break;
        char raw[128];
        size_t nlen = (size_t)(name_end - name_start);
        if (nlen >= sizeof(raw)) nlen = sizeof(raw) - 1;
        memcpy(raw, name_start, nlen);
        raw[nlen] = 0;
        char name[128];
        xml_unescape(raw, name, sizeof(name));

        /* Roku serves each app icon itself, so the tile gets the real artwork
         * without any cloud lookup: /query/icon/<id> is a PNG. */
        int wrote = snprintf(out + used, outlen - used, "%s\t%s\thttp://%s:%d/query/icon/%s\n",
                             id, name, c->host, c->port, id);
        if (wrote < 0 || (size_t)wrote >= outlen - used) break;
        used += (size_t)wrote;
        p = name_end + 6;
    }
    if (outlen) out[used < outlen ? used : outlen - 1] = 0;
    return used > 0 ? NET_OK : NET_ERR_PROTO;
}

int roku_launch(proto_ctx *c, const char *app_id) {
    char path[96];
    snprintf(path, sizeof(path), "/launch/%s", app_id);
    http_response resp;
    int rc = http_request(c->host, c->port, "POST", path, NULL, NULL, 5000, NULL, 0, &resp);
    return rc == NET_OK && resp.status == 200 ? NET_OK : NET_ERR_PROTO;
}

int roku_info(proto_ctx *c, char *out, size_t outlen) {
    char body[4096];
    http_response resp;
    int rc = http_request(c->host, c->port, "GET", "/query/device-info", NULL, NULL,
                          5000, body, sizeof(body), &resp);
    if (rc != NET_OK || resp.status != 200) return NET_ERR_PROTO;
    char model[64], name[64], serial[64];
    xml_tag(body, "model-name", model, sizeof(model));
    xml_tag(body, "friendly-device-name", name, sizeof(name));
    xml_tag(body, "serial-number", serial, sizeof(serial));
    snprintf(out, outlen, "%s|%s|%s", name, model, serial);
    return NET_OK;
}
