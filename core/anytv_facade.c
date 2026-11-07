/* anytv_facade.c - dispatches the canonical commands to the right transport. */
#define _POSIX_C_SOURCE 200809L
#include "anytv_facade.h"
#include "anytv_adb.h"
#include "anytv_keys.h"
#include "anytv_net.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

struct anytv_client {
    int proto;
    adb_conn conn;          /* ADB transport */
    rsa_key key;
    int have_key;
    proto_ctx ctx;          /* Roku / webOS / Tizen */
    int connected;
    uint32_t caps;
    char error[256];
    /* scrcpy mode: the stream carries 12-byte frame headers instead of raw
     * Annex-B, so the reader strips them before handing bytes back */
    int scrcpy;
    uint8_t sc_hdr[12];
    size_t sc_hdr_len;
    uint32_t sc_remaining;
    int sc_meta_done;
    int sc_width;
    int sc_height;
    char sc_device[64];
    uint8_t sc_raw[64 * 1024];
    size_t sc_raw_len;
    size_t sc_raw_off;
    /* Tizen dev bridge (sdb) on port 26101: it is what exposes the app list,
     * because the remote-control channel does not */
    adb_conn sdb;
    int sdb_open;
    int sdb_ready;
};

static const char *PROTO_NAMES[] = { "adb", "roku", "webos", "tizen" };

static int tizen_sdb_ready(anytv_client *c);
static int tizen_sdb_launch(anytv_client *c, const char *app_id);

static uint32_t caps_for(int proto) {
    switch (proto) {
    case ANYTV_PROTO_ROKU: return roku_caps();
    case ANYTV_PROTO_WEBOS: return webos_caps();
    case ANYTV_PROTO_TIZEN: return tizen_caps();
    default:
        return ANYTV_CAP_KEY | ANYTV_CAP_TEXT | ANYTV_CAP_APPS | ANYTV_CAP_LAUNCH |
               ANYTV_CAP_SCREEN | ANYTV_CAP_POWER_OFF | ANYTV_CAP_POWER_ON | ANYTV_CAP_INFO;
    }
}

anytv_client *anytv_client_new_proto(int proto) {
    anytv_client *c = (anytv_client *)calloc(1, sizeof(anytv_client));
    if (!c) return NULL;
    c->proto = proto;
    c->conn.fd = -1;
    c->ctx.ws.fd = -1;
    c->ctx.pointer.fd = -1;
    c->caps = caps_for(proto);
    return c;
}

anytv_client *anytv_client_new(void) { return anytv_client_new_proto(ANYTV_PROTO_ADB); }

void anytv_client_free(anytv_client *c) {
    if (!c) return;
    anytv_client_disconnect(c);
    free(c);
}

int anytv_client_keygen(anytv_client *c) {
    if (!c) return ADB_ERR_PROTOCOL;
    if (rsa_generate(&c->key) != 0) {
        snprintf(c->error, sizeof(c->error), "falha ao gerar a chave RSA");
        return ADB_ERR_PROTOCOL;
    }
    c->have_key = 1;
    return 0;
}

int anytv_client_key_load(anytv_client *c, const uint8_t *blob, size_t len) {
    if (!c || !blob) return ADB_ERR_PROTOCOL;
    if (rsa_import(&c->key, blob, len) != 0) {
        snprintf(c->error, sizeof(c->error), "chave salva invalida");
        return ADB_ERR_PROTOCOL;
    }
    c->have_key = 1;
    return 0;
}

int anytv_client_key_export(const anytv_client *c, uint8_t *out, size_t len) {
    if (!c || !out || len < ANYTV_KEY_SIZE) return -1;
    return rsa_export(&c->key, out, len) == ANYTV_KEY_SIZE ? 0 : -1;
}

int anytv_client_connect(anytv_client *c, const char *host, int port,
                         int timeout_ms, int auth_wait_ms) {
    if (!c || !host) return ADB_ERR_PROTOCOL;
    anytv_client_disconnect(c);
    snprintf(c->ctx.host, sizeof(c->ctx.host), "%s", host);
    c->ctx.port = port;

    switch (c->proto) {
    case ANYTV_PROTO_ROKU: {
        int rc = roku_connect(&c->ctx, timeout_ms);
        if (rc != NET_OK) {
            snprintf(c->error, sizeof(c->error), "Roku ECP sem resposta em %s:%d", host, port);
            return ADB_ERR_IO;
        }
        c->connected = 1;
        c->error[0] = 0;
        return 0;
    }
    case ANYTV_PROTO_WEBOS: {
        int rc = webos_connect(&c->ctx, timeout_ms);
        if (rc != NET_OK) {
            if (c->ctx.last_error[0]) {
                snprintf(c->error, sizeof(c->error), "LG: %s", c->ctx.last_error);
            } else {
                snprintf(c->error, sizeof(c->error),
                         rc == NET_ERR_TIMEOUT
                         ? "TV LG nao respondeu: aceite o pedido de pareamento na tela"
                         : "LG webOS recusou a conexao: confira 'LG Connect Apps' e a rede");
            }
            return ADB_ERR_IO;
        }
        c->connected = 1;
        c->error[0] = 0;
        return 0;
    }
    case ANYTV_PROTO_TIZEN: {
        int rc = tizen_connect(&c->ctx, timeout_ms);
        if (rc != NET_OK && c->ctx.mac[0]) {
            /* A sleeping Samsung closes the remote channel; wake it and retry once
             * instead of failing straight away. */
            net_wol(c->ctx.mac, 9);
            struct timespec pause = { .tv_sec = 6, .tv_nsec = 0 };
            nanosleep(&pause, NULL);
            rc = tizen_connect(&c->ctx, timeout_ms);
        }
        if (rc != NET_OK) {
            /* The remote channel dies when the TV sleeps; the development bridge
             * keeps answering, so apps and launching still work. */
            if (tizen_sdb_ready(c) == ADB_OK) {
                c->connected = 1;
                c->caps = ANYTV_CAP_APPS | ANYTV_CAP_LAUNCH | ANYTV_CAP_INFO;
                snprintf(c->error, sizeof(c->error),
                         "canal remoto fora; usando o sdb (apps e abrir app)");
                return 0;
            }
            if (c->ctx.last_error[0]) {
                snprintf(c->error, sizeof(c->error), "Samsung: %s", c->ctx.last_error);
            } else {
                snprintf(c->error, sizeof(c->error),
                         rc == NET_ERR_TIMEOUT
                         ? "TV Samsung nao respondeu: aceite 'Permitir' na tela"
                         : "Samsung nao aceitou: menu > Geral > Rede > Estado do dispositivo");
            }
            return ADB_ERR_IO;
        }
        c->connected = 1;
        /* the app list lives on the development bridge, not on the remote channel */
        if (tizen_sdb_ready(c) == ADB_OK) {
            c->caps |= ANYTV_CAP_APPS | ANYTV_CAP_LAUNCH;
        }
        c->error[0] = 0;
        return 0;
    }
    default: {
        if (!c->have_key) {
            int rc = anytv_client_keygen(c);
            if (rc != 0) return rc;
        }
        int rc = adb_connect_ex(&c->conn, host, port, &c->key, timeout_ms, auth_wait_ms);
        if (rc != ADB_OK) {
            snprintf(c->error, sizeof(c->error), "%s", c->conn.last_error_text);
            return rc;
        }
        c->connected = 1;
        c->error[0] = 0;
        return 0;
    }
    }
}

void anytv_client_disconnect(anytv_client *c) {
    if (!c) return;
    if (c->conn.fd >= 0) adb_close(&c->conn);
    if (c->ctx.ws.fd >= 0) ws_close(&c->ctx.ws);
    if (c->ctx.pointer.fd >= 0) ws_close(&c->ctx.pointer);
    if (c->sdb_open) {
        adb_close(&c->sdb);
        c->sdb_open = 0;
    }
    c->sdb_ready = 0;
    c->ctx.ws.fd = -1;
    c->ctx.pointer.fd = -1;
    c->connected = 0;
}

int anytv_client_connected(const anytv_client *c) { return c ? c->connected : 0; }
uint32_t anytv_client_caps(const anytv_client *c) { return c ? c->caps : 0; }
int anytv_client_proto(const anytv_client *c) { return c ? c->proto : 0; }

const char *anytv_client_proto_name(const anytv_client *c) {
    if (!c || c->proto < 0 || c->proto > 3) return "?";
    return PROTO_NAMES[c->proto];
}

const char *anytv_client_model(const anytv_client *c) {
    if (!c) return "";
    if (c->proto == ANYTV_PROTO_ADB) return c->conn.device_model;
    return c->ctx.name;
}

const char *anytv_client_android(const anytv_client *c) {
    if (!c) return "";
    if (c->proto == ANYTV_PROTO_ADB) return c->conn.device_version;
    return "";
}

const char *anytv_client_error(const anytv_client *c) { return c ? c->error : ""; }
const char *anytv_client_token(const anytv_client *c) { return c ? c->ctx.token : ""; }
const char *anytv_client_mac(const anytv_client *c) { return c ? c->ctx.mac : ""; }

void anytv_client_set_token(anytv_client *c, const char *token) {
    if (!c || !token) return;
    snprintf(c->ctx.token, sizeof(c->ctx.token), "%s", token);
}

void anytv_client_set_mac(anytv_client *c, const char *mac) {
    if (!c || !mac) return;
    snprintf(c->ctx.mac, sizeof(c->ctx.mac), "%s", mac);
}

static int adb_run(anytv_client *c, const char *cmd, int timeout_ms) {
    if (!c || !c->connected) {
        if (c) snprintf(c->error, sizeof(c->error), "nao conectado");
        return ADB_ERR_IO;
    }
    char *tmp = (char *)malloc(4096);
    if (!tmp) return ADB_ERR_IO;
    int code = adb_shell(&c->conn, cmd, tmp, 4096, timeout_ms);
    free(tmp);
    if (code < 0) snprintf(c->error, sizeof(c->error), "%s", c->conn.last_error_text);
    return code < 0 ? code : 0;
}

static int adb_cmd(anytv_client *c, anytv_cmd cmd) {
    static const int ADB_CODES[ANYTV_CMD_COUNT] = {
        19, 20, 21, 22, 23, 4, 3, 82,
        24, 25, 91, 85, 87, 88,
        86, 89, 90, 26, 165, 176, 178, 166, 167
    };
    if ((int)cmd < 0 || cmd >= ANYTV_CMD_COUNT) return ADB_ERR_PROTOCOL;
    char line[128];
    anytv_keyevent_command(ADB_CODES[cmd], 1, line, sizeof(line));
    return adb_run(c, line, 8000);
}

int anytv_client_cmd(anytv_client *c, int cmd, int repeat) {
    if (!c) return ADB_ERR_PROTOCOL;
    if (cmd < 0 || cmd >= (int)ANYTV_CMD_COUNT) return ADB_ERR_PROTOCOL;
    int times = repeat < 1 ? 1 : repeat;
    int rc = ADB_ERR_IO;
    switch (c->proto) {
    case ANYTV_PROTO_ROKU:
        for (int i = 0; i < times; i++) rc = roku_cmd(&c->ctx, (anytv_cmd)cmd);
        if (rc != NET_OK) snprintf(c->error, sizeof(c->error), "Roku recusou o comando");
        return rc == NET_OK ? 0 : ADB_ERR_IO;
    case ANYTV_PROTO_WEBOS:
        for (int i = 0; i < times; i++) rc = webos_cmd(&c->ctx, (anytv_cmd)cmd);
        if (rc != NET_OK) snprintf(c->error, sizeof(c->error), "LG recusou o comando");
        return rc == NET_OK ? 0 : ADB_ERR_IO;
    case ANYTV_PROTO_TIZEN:
        for (int i = 0; i < times; i++) rc = tizen_cmd(&c->ctx, (anytv_cmd)cmd);
        if (rc != NET_OK) snprintf(c->error, sizeof(c->error), "Samsung recusou o comando");
        return rc == NET_OK ? 0 : ADB_ERR_IO;
    default:
        return adb_cmd(c, (anytv_cmd)cmd);
    }
}

int anytv_client_key(anytv_client *c, int code, int repeat) {
    if (!c) return ADB_ERR_PROTOCOL;
    if (c->proto != ANYTV_PROTO_ADB) {
        snprintf(c->error, sizeof(c->error), "keycodes so existem no ADB");
        return ADB_ERR_PROTOCOL;
    }
    char cmd[256];
    anytv_keyevent_command(code, repeat, cmd, sizeof(cmd));
    return adb_run(c, cmd, 8000);
}

int anytv_client_text(anytv_client *c, const char *utf8) {
    if (!c) return ADB_ERR_PROTOCOL;
    int rc = NET_ERR_PROTO;
    switch (c->proto) {
    case ANYTV_PROTO_ROKU: rc = roku_text(&c->ctx, utf8); break;
    case ANYTV_PROTO_WEBOS: rc = webos_text(&c->ctx, utf8); break;
    case ANYTV_PROTO_TIZEN: rc = tizen_text(&c->ctx, utf8); break;
    default: {
        char cmd[4096];
        if (anytv_input_text_command(utf8, cmd, sizeof(cmd)) < 0) return ADB_ERR_PROTOCOL;
        return adb_run(c, cmd, 8000);
    }
    }
    if (rc != NET_OK) snprintf(c->error, sizeof(c->error), "falha ao enviar texto");
    return rc == NET_OK ? 0 : ADB_ERR_IO;
}

int anytv_client_shell(anytv_client *c, const char *command, char *out, size_t outlen,
                       int timeout_ms) {
    if (!c || c->proto != ANYTV_PROTO_ADB) {
        if (c) snprintf(c->error, sizeof(c->error), "shell so existe no ADB");
        return ADB_ERR_IO;
    }
    if (!c->connected) {
        snprintf(c->error, sizeof(c->error), "nao conectado");
        return ADB_ERR_IO;
    }
    int code = adb_shell(&c->conn, command, out, outlen, timeout_ms);
    if (code < 0) {
        snprintf(c->error, sizeof(c->error), "%s", c->conn.last_error_text);
        return code;
    }
    return code;
}

static int anytv_generic_segment(const char *seg, size_t len) {
    static const char *skip[] = {"tv", "app", "apps", "pt", "br", "com", "net", "org",
                                 "mobile", "android", "phone", "tablet", "ninja",
                                 "free", "pro", "hd", "lite", "beta", "client", NULL};
    for (int i = 0; skip[i]; i++) {
        size_t sl = strlen(skip[i]);
        if (sl != len) continue;
        size_t k = 0;
        while (k < len) {
            char a = seg[k], b = skip[i][k];
            if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
            if (a != b) break;
            k++;
        }
        if (k == len) return 1;
    }
    return 0;
}

/* Package ids carry no label, so the readable name comes from the best segment:
 * com.google.android.youtube.tv -> Youtube, com.tid.enjoy -> Enjoy. */
static void anytv_pretty_label(const char *pkg, char *out, size_t outlen) {
    const char *seg[10];
    size_t seg_len[10];
    int count = 0;
    const char *p = pkg;
    while (*p && count < 10) {
        const char *dot = strchr(p, '.');
        size_t len = dot ? (size_t)(dot - p) : strlen(p);
        if (len) { seg[count] = p; seg_len[count] = len; count++; }
        if (!dot) break;
        p = dot + 1;
    }
    const char *chosen = pkg;
    size_t chosen_len = strlen(pkg);
    for (int i = count - 1; i >= 0; i--) {
        if (seg_len[i] >= 2 && !anytv_generic_segment(seg[i], seg_len[i])) {
            chosen = seg[i];
            chosen_len = seg_len[i];
            break;
        }
    }
    size_t i = 0;
    for (; i < chosen_len && i + 1 < outlen; i++) {
        char ch = chosen[i];
        if (i == 0 && ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);
        out[i] = ch;
    }
    out[i] = 0;
}

/* ---- Tizen dev bridge (sdb, port 26101) ----------------------------------
 * The remote channel has no app list, but the development bridge does. It only
 * accepts a whitelist of shell verbs: "0 vd_applist" dumps key=value pairs per
 * app and "0 execute <appid>" launches one. */
static int tizen_sdb_ready(anytv_client *c) {
    if (c->sdb_ready) return ADB_OK;
    if (c->sdb_open) {
        adb_close(&c->sdb);
        c->sdb_open = 0;
    }
    memset(&c->sdb, 0, sizeof(c->sdb));
    c->sdb.fd = -1;
    int rc = adb_connect(&c->sdb, c->ctx.host, 26101, NULL, 6000);
    if (rc != ADB_OK) {
        snprintf(c->error, sizeof(c->error), "sdb: %.160s", c->sdb.last_error_text);
        return rc;
    }
    c->sdb_open = 1;
    c->sdb_ready = 1;
    return ADB_OK;
}

static int tizen_sdb_launch(anytv_client *c, const char *app_id) {
    if (tizen_sdb_ready(c) != ADB_OK) return ADB_ERR_IO;
    char svc[256];
    snprintf(svc, sizeof(svc), "shell:0 execute %s 0", app_id);
    char sink[512];
    if (adb_service_capture(&c->sdb, svc, sink, sizeof(sink), 12000) != 0) {
        c->sdb_ready = 0;
        snprintf(c->error, sizeof(c->error), "abrir app precisa do modo desenvolvedor com o IP deste iPhone (TV: Apps > 123 > 12345)");
        return ADB_ERR_IO;
    }
    return ADB_OK;
}

int anytv_client_apps(anytv_client *c, char *out, size_t outlen) {
    if (!c) return ADB_ERR_IO;
    int rc;
    switch (c->proto) {
    case ANYTV_PROTO_ROKU: rc = roku_apps(&c->ctx, out, outlen); break;
    case ANYTV_PROTO_WEBOS: rc = webos_apps(&c->ctx, out, outlen); break;
    case ANYTV_PROTO_TIZEN:
        /* remote channel only: it needs no dev-mode host and is the path the
         * Samsung app itself uses, so the sdb connection stays out of this call */
        rc = tizen_apps(&c->ctx, out, outlen);
        break;
    default: {
        if (!c->connected) return ADB_ERR_IO;
        /* `cmd package query-activities --brief` only exists on newer Android and
         * older builds answer "No activities found" with exit 0; pm list packages
         * works from Android 4 to 15, so it is the primary source. */
        char raw[32768];
        int code = adb_shell(&c->conn, "pm list packages -3", raw, sizeof(raw), 20000);
        if (code < 0) {
            snprintf(c->error, sizeof(c->error), "%s", c->conn.last_error_text);
            return code;
        }
        if (raw[0] == 0) {
            code = adb_shell(&c->conn, "pm list packages", raw, sizeof(raw), 20000);
            if (code < 0) {
                snprintf(c->error, sizeof(c->error), "%s", c->conn.last_error_text);
                return code;
            }
        }
        /* The device only hands out package ids, so the tile shows a readable
         * name and the official icon is resolved from the Play page by the app. */
        size_t used = 0;
        const char *line = raw;
        while (*line) {
            const char *eol = strchr(line, '\n');
            size_t len = eol ? (size_t)(eol - line) : strlen(line);
            while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == ' ')) len--;
            const char *pkg = line;
            size_t plen = len;
            if (plen > 8 && strncmp(pkg, "package:", 8) == 0) { pkg += 8; plen -= 8; }
            if (plen == 0 || memchr(pkg, ' ', plen)) {
                if (!eol) break;
                line = eol + 1;
                continue;
            }
            char id[128];
            size_t cl = plen < sizeof(id) - 1 ? plen : sizeof(id) - 1;
            memcpy(id, pkg, cl);
            id[cl] = 0;
            char name[96];
            anytv_pretty_label(id, name, sizeof(name));
            int wrote = snprintf(out + used, outlen - used,
                                 "%s\t%s\thttps://play.google.com/store/apps/details?id=%s\n",
                                 id, name, id);
            if (wrote < 0 || (size_t)wrote >= outlen - used) break;
            used += (size_t)wrote;
            if (!eol) break;
            line = eol + 1;
        }
        if (outlen) out[used < outlen ? used : outlen - 1] = 0;
        return used > 0 ? 0 : ADB_ERR_PROTOCOL;
    }
    }
    if (rc != NET_OK && !c->error[0]) snprintf(c->error, sizeof(c->error), "falha ao listar apps");
    return rc == NET_OK ? 0 : ADB_ERR_IO;
}

int anytv_client_launch(anytv_client *c, const char *app_id) {
    if (!c) return ADB_ERR_PROTOCOL;
    int rc;
    switch (c->proto) {
    case ANYTV_PROTO_ROKU: rc = roku_launch(&c->ctx, app_id); break;
    case ANYTV_PROTO_WEBOS: rc = webos_launch(&c->ctx, app_id); break;
    case ANYTV_PROTO_TIZEN:
        rc = tizen_sdb_launch(c, app_id) == ADB_OK ? NET_OK : NET_ERR_PROTO;
        break;
    default: {
        char cmd[512];
        anytv_launch_command(app_id, cmd, sizeof(cmd));
        return adb_run(c, cmd, 15000);
    }
    }
    if (rc != NET_OK && !c->error[0]) snprintf(c->error, sizeof(c->error), "falha ao abrir o app");
    return rc == NET_OK ? 0 : ADB_ERR_IO;
}

static uint32_t anytv_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

/* PNG and WebP dimensions, enough to reject splash screens and banners. */
static int anytv_image_dims(const uint8_t *d, size_t n, int *w, int *h) {
    if (n >= 24 && d[0] == 0x89 && d[1] == 'P' && d[2] == 'N' && d[3] == 'G') {
        uint32_t ww = anytv_be32(d + 16), hh = anytv_be32(d + 20);
        if (!ww || !hh || ww > 4096 || hh > 4096) return 0;
        *w = (int)ww; *h = (int)hh;
        return 1;
    }
    if (n >= 30 && memcmp(d, "RIFF", 4) == 0 && memcmp(d + 8, "WEBP", 4) == 0 &&
        memcmp(d + 12, "VP8X", 4) == 0) {
        *w = (int)((uint32_t)d[24] | ((uint32_t)d[25] << 8) | ((uint32_t)d[26] << 16)) + 1;
        *h = (int)((uint32_t)d[27] | ((uint32_t)d[28] << 8) | ((uint32_t)d[29] << 16)) + 1;
        return 1;
    }
    return 0;
}

/* Extract a single entry inside the device with busybox and pull only that file. */
static int anytv_pull_apk_entry(anytv_client *c, const char *apk, const char *entry,
                                uint8_t *out, size_t outlen) {
    char cmd[768];
    char raw[512];
    snprintf(cmd, sizeof(cmd),
             "busybox unzip -p '%s' '%s' > /data/local/tmp/.anytv_icon 2>/dev/null", apk, entry);
    if (adb_shell(&c->conn, cmd, raw, sizeof(raw), 25000) < 0) return ADB_ERR_IO;
    int got = adb_pull(&c->conn, "/data/local/tmp/.anytv_icon", out, outlen, 20000);
    adb_shell(&c->conn, "rm -f /data/local/tmp/.anytv_icon", raw, sizeof(raw), 8000);
    return got;
}

/* Icons for sideloaded APKs: the TV itself extracts the PNG out of the APK with
 * busybox, so the phone only pulls a few dozen kilobytes instead of a 200 MB
 * file. Returns the number of image bytes copied into out, or a negative error. */
int anytv_client_app_icon(anytv_client *c, const char *app_id, uint8_t *out, size_t outlen) {
    if (!c || c->proto != ANYTV_PROTO_ADB || !c->connected) {
        if (c) snprintf(c->error, sizeof(c->error), "icone do APK so existe no ADB");
        return ADB_ERR_IO;
    }
    char cmd[768];
    char raw[8192];

    snprintf(cmd, sizeof(cmd), "pm path %s", app_id);
    if (adb_shell(&c->conn, cmd, raw, sizeof(raw), 15000) < 0) {
        snprintf(c->error, sizeof(c->error), "%s", c->conn.last_error_text);
        return ADB_ERR_IO;
    }
    char *tag = strstr(raw, "package:");
    if (!tag) {
        snprintf(c->error, sizeof(c->error), "app sem APK acessivel");
        return ADB_ERR_PROTOCOL;
    }
    tag += 8;
    char apk[300];
    size_t apk_len = 0;
    while (*tag && *tag != '\n' && *tag != '\r' && apk_len + 1 < sizeof(apk)) apk[apk_len++] = *tag++;
    apk[apk_len] = 0;
    if (apk_len < 4) {
        snprintf(c->error, sizeof(c->error), "caminho do APK vazio");
        return ADB_ERR_PROTOCOL;
    }

    snprintf(cmd, sizeof(cmd),
             "busybox unzip -l '%s' 2>/dev/null | grep -Ei '(launcher|app_icon|ic_app|appicon)' "
             "| grep -Ei '\\.(png|webp)$'", apk);
    if (adb_shell(&c->conn, cmd, raw, sizeof(raw), 20000) < 0) {
        snprintf(c->error, sizeof(c->error), "%s", c->conn.last_error_text);
        return ADB_ERR_IO;
    }
    if (!raw[0]) raw[0] = 0; /* no named launcher entry: the fallback below runs */

    char best[256] = {0};
    int best_score = -1;
    const char *line = raw;
    while (*line) {
        const char *eol = strchr(line, '\n');
        size_t len = eol ? (size_t)(eol - line) : strlen(line);
        const char *name_end = line + len;
        while (name_end > line && (name_end[-1] == '\r' || name_end[-1] == ' ')) name_end--;
        const char *name_start = name_end;
        while (name_start > line && name_start[-1] != ' ') name_start--;
        size_t nlen = (size_t)(name_end - name_start);
        if (nlen > 4 && nlen < sizeof(best)) {
            char candidate[256];
            memcpy(candidate, name_start, nlen);
            candidate[nlen] = 0;
            int score = 0;
            if (strstr(candidate, "xxxhdpi")) score += 50;
            else if (strstr(candidate, "xxhdpi")) score += 40;
            else if (strstr(candidate, "xhdpi")) score += 30;
            else if (strstr(candidate, "hdpi")) score += 20;
            else if (strstr(candidate, "mdpi")) score += 10;
            size_t cl = strlen(candidate);
            if (cl > 4 && strcmp(candidate + cl - 4, ".png") == 0) score += 5;
            if (strstr(candidate, "foreground") || strstr(candidate, "forground") ||
                strstr(candidate, "background")) score -= 40;
            if (score > best_score) {
                best_score = score;
                snprintf(best, sizeof(best), "%s", candidate);
            }
        }
        if (!eol) break;
        line = eol + 1;
    }

    /* First attempt: an entry whose name says it is the launcher icon. */
    int got = 0;
    if (best[0]) {
        got = anytv_pull_apk_entry(c, apk, best, out, outlen);
        int w = 0, h = 0;
        if (got > 0 && anytv_image_dims(out, (size_t)got, &w, &h) &&
            (long)got * 32 >= (long)w * (long)h) {
            c->error[0] = 0;
            return got;
        }
    }

    /* Second attempt: obfuscated resources have random names, so take the six
     * biggest images under res/ and keep the largest square one (>= 96px), which
     * is the launcher icon on every APK seen so far. */
    snprintf(cmd, sizeof(cmd),
             "busybox unzip -l '%s' 2>/dev/null | grep -E 'res/[^/]+\\.(png|webp)$' "
             "| sort -k1 -n -r 2>/dev/null | head -6", apk);
    if (adb_shell(&c->conn, cmd, raw, sizeof(raw), 20000) < 0) {
        snprintf(c->error, sizeof(c->error), "%s", c->conn.last_error_text);
        return ADB_ERR_IO;
    }
    char names[6][192];
    long sizes[6] = {0, 0, 0, 0, 0, 0};
    int have = 0;
    line = raw;
    while (*line) {
        const char *eol = strchr(line, '\n');
        size_t len = eol ? (size_t)(eol - line) : strlen(line);
        const char *name_end = line + len;
        while (name_end > line && (name_end[-1] == '\r' || name_end[-1] == ' ')) name_end--;
        const char *name_start = name_end;
        while (name_start > line && name_start[-1] != ' ') name_start--;
        size_t nlen = (size_t)(name_end - name_start);
        long size = atol(line);
        if (nlen > 4 && nlen < sizeof(names[0]) && size > 0) {
            int slot = -1;
            long smallest = -1;
            if (have < 6) {
                slot = have++;
            } else {
                for (int i = 0; i < 6; i++) {
                    if (smallest < 0 || sizes[i] < smallest) { smallest = sizes[i]; slot = i; }
                }
                if (sizes[slot] >= size) slot = -1;
            }
            if (slot >= 0) {
                memcpy(names[slot], name_start, nlen);
                names[slot][nlen] = 0;
                sizes[slot] = size;
            }
        }
        if (!eol) break;
        line = eol + 1;
    }
    if (have == 0) {
        if (got > 0) { c->error[0] = 0; return got; }
        snprintf(c->error, sizeof(c->error), "APK sem icone utilizavel");
        return ADB_ERR_PROTOCOL;
    }

    /* Real launcher icons are 48-512px; a 2048px square is usually a splash or a
     * placeholder, so those are only used when nothing else is square. */
    char small_name[192] = {0};
    char big_name[192] = {0};
    long small_area = 0, big_area = 0;
    for (int i = 0; i < have; i++) {
        int n = anytv_pull_apk_entry(c, apk, names[i], out, outlen);
        int w = 0, h = 0;
        if (n > 0 && anytv_image_dims(out, (size_t)n, &w, &h) && w == h && w >= 96) {
            long area = (long)w * (long)h;
            /* A flat placeholder (grey note, offline glyph) compresses to almost
             * nothing; real artwork always carries tens of bytes per pixel. */
            if ((long)n * 32 < area) continue;
            if (w <= 512) {
                if (area > small_area) {
                    small_area = area;
                    size_t sl = strlen(names[i]);
                    memcpy(small_name, names[i], sl);
                    small_name[sl] = 0;
                }
            } else if (area > big_area) {
                big_area = area;
                size_t bl = strlen(names[i]);
                memcpy(big_name, names[i], bl);
                big_name[bl] = 0;
            }
        }
    }
    const char *chosen = small_name[0] ? small_name : (big_name[0] ? big_name : NULL);
    if (chosen) {
        int n = anytv_pull_apk_entry(c, apk, chosen, out, outlen);
        if (n > 0) { c->error[0] = 0; return n; }
    }
    if (got > 0) { c->error[0] = 0; return got; }
    snprintf(c->error, sizeof(c->error), "APK sem icone utilizavel");
    return ADB_ERR_PROTOCOL;
}

int anytv_client_stream_start(anytv_client *c, int width, int height, int bitrate_bps, int seconds) {
    if (!c || c->proto != ANYTV_PROTO_ADB || !c->connected) {
        if (c) snprintf(c->error, sizeof(c->error), "stream so existe no ADB");
        return ADB_ERR_IO;
    }
    int want_size = (width > 0 && height > 0);
    if (width < 64) width = 640;
    if (height < 64) height = 360;
    if (bitrate_bps < 100000) bitrate_bps = 4000000;
    if (seconds <= 0 || seconds > 180) seconds = 180;
    /* Android's own encoder: Annex-B H.264 on stdout, no root, no extra binary.
     * Asking screenrecord to downscale (--size) costs frames on cheap boxes, so
     * the native size is the default and only explicit values add --size. */
    char size_arg[48] = "";
    if (want_size) snprintf(size_arg, sizeof(size_arg), "--size %dx%d ", width, height);
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             "screenrecord --output-format=h264 --time-limit %d %s--bit-rate %d - 2>/dev/null",
             seconds, size_arg, bitrate_bps);
    int rc = adb_stream_start(&c->conn, cmd, 8000);
    if (rc != ADB_OK) {
        snprintf(c->error, sizeof(c->error), "%s", c->conn.last_error_text);
        return rc;
    }
    return ADB_OK;
}

int anytv_client_stream_read(anytv_client *c, uint8_t *out, size_t outlen, int timeout_ms) {
    if (!c) return ADB_ERR_IO;
    if (!c->scrcpy) return adb_stream_read(&c->conn, out, outlen, timeout_ms);

    /* scrcpy v1.x frames: 12-byte header (8-byte pts, 4-byte size) + H.264 data.
     * Headers are stripped here so the decoder only sees Annex-B. */
    size_t produced = 0;
    while (produced < outlen) {
        if (c->sc_remaining > 0) {
            size_t avail = c->sc_raw_len - c->sc_raw_off;
            if (avail == 0) {
                int got = adb_stream_read(&c->conn, c->sc_raw, sizeof(c->sc_raw), timeout_ms);
                if (got <= 0) return produced > 0 ? (int)produced : got;
                c->sc_raw_len = (size_t)got;
                c->sc_raw_off = 0;
                continue;
            }
            size_t take = avail < c->sc_remaining ? avail : c->sc_remaining;
            size_t space = outlen - produced;
            if (take > space) take = space;
            memcpy(out + produced, c->sc_raw + c->sc_raw_off, take);
            produced += take;
            c->sc_raw_off += take;
            c->sc_remaining -= (uint32_t)take;
            continue;
        }
        if (c->sc_hdr_len < sizeof(c->sc_hdr)) {
            if (c->sc_raw_off >= c->sc_raw_len) {
                int got = adb_stream_read(&c->conn, c->sc_raw, sizeof(c->sc_raw), timeout_ms);
                if (got <= 0) return produced > 0 ? (int)produced : got;
                c->sc_raw_len = (size_t)got;
                c->sc_raw_off = 0;
            }
            c->sc_hdr[c->sc_hdr_len++] = c->sc_raw[c->sc_raw_off++];
            if (c->sc_hdr_len == sizeof(c->sc_hdr)) {
                c->sc_remaining = ((uint32_t)c->sc_hdr[8] << 24) | ((uint32_t)c->sc_hdr[9] << 16) |
                                  ((uint32_t)c->sc_hdr[10] << 8) | c->sc_hdr[11];
                c->sc_hdr_len = 0;
            }
            continue;
        }
        break;
    }
    return (int)produced;
}

int anytv_client_scrcpy_start(anytv_client *c, const uint8_t *jar, size_t jar_len,
                              int bitrate_bps, int max_fps, int max_size, int timeout_ms) {
    if (!c || c->proto != ANYTV_PROTO_ADB || !c->connected) {
        if (c) snprintf(c->error, sizeof(c->error), "scrcpy so existe no ADB");
        return ADB_ERR_IO;
    }
    if (!jar || jar_len < 1024) {
        snprintf(c->error, sizeof(c->error), "servidor scrcpy ausente");
        return ADB_ERR_IO;
    }
    const char *remote = "/data/local/tmp/scrcpy-server.jar";
    char probe[512];
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "ls -l %s 2>/dev/null", remote);
    if (adb_shell(&c->conn, cmd, probe, sizeof(probe), 8000) < 0) probe[0] = 0;
    if (!strstr(probe, "scrcpy-server.jar")) {
        int rc = adb_push(&c->conn, remote, jar, jar_len, timeout_ms);
        if (rc != ADB_OK) {
            snprintf(c->error, sizeof(c->error), "falha ao enviar o scrcpy-server: %.180s",
                     c->conn.last_error_text);
            return rc;
        }
    }
    adb_shell(&c->conn, "pkill -f com.genymobile.scrcpy 2>/dev/null; sleep 0.2",
              probe, sizeof(probe), 5000);

    if (bitrate_bps < 500000) bitrate_bps = 8000000;
    if (max_fps < 1) max_fps = 60;
    if (max_size < 0) max_size = 0;
    snprintf(cmd, sizeof(cmd),
             "nohup env CLASSPATH=%s app_process / com.genymobile.scrcpy.Server 1.25 "
             "log_level=info max_size=%d bit_rate=%d max_fps=%d tunnel_forward=true "
             "send_device_meta=true send_dummy_byte=true control=false cleanup=false "
             ">/data/local/tmp/anytv-scrcpy.log 2>&1 &",
             remote, max_size, bitrate_bps, max_fps);
    if (adb_shell(&c->conn, cmd, probe, sizeof(probe), 8000) < 0) {
        snprintf(c->error, sizeof(c->error), "%s", c->conn.last_error_text);
        return ADB_ERR_IO;
    }

    /* The server creates the "scrcpy" abstract socket; give it a moment. */
    int rc = ADB_ERR_SOCKET;
    for (int attempt = 0; attempt < 12; attempt++) {
        rc = adb_stream_start_service(&c->conn, "localabstract:scrcpy", 8000);
        if (rc == ADB_OK) break;
        struct timespec ts = { .tv_sec = 0, .tv_nsec = 250 * 1000 * 1000 };
        nanosleep(&ts, NULL);
        snprintf(cmd, sizeof(cmd), "ls /proc/net/unix >/dev/null 2>&1");
        char sink[64];
        adb_shell(&c->conn, cmd, sink, sizeof(sink), 4000);
    }
    if (rc != ADB_OK) {
        snprintf(c->error, sizeof(c->error), "servidor scrcpy nao abriu o socket (%d)", rc);
        return rc;
    }

    /* 1 dummy byte, then 64-byte name + width/height as big-endian u16 */
    uint8_t meta[1 + 64 + 4];
    size_t got = 0;
    while (got < sizeof(meta)) {
        int n = adb_stream_read(&c->conn, meta + got, sizeof(meta) - got, 4000);
        if (n <= 0) {
            snprintf(c->error, sizeof(c->error), "sem metadados do scrcpy (%d)", n);
            adb_stream_stop(&c->conn);
            return ADB_ERR_PROTOCOL;
        }
        got += (size_t)n;
    }
    memcpy(c->sc_device, meta + 1, 63);
    c->sc_device[63] = 0;
    for (int i = 63; i > 0 && c->sc_device[i - 1] == 0; i--) c->sc_device[i - 1] = 0;
    c->sc_width = (meta[65] << 8) | meta[66];
    c->sc_height = (meta[67] << 8) | meta[68];
    c->scrcpy = 1;
    c->sc_meta_done = 1;
    c->sc_hdr_len = 0;
    c->sc_remaining = 0;
    c->sc_raw_len = 0;
    c->sc_raw_off = 0;
    c->error[0] = 0;
    return ADB_OK;
}

/* Probes the dev bridge and reports whether the device can list apps. Keeps the
 * capability mask honest without making the caller know about sdb. */
int anytv_client_probe_apps(anytv_client *c) {
    if (!c) return 0;
    if (c->proto == ANYTV_PROTO_TIZEN) {
        if (tizen_sdb_ready(c) != ADB_OK) return 0;
        c->caps |= ANYTV_CAP_APPS | ANYTV_CAP_LAUNCH;
        return 1;
    }
    return (c->caps & ANYTV_CAP_APPS) ? 1 : 0;
}

int anytv_client_scrcpy_width(const anytv_client *c) { return c ? c->sc_width : 0; }
int anytv_client_scrcpy_height(const anytv_client *c) { return c ? c->sc_height : 0; }

void anytv_client_stream_stop(anytv_client *c) {
    if (!c) return;
    adb_stream_stop(&c->conn);
    if (c->scrcpy) {
        char sink[128];
        adb_shell(&c->conn, "pkill -f com.genymobile.scrcpy 2>/dev/null", sink, sizeof(sink), 4000);
        c->scrcpy = 0;
    }
}

int anytv_client_screenshot(anytv_client *c, uint8_t *out, size_t outlen) {
    if (!c || c->proto != ANYTV_PROTO_ADB) {
        if (c) snprintf(c->error, sizeof(c->error), "captura de tela so existe no ADB");
        return ADB_ERR_IO;
    }
    if (!c->connected || !out) return ADB_ERR_IO;
    char cmd[256];
    anytv_screencap_command(cmd, sizeof(cmd));
    char small[512];
    int code = adb_shell(&c->conn, cmd, small, sizeof(small), 20000);
    if (code < 0) {
        snprintf(c->error, sizeof(c->error), "%s", c->conn.last_error_text);
        return code;
    }
    int got = adb_pull(&c->conn, "/data/local/tmp/anytv_shot.png", out, outlen, 20000);
    adb_rm(&c->conn, "/data/local/tmp/anytv_shot.png", 8000);
    if (got < 0) snprintf(c->error, sizeof(c->error), "falha ao ler a captura");
    return got;
}

int anytv_client_info(anytv_client *c, char *out, size_t outlen) {
    if (!c) return ADB_ERR_PROTOCOL;
    int rc;
    switch (c->proto) {
    case ANYTV_PROTO_ROKU:
        rc = roku_info(&c->ctx, out, outlen);
        break;
    case ANYTV_PROTO_WEBOS:
        rc = webos_info(&c->ctx, out, outlen);
        break;
    case ANYTV_PROTO_TIZEN:
        rc = tizen_info(&c->ctx, out, outlen);
        break;
    default:
        snprintf(out, outlen, "%s|Android %s|%s",
                 c->conn.device_model[0] ? c->conn.device_model : "Android TV",
                 c->conn.device_version, c->conn.device_banner);
        rc = NET_OK;
        break;
    }
    if (rc != NET_OK) snprintf(c->error, sizeof(c->error), "falha ao ler informacoes");
    return rc == NET_OK ? 0 : ADB_ERR_IO;
}

int anytv_client_wake(anytv_client *c) {
    if (!c) return ADB_ERR_PROTOCOL;
    if (c->proto == ANYTV_PROTO_ADB) return adb_run(c, "input keyevent KEYCODE_WAKEUP", 8000);
    return anytv_client_cmd(c, ANYTV_CMD_POWER, 1);
}

int anytv_client_power_on(anytv_client *c) {
    if (!c) return ADB_ERR_PROTOCOL;
    if (c->ctx.mac[0] == 0) {
        snprintf(c->error, sizeof(c->error), "MAC desconhecido: informe o MAC da TV para ligar");
        return ADB_ERR_PROTOCOL;
    }
    return net_wol_full(c->ctx.mac, c->ctx.host, 9) == NET_OK ? 0 : ADB_ERR_IO;
}

int anytv_client_sleep(anytv_client *c) {
    if (!c) return ADB_ERR_PROTOCOL;
    switch (c->proto) {
    case ANYTV_PROTO_ROKU: return anytv_client_cmd(c, ANYTV_CMD_POWER, 1);
    case ANYTV_PROTO_WEBOS: {
        int rc = webos_power_off(&c->ctx);
        if (rc != NET_OK) snprintf(c->error, sizeof(c->error), "LG nao desligou");
        return rc == NET_OK ? 0 : ADB_ERR_IO;
    }
    case ANYTV_PROTO_TIZEN: return anytv_client_cmd(c, ANYTV_CMD_POWER, 1);
    default: return adb_run(c, "input keyevent KEYCODE_SLEEP", 8000);
    }
}

int anytv_probe_port(const char *host, int port, int timeout_ms) {
    int fd = net_connect(host, port, timeout_ms);
    if (fd < 0) return 0;
    net_close(fd);
    return 1;
}
