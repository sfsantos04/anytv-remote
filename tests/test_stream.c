/* test_stream.c - prova o stream ao vivo: puxa H.264 do encoder do aparelho
 * e grava em arquivo para ser validado por um decodificador independente.
 * uso: ./test_stream <host> <port> <key|-> <segundos> <arquivo.264> [largura altura bitrate] */
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
    if (argc < 6) {
        fprintf(stderr, "uso: %s <host> <port> <key|-> <segundos> <arquivo> [w h bitrate]\n", argv[0]);
        return 2;
    }
    const char *host = argv[1];
    int port = atoi(argv[2]);
    int seconds = atoi(argv[4]);
    int w = argc > 6 ? atoi(argv[6]) : 640;
    int h = argc > 7 ? atoi(argv[7]) : 360;
    int br = argc > 8 ? atoi(argv[8]) : 2000000;

    anytv_client *c = anytv_client_new_proto(0);
    if (!c) return 3;
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
    int rc = anytv_client_connect(c, host, port, 8000, 30000);
    printf("connect rc=%d model=%s err=%s\n", rc, anytv_client_model(c), anytv_client_error(c));
    if (rc != 0) { anytv_client_free(c); return 1; }

    rc = anytv_client_stream_start(c, w, h, br, seconds + 2);
    printf("stream start rc=%d err=%s\n", rc, anytv_client_error(c));
    if (rc != 0) { anytv_client_free(c); return 1; }

    FILE *out = fopen(argv[5], "wb");
    if (!out) { anytv_client_stream_stop(c); anytv_client_free(c); return 4; }

    static uint8_t buffer[256 * 1024];
    double started = now();
    double last_report = started;
    long total = 0;
    int chunks = 0, zero_reads = 0;
    while (now() - started < seconds) {
        int got = anytv_client_stream_read(c, buffer, sizeof(buffer), 1500);
        if (got > 0) {
            fwrite(buffer, 1, (size_t)got, out);
            total += got;
            chunks++;
        } else if (got == 0) {
            break;
        } else if (got == -5) { /* timeout: keep waiting until the deadline */
            if (++zero_reads % 8 == 0) printf("  ... sem dados (%d timeouts)\n", zero_reads);
            continue;
        } else {
            printf("read erro rc=%d\n", got);
            break;
        }
        double t = now();
        if (t - last_report >= 1.0) {
            printf("  %.1fs  %ld bytes  %.1f KB/s\n", t - started, total, total / 1024.0 / (t - started));
            last_report = t;
        }
    }
    anytv_client_stream_stop(c);
    fclose(out);
    printf("total=%ld bytes em %.1fs (%.1f KB/s), chunks=%d\n", total, now() - started,
           total / 1024.0 / (now() - started), chunks);
    anytv_client_free(c);
    return total > 0 ? 0 : 1;
}
