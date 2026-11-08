/* test_scrcpy_ctl.c - prova que o canal de controle do scrcpy injeta teclas
 * sem root, medindo o tempo e conferindo o foco da tela antes e depois. */
#include "anytv_adb.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static uint8_t *read_file(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc((size_t)n);
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); buf = NULL; }
    fclose(f);
    *len = (size_t)n;
    return buf;
}

static void key_msg(uint8_t out[14], int action, int keycode) {
    out[0] = 0;                       /* INJECT_KEYCODE */
    out[1] = (uint8_t)action;         /* 0 = down, 1 = up */
    out[2] = (uint8_t)(keycode >> 24); out[3] = (uint8_t)(keycode >> 16);
    out[4] = (uint8_t)(keycode >> 8);  out[5] = (uint8_t)keycode;
    memset(out + 6, 0, 8);            /* repeat + metaState */
}

int main(int argc, char **argv) {
    const char *host = argc > 1 ? argv[1] : "127.0.0.1";
    int port = argc > 2 ? atoi(argv[2]) : 5601;
    const char *jar = argc > 3 ? argv[3] : "/tmp/scrcpy-server-1.25";

    adb_conn c;
    memset(&c, 0, sizeof(c));
    c.fd = -1;
    if (adb_connect(&c, host, port, NULL, 6000) != ADB_OK) {
        printf("connect falhou: %s\n", c.last_error_text);
        return 1;
    }
    printf("conectado: %s\n", c.device_model);

    size_t jlen = 0;
    uint8_t *data = read_file(jar, &jlen);
    if (!data) { printf("jar nao lido\n"); return 1; }
    /* envia pelo stdin de um cat: nao depende do sync do adbd */
    if (adb_stream_start_service(&c, "shell:cat > /data/local/tmp/scrcpy-server.jar", 8000) != ADB_OK) {
        printf("canal de envio falhou: %s\n", c.last_error_text);
        return 1;
    }
    size_t off = 0;
    while (off < jlen) {
        size_t n = jlen - off;
        if (n > 3000) n = 3000;
        if (adb_stream_write(&c, data + off, n) != ADB_OK) {
            printf("envio interrompido em %zu\n", off);
            break;
        }
        off += n;
    }
    adb_stream_stop(&c);
    printf("jar enviado: %zu bytes\n", off);
    free(data);

    char out[4096];
    /* abre Ajustes para ter certeza de que o foco muda quando mandarmos HOME */
    adb_shell(&c, "am start -a android.settings.SETTINGS >/dev/null 2>&1", out, sizeof(out), 8000);
    struct timespec w = {2, 0};
    nanosleep(&w, NULL);
    adb_shell(&c, "dumpsys window 2>/dev/null | grep -m1 mCurrentFocus", out, sizeof(out), 8000);
    printf("foco antes: %.80s\n", out);

    adb_shell(&c, "pkill -f com.genymobile.scrcpy.Server; sleep 1", out, sizeof(out), 5000);
    const char *cmd =
        "CLASSPATH=/data/local/tmp/scrcpy-server.jar app_process /system/bin "
        "com.genymobile.scrcpy.Server 1.25 log_level=info max_size=1024 "
        "bit_rate=2000000 max_fps=15 lock_video_orientation=-1 tunnel_forward=true "
        "send_frame_meta=true control=true display_id=0 show_touches=false "
        "stay_awake=false >/data/local/tmp/scrcpy.log 2>&1 &";
    adb_shell(&c, cmd, out, sizeof(out), 4000);
    printf("servidor iniciado\n");
    {
        char log[1024];
        adb_shell(&c, "sleep 2; cat /data/local/tmp/scrcpy.log 2>/dev/null | head -5", log, sizeof(log), 8000);
        printf("log servidor: %.500s\n", log);
    }
    struct timespec ts = {1, 500000000};
    nanosleep(&ts, NULL);

    if (adb_stream_start_service(&c, "localabstract:scrcpy", 8000) != ADB_OK) {
        printf("socket scrcpy falhou: %s\n", c.last_error_text);
    } else {
        uint8_t head[512];
        int n = 0;
        for (int i = 0; i < 6 && n < 68; i++) {   /* cabecalho do scrcpy: 68 bytes */
            int r = adb_stream_read(&c, head + n, sizeof(head) - (size_t)n, 3000);
            printf("  leitura %d: %d bytes\n", i, r);
            if (r <= 0) break;
            n += r;
        }
        printf("scrcpy: %d bytes de cabecalho (%.40s)\n", n, (char *)head);

        uint8_t msg[14];
        double t0 = now_ms();
        key_msg(msg, 0, 3);              /* HOME down */
        adb_stream_write(&c, msg, sizeof(msg));
        key_msg(msg, 1, 3);              /* HOME up */
        adb_stream_write(&c, msg, sizeof(msg));
        double dt = now_ms() - t0;
        printf("envio de HOME: %.1f ms\n", dt);

        adb_stream_stop(&c);
    }

    /* segundo socket, caso o controle viaje separado */
    if (adb_stream_start_service(&c, "localabstract:scrcpy_control", 4000) == ADB_OK) {
        printf("socket scrcpy_control abriu\n");
        adb_stream_stop(&c);
    } else {
        printf("scrcpy_control indisponivel (%s)\n", c.last_error_text);
    }

    struct timespec ts2 = {1, 0};
    nanosleep(&ts2, NULL);
    adb_shell(&c, "dumpsys window 2>/dev/null | grep -m1 mCurrentFocus", out, sizeof(out), 8000);
    printf("foco depois: %.70s\n", out);

    if (c.fd >= 0) close(c.fd);
    return 0;
}
