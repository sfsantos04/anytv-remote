/* anytv_facade.c - thin, string-safe wrapper around the ADB core. */
#include "anytv_facade.h"
#include "anytv_adb.h"
#include "anytv_keys.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct anytv_client {
    adb_conn conn;
    rsa_key key;
    int have_key;
    int connected;
    char error[256];
};

anytv_client *anytv_client_new(void) {
    anytv_client *c = (anytv_client *)calloc(1, sizeof(anytv_client));
    if (!c) return NULL;
    c->conn.fd = -1;
    return c;
}

void anytv_client_free(anytv_client *c) {
    if (!c) return;
    if (c->connected) adb_close(&c->conn);
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
    if (!c->have_key) {
        int rc = anytv_client_keygen(c);
        if (rc != 0) return rc;
    }
    if (c->connected) { adb_close(&c->conn); c->connected = 0; }
    c->conn.verbose = 0;
    int rc = adb_connect_ex(&c->conn, host, port, &c->key, timeout_ms, auth_wait_ms);
    if (rc != ADB_OK) {
        snprintf(c->error, sizeof(c->error), "%s", c->conn.last_error_text);
        c->connected = 0;
        return rc;
    }
    c->connected = 1;
    c->error[0] = 0;
    return 0;
}

void anytv_client_disconnect(anytv_client *c) {
    if (!c) return;
    if (c->connected) { adb_close(&c->conn); c->connected = 0; }
}

int anytv_client_connected(const anytv_client *c) { return c ? c->connected : 0; }

const char *anytv_client_model(const anytv_client *c) {
    return (c && c->conn.device_model[0]) ? c->conn.device_model : "";
}

const char *anytv_client_android(const anytv_client *c) {
    return (c && c->conn.device_version[0]) ? c->conn.device_version : "";
}

const char *anytv_client_error(const anytv_client *c) {
    return c ? c->error : "";
}

static int run(anytv_client *c, const char *cmd, int timeout_ms) {
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

int anytv_client_key(anytv_client *c, int code, int repeat) {
    char cmd[256];
    anytv_keyevent_command(code, repeat, cmd, sizeof(cmd));
    return run(c, cmd, 8000);
}

int anytv_client_text(anytv_client *c, const char *utf8) {
    char cmd[4096];
    if (anytv_input_text_command(utf8, cmd, sizeof(cmd)) < 0) return ADB_ERR_PROTOCOL;
    return run(c, cmd, 8000);
}

int anytv_client_shell(anytv_client *c, const char *command, char *out, size_t outlen,
                       int timeout_ms) {
    if (!c || !c->connected) {
        if (c) snprintf(c->error, sizeof(c->error), "nao conectado");
        return ADB_ERR_IO;
    }
    int code = adb_shell(&c->conn, command, out, outlen, timeout_ms);
    if (code < 0) {
        snprintf(c->error, sizeof(c->error), "%s", c->conn.last_error_text);
        return code;
    }
    return code;
}

int anytv_client_apps(anytv_client *c, char *out, size_t outlen) {
    if (!c || !c->connected) return ADB_ERR_IO;
    /* Compact first: the launcher activity list of every installed app. */
    int code = adb_shell(&c->conn,
                         "cmd package query-activities --brief -c "
                         "android.intent.category.LEANBACK_LAUNCHER 2>/dev/null || "
                         "pm list packages -3",
                         out, outlen, 20000);
    if (code >= 0) return 0;
    snprintf(c->error, sizeof(c->error), "%s", c->conn.last_error_text);
    return code;
}

int anytv_client_launch(anytv_client *c, const char *package) {
    char cmd[512];
    anytv_launch_command(package, cmd, sizeof(cmd));
    return run(c, cmd, 15000);
}

int anytv_client_screenshot(anytv_client *c, uint8_t *out, size_t outlen) {
    if (!c || !c->connected || !out) return ADB_ERR_IO;
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

int anytv_client_wake(anytv_client *c) {
    return run(c, "input keyevent KEYCODE_WAKEUP", 8000);
}

int anytv_client_sleep(anytv_client *c) {
    return run(c, "input keyevent KEYCODE_SLEEP", 8000);
}
