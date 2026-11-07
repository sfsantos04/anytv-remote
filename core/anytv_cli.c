/* anytv_cli.c - Linux test harness for the same core the iOS app links.
 * Everything the app can do can be exercised here against a real device. */
#ifndef X_NO_MAIN

#include "anytv_adb.h"
#include "anytv_keys.h"
#include "anytv_facade.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int load_or_create_key(const char *path, rsa_key *key) {
    FILE *f = fopen(path, "rb");
    if (f) {
        uint8_t blob[RSA_BYTES * 3];
        size_t n = fread(blob, 1, sizeof(blob), f);
        fclose(f);
        if (n == sizeof(blob) && rsa_import(key, blob, n) == 0) return 0;
        fprintf(stderr, "key file corrupt, regenerating\n");
    }
    fprintf(stderr, "generating RSA-2048 key (one time)...\n");
    clock_t t0 = clock();
    if (rsa_generate(key) != 0) return -1;
    fprintf(stderr, "key generated in %.2fs\n", (double)(clock() - t0) / CLOCKS_PER_SEC);
    uint8_t blob[RSA_BYTES * 3];
    if (rsa_export(key, blob, sizeof(blob)) != sizeof(blob)) return -1;
    f = fopen(path, "wb");
    if (!f) return -1;
    fwrite(blob, 1, sizeof(blob), f);
    fclose(f);
    return 0;
}

/* Generic mode for the non-ADB transports (Roku ECP, webOS, Tizen). */
static int generic_mode(int proto, const char *host, int port, const char *keypath,
                        const char *cmd, int argc, char **argv) {
    anytv_client *c = anytv_client_new_proto(proto);
    if (!c) return 3;
    const char *token = getenv("ANYTV_TOKEN");
    if (token) anytv_client_set_token(c, token);
    const char *mac = getenv("ANYTV_MAC");
    if (mac) anytv_client_set_mac(c, mac);
    if (strcmp(cmd, "selftest") == 0) {
        printf("proto=%s caps=0x%x\n", anytv_client_proto_name(c), anytv_client_caps(c));
        anytv_client_free(c);
        return 0;
    }
    int rc = anytv_client_connect(c, host, port, 5000, 90000);
    if (rc != 0) {
        fprintf(stderr, "connect failed rc=%d err=%s\n", rc, anytv_client_error(c));
        anytv_client_free(c);
        return 4;
    }
    printf("connected proto=%s caps=0x%x model=%s token=%s mac=%s\n",
           anytv_client_proto_name(c), anytv_client_caps(c),
           anytv_client_model(c), anytv_client_token(c), anytv_client_mac(c));

    char out[65536];
    int ret = 0;
    if (strcmp(cmd, "cmd") == 0) {
        int code = anytv_cmd_from_name(argc > 5 ? argv[5] : "");
        if (code < 0) { fprintf(stderr, "comando invalido\n"); ret = 5; }
        else {
            int r = anytv_client_cmd(c, code, argc > 6 ? atoi(argv[6]) : 1);
            printf("cmd=%s rc=%d err=%s\n", anytv_cmd_name((anytv_cmd)code), r,
                   anytv_client_error(c));
            ret = r == 0 ? 0 : 6;
        }
    } else if (strcmp(cmd, "text") == 0) {
        int r = anytv_client_text(c, argc > 5 ? argv[5] : "");
        printf("text rc=%d err=%s\n", r, anytv_client_error(c));
        ret = r == 0 ? 0 : 6;
    } else if (strcmp(cmd, "apps") == 0) {
        int r = anytv_client_apps(c, out, sizeof(out));
        printf("apps rc=%d err=%s\n%s\n", r, anytv_client_error(c), out);
        ret = r == 0 ? 0 : 6;
    } else if (strcmp(cmd, "launch") == 0) {
        int r = anytv_client_launch(c, argc > 5 ? argv[5] : "");
        printf("launch rc=%d err=%s\n", r, anytv_client_error(c));
        ret = r == 0 ? 0 : 6;
    } else if (strcmp(cmd, "info") == 0) {
        int r = anytv_client_info(c, out, sizeof(out));
        printf("info rc=%d err=%s\n%s\n", r, anytv_client_error(c), out);
        ret = r == 0 ? 0 : 6;
    } else if (strcmp(cmd, "poweron") == 0) {
        int r = anytv_client_power_on(c);
        printf("poweron rc=%d err=%s\n", r, anytv_client_error(c));
        ret = r == 0 ? 0 : 6;
    } else if (strcmp(cmd, "sleep") == 0) {
        int r = anytv_client_sleep(c);
        printf("sleep rc=%d err=%s\n", r, anytv_client_error(c));
        ret = r == 0 ? 0 : 6;
    } else {
        fprintf(stderr, "comando desconhecido no modo generico\n");
        ret = 2;
    }
    anytv_client_disconnect(c);
    anytv_client_free(c);
    (void)keypath;
    return ret;
}

int main(int argc, char **argv) {
    if (argc < 5) {
        fprintf(stderr,
                "usage: %s <host> <port> <keyfile> <command> [args]\n"
                "  probe | keygen | pub | info | sh <cmd> | key <NAME> [n] |\n"
                "  text <str> | apps | launch <pkg> | shot <file.png> | wake | sleep\n", argv[0]);
        return 2;
    }
    const char *host = argv[1];
    int port = atoi(argv[2]);
    const char *keypath = argv[3];
    const char *cmd = argv[4];

    const char *proto_env = getenv("ANYTV_PROTO");
    if (proto_env && strcmp(proto_env, "adb") != 0) {
        int proto = strcmp(proto_env, "roku") == 0 ? ANYTV_PROTO_ROKU
                  : strcmp(proto_env, "webos") == 0 ? ANYTV_PROTO_WEBOS
                  : ANYTV_PROTO_TIZEN;
        return generic_mode(proto, host, port, keypath, cmd, argc, argv);
    }

    rsa_key key;
    memset(&key, 0, sizeof(key));
    int need_key = strcmp(cmd, "probe") != 0;
    if (need_key && load_or_create_key(keypath, &key) != 0) {
        fprintf(stderr, "key error\n");
        return 3;
    }
    if (strcmp(cmd, "keygen") == 0 || strcmp(cmd, "pub") == 0) {
        char b64[RSA_PUB_BLOB_MAX * 2];
        rsa_public_adb_b64(&key, b64, sizeof(b64));
        printf("%s\n", b64);
        return 0;
    }
    if (strcmp(cmd, "probe") == 0) {
        printf("probe %s:%d -> %s\n", host, port, adb_probe(host, port, 1500) ? "ADB" : "no");
        return adb_probe(host, port, 1500) ? 0 : 1;
    }

    adb_conn c;
    memset(&c, 0, sizeof(c));
    const char *v = getenv("ANYTV_VERBOSE");
    const char *so = getenv("ANYTV_SIGORDER");
    c.verbose = v ? atoi(v) : 0;
    c.sig_order = so ? atoi(so) : 0;
    int rc = adb_connect_ex(&c, host, port, &key, 8000, 90000);
    if (rc != ADB_OK) {
        fprintf(stderr, "connect failed rc=%d err=%s\n", rc, c.last_error_text);
        return 4;
    }
    printf("connected: model=%s android=%s banner_len=%zu\n",
           c.device_model[0] ? c.device_model : "?", c.device_version[0] ? c.device_version : "?",
           strlen(c.device_banner));

    char out[65536];
    int code = 0;
    if (strcmp(cmd, "raw") == 0) {
        /* debug: open a raw service and dump the frames the device sends */
        char svc[512] = {0};
        for (int i = 5; i < argc; i++) {
            strncat(svc, argv[i], sizeof(svc) - strlen(svc) - 2);
            if (i + 1 < argc) strncat(svc, " ", sizeof(svc) - strlen(svc) - 2);
        }
        int drc = anytv_debug_service(&c, svc[0] ? svc : "shell:echo hi");
        printf("debug_rc=%d\n", drc);
    } else if (strcmp(cmd, "info") == 0) {
        code = adb_shell(&c, "getprop ro.product.model; getprop ro.build.version.release; "
                             "getprop ro.build.version.sdk; getprop ro.serialno",
                         out, sizeof out, 8000);
        printf("exit=%d\n%s\n", code, out);
    } else if (strcmp(cmd, "sh") == 0) {
        char line[1024] = {0};
        for (int i = 5; i < argc; i++) {
            strncat(line, argv[i], sizeof(line) - strlen(line) - 2);
            if (i + 1 < argc) strncat(line, " ", sizeof(line) - strlen(line) - 2);
        }
        code = adb_shell(&c, line, out, sizeof out, 15000);
        printf("exit=%d\n%s\n", code, out);
    } else if (strcmp(cmd, "key") == 0) {
        int kc = anytv_key_code(argc > 5 ? argv[5] : "");
        if (kc < 0) { fprintf(stderr, "unknown key\n"); return 5; }
        char line[512];
        anytv_keyevent_command(kc, argc > 6 ? atoi(argv[6]) : 1, line, sizeof line);
        code = adb_shell(&c, line, out, sizeof out, 8000);
        printf("sent %s (%d) exit=%d out=%s\n", anytv_key_name(kc), kc, code, out);
    } else if (strcmp(cmd, "text") == 0) {
        char line[2048];
        anytv_input_text_command(argc > 5 ? argv[5] : "", line, sizeof line);
        code = adb_shell(&c, line, out, sizeof out, 8000);
        printf("cmd=%s exit=%d out=%s\n", line, code, out);
    } else if (strcmp(cmd, "apps") == 0) {
        code = adb_shell(&c, "dumpsys package", out, sizeof out, 30000);
        static char pkgs[256][128];
        int n = anytv_parse_launchable(out, pkgs, 256);
        printf("exit=%d launchable=%d buffer_used=%zu\n", code, n, strlen(out));
        for (int i = 0; i < n; i++) printf("  %s\n", pkgs[i]);
    } else if (strcmp(cmd, "launch") == 0) {
        char line[512];
        anytv_launch_command(argc > 5 ? argv[5] : "", line, sizeof line);
        code = adb_shell(&c, line, out, sizeof out, 15000);
        printf("exit=%d\n%s\n", code, out);
    } else if (strcmp(cmd, "shot") == 0) {
        char line[256];
        anytv_screencap_command(line, sizeof line);
        code = adb_shell(&c, line, out, sizeof out, 20000);
        size_t cap = 12u * 1024 * 1024;
        uint8_t *png = (uint8_t *)malloc(cap);
        int got = adb_pull(&c, "/data/local/tmp/anytv_shot.png", png, cap, 20000);
        printf("screencap exit=%d pulled=%d bytes\n", code, got);
        if (got > 0 && argc > 5) {
            FILE *f = fopen(argv[5], "wb");
            if (f) { fwrite(png, 1, (size_t)got, f); fclose(f); printf("wrote %s\n", argv[5]); }
        }
        adb_rm(&c, "/data/local/tmp/anytv_shot.png", 8000);
        free(png);
    } else if (strcmp(cmd, "wake") == 0) {
        code = adb_shell(&c, "input keyevent KEYCODE_WAKEUP; input keyevent KEYCODE_MENU", out, sizeof out, 8000);
        printf("exit=%d out=%s\n", code, out);
    } else if (strcmp(cmd, "sleep") == 0) {
        code = adb_shell(&c, "input keyevent KEYCODE_SLEEP", out, sizeof out, 8000);
        printf("exit=%d out=%s\n", code, out);
    } else {
        fprintf(stderr, "unknown command %s\n", cmd);
        adb_close(&c);
        return 2;
    }
    adb_close(&c);
    return 0;
}
#endif
