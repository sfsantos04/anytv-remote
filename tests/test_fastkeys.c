/* test_fastkeys.c - mede a latência das teclas pelo canal do scrcpy. */
#include "anytv_facade.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

static double ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

int main(int argc, char **argv) {
    const char *host = argc > 1 ? argv[1] : "192.168.1.40";
    int port = argc > 2 ? atoi(argv[2]) : 5555;

    anytv_client *c = anytv_client_new_proto(ANYTV_PROTO_ADB);
    if (!c) return 1;
    if (anytv_client_connect(c, host, port, 6000, 90000) != 0) {
        printf("connect falhou: %s\n", anytv_client_error(c));
        return 1;
    }
    printf("conectado: %s\n", anytv_client_model(c));

    FILE *jf = fopen("/tmp/scrcpy-server-1.25", "rb");
    if (!jf) { printf("jar nao encontrado\n"); return 1; }
    fseek(jf, 0, SEEK_END); long jl = ftell(jf); fseek(jf, 0, SEEK_SET);
    uint8_t *jar = malloc((size_t)jl);
    if (fread(jar, 1, (size_t)jl, jf) != (size_t)jl) { printf("leitura do jar falhou\n"); return 1; }
    fclose(jf);

    double t0 = ms();
    int rc = anytv_client_fastkeys_start(c, jar, (size_t)jl);
    printf("fastkeys_start rc=%d em %.0f ms (%s)\n", rc, ms() - t0, anytv_client_error(c));
    if (rc != 0) {
        char log[1024];
        anytv_client_shell(c, "cat /data/local/tmp/anytv-fastkeys.log 2>/dev/null | head -6",
                           log, sizeof(log), 8000);
        printf("log do servidor: %.600s\n", log);
        anytv_client_free(c);
        return 1;
    }

    /* mede 8 teclas do direcional pelo canal rápido */
    int codes[8] = {22, 22, 21, 21, 20, 20, 19, 19};
    double sum = 0, worst = 0;
    for (int i = 0; i < 8; i++) {
        double s = ms();
        int r = anytv_client_fastkey(c, codes[i], 1);
        double d = ms() - s;
        sum += d;
        if (d > worst) worst = d;
        printf("  tecla %d: %.1f ms (rc=%d)\n", codes[i], d, r);
    }
    printf("media %.1f ms, pior %.1f ms\n", sum / 8, worst);

    /* prova funcional: o foco tem de mudar ao mandar HOME */
    char out[1024];
    anytv_client_shell(c, "am start -a android.settings.SETTINGS >/dev/null 2>&1", out, sizeof(out), 8000);
    sleep(2);
    anytv_client_shell(c, "dumpsys window 2>/dev/null | grep -m1 mCurrentFocus", out, sizeof(out), 8000);
    printf("foco antes do HOME: %.70s\n", out);
    int r1 = anytv_client_fastkey(c, 3, 1);   /* HOME */
    sleep(2);
    anytv_client_shell(c, "dumpsys window 2>/dev/null | grep -m1 mCurrentFocus", out, sizeof(out), 8000);
    printf("foco depois do HOME (rc=%d): %.70s\n", r1, out);

    anytv_client_free(c);
    return 0;
}
