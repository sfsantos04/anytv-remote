/* test_scrcpy.c - prova o caminho scrcpy: envia o servidor, abre o socket
 * abstrato e grava o H.264 recebido para um decodificador independente validar.
 * uso: ./test_scrcpy <host> <port> <key|-> <jar> <segundos> <saida.264> [bitrate fps max_size] */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../core/anytv_facade.h"

static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

int main(int argc, char **argv) {
    if (argc < 7) {
        fprintf(stderr, "uso: %s <host> <port> <key|-> <jar> <segundos> <saida> [bitrate fps max_size]\n", argv[0]);
        return 2;
    }
    int seconds = atoi(argv[5]);
    int bitrate = argc > 7 ? atoi(argv[7]) : 8000000;
    int fps = argc > 8 ? atoi(argv[8]) : 60;
    int maxsize = argc > 9 ? atoi(argv[9]) : 0;

    FILE *jar_file = fopen(argv[4], "rb");
    if (!jar_file) { perror("jar"); return 2; }
    fseek(jar_file, 0, SEEK_END);
    long jar_len = ftell(jar_file);
    fseek(jar_file, 0, SEEK_SET);
    uint8_t *jar = (uint8_t *)malloc((size_t)jar_len);
    if (!jar || fread(jar, 1, (size_t)jar_len, jar_file) != (size_t)jar_len) { fclose(jar_file); return 2; }
    fclose(jar_file);
    printf("jar=%ld bytes\n", jar_len);

    anytv_client *c = anytv_client_new_proto(0);
    if (strcmp(argv[3], "-") != 0) {
        FILE *f = fopen(argv[3], "rb");
        if (f) {
            uint8_t blob[ANYTV_KEY_SIZE];
            size_t n = fread(blob, 1, sizeof(blob), f);
            fclose(f);
            anytv_client_key_load(c, blob, n);
        }
    } else {
        anytv_client_keygen(c);
    }
    int rc = anytv_client_connect(c, argv[1], atoi(argv[2]), 8000, 30000);
    printf("connect rc=%d model=%s\n", rc, anytv_client_model(c));
    if (rc != 0) return 1;

    double t0 = now();
    rc = anytv_client_scrcpy_start(c, jar, (size_t)jar_len, bitrate, fps, maxsize, 20000);
    printf("scrcpy start rc=%d err=%s (%.1fs)\n", rc, anytv_client_error(c), now() - t0);
    if (rc != 0) { anytv_client_free(c); return 1; }
    printf("device=%dx%d\n", anytv_client_scrcpy_width(c), anytv_client_scrcpy_height(c));

    FILE *out = fopen(argv[6], "wb");
    static uint8_t buffer[256 * 1024];
    long total = 0;
    int chunks = 0;
    double started = now();
    while (now() - started < seconds) {
        int got = anytv_client_stream_read(c, buffer, sizeof(buffer), 1200);
        if (got > 0) {
            fwrite(buffer, 1, (size_t)got, out);
            total += got;
            chunks++;
        } else if (got == 0) {
            printf("stream encerrado pelo servidor\n");
            break;
        } else if (got == -5) {
            continue;
        } else {
            printf("erro rc=%d\n", got);
            break;
        }
    }
    anytv_client_stream_stop(c);
    if (out) fclose(out);
    printf("total=%ld bytes em %.1fs (%.1f KB/s) chunks=%d\n",
           total, now() - started, total / 1024.0 / (now() - started), chunks);
    anytv_client_free(c);
    return total > 0 ? 0 : 1;
}
