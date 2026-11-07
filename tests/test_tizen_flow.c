/* test_tizen_flow.c - repete a sequência que o app faz na Samsung:
 * conectar, info, probe de apps (abre o sdb), mandar tecla, listar apps, abrir app.
 * Rodar com -fsanitize=address para encontrar estouro/uso de memória liberada.
 * uso: ./test_tizen_flow <host> <port> <token> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../core/anytv_facade.h"

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "uso: %s <host> <porta> [token]\n", argv[0]);
        return 2;
    }
    anytv_client *c = anytv_client_new_proto(3);
    if (!c) return 3;
    if (argc > 3 && argv[3][0]) anytv_client_set_token(c, argv[3]);

    printf("1. connect\n");
    int rc = anytv_client_connect(c, argv[1], atoi(argv[2]), 6000, 30000);
    printf("   rc=%d caps=0x%x err=%s\n", rc, anytv_client_caps(c), anytv_client_error(c));
    if (rc != 0) { anytv_client_free(c); return 1; }

    printf("2. info\n");
    char info[4096];
    rc = anytv_client_info(c, info, sizeof(info));
    printf("   rc=%d info=%.80s\n", rc, info);

    printf("3. probe apps (sdb)\n");
    printf("   probe=%d caps=0x%x err=%s\n", anytv_client_probe_apps(c),
           anytv_client_caps(c), anytv_client_error(c));

    printf("4. tecla UP\n");
    rc = anytv_client_cmd(c, 0 /* up */, 1);
    printf("   rc=%d err=%s\n", rc, anytv_client_error(c));

    printf("5. tecla OK\n");
    rc = anytv_client_cmd(c, 4, 1);
    printf("   rc=%d err=%s\n", rc, anytv_client_error(c));

    printf("6. lista de apps\n");
    static char apps[96 * 1024];
    rc = anytv_client_apps(c, apps, sizeof(apps));
    int n = 0;
    for (char *p = apps; rc == 0 && *p; p++) if (*p == '\n') n++;
    printf("   rc=%d apps=%d err=%s\n", rc, n, anytv_client_error(c));

    printf("7. token guardado = %.20s\n", anytv_client_token(c));
    anytv_client_disconnect(c);
    anytv_client_free(c);
    printf("fim sem crash\n");
    return 0;
}
