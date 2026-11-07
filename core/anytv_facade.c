/* anytv_facade.c - dispatches the canonical commands to the right transport. */
#include "anytv_facade.h"
#include "anytv_adb.h"
#include "anytv_keys.h"
#include "anytv_net.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct anytv_client {
    int proto;
    adb_conn conn;          /* ADB transport */
    rsa_key key;
    int have_key;
    proto_ctx ctx;          /* Roku / webOS / Tizen */
    int connected;
    uint32_t caps;
    char error[256];
};

static const char *PROTO_NAMES[] = { "adb", "roku", "webos", "tizen" };

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
        if (rc != NET_OK) {
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

int anytv_client_apps(anytv_client *c, char *out, size_t outlen) {
    if (!c) return ADB_ERR_IO;
    int rc;
    switch (c->proto) {
    case ANYTV_PROTO_ROKU: rc = roku_apps(&c->ctx, out, outlen); break;
    case ANYTV_PROTO_WEBOS: rc = webos_apps(&c->ctx, out, outlen); break;
    case ANYTV_PROTO_TIZEN:
        snprintf(c->error, sizeof(c->error), "Samsung nao expoe lista de apps no controle remoto");
        return ADB_ERR_PROTOCOL;
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
    if (rc != NET_OK) snprintf(c->error, sizeof(c->error), "falha ao listar apps");
    return rc == NET_OK ? 0 : ADB_ERR_IO;
}

int anytv_client_launch(anytv_client *c, const char *app_id) {
    if (!c) return ADB_ERR_PROTOCOL;
    int rc;
    switch (c->proto) {
    case ANYTV_PROTO_ROKU: rc = roku_launch(&c->ctx, app_id); break;
    case ANYTV_PROTO_WEBOS: rc = webos_launch(&c->ctx, app_id); break;
    case ANYTV_PROTO_TIZEN:
        snprintf(c->error, sizeof(c->error), "Samsung nao permite abrir app por controle remoto");
        return ADB_ERR_PROTOCOL;
    default: {
        char cmd[512];
        anytv_launch_command(app_id, cmd, sizeof(cmd));
        return adb_run(c, cmd, 15000);
    }
    }
    if (rc != NET_OK) snprintf(c->error, sizeof(c->error), "falha ao abrir o app");
    return rc == NET_OK ? 0 : ADB_ERR_IO;
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
    return net_wol(c->ctx.mac, 9) == NET_OK ? 0 : ADB_ERR_IO;
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
