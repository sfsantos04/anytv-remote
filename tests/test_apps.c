/* test_apps.c - exercita a fachada: lista de apps com icone contra um aparelho real.
 * uso: ./test_apps <proto 0=adb 1=roku 2=webos 3=tizen> <host> <port> <key file|-> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../core/anytv_facade.h"

int main(int argc, char **argv) {
    if (argc < 5) {
        fprintf(stderr, "uso: %s <proto> <host> <port> <key|->\n", argv[0]);
        return 2;
    }
    int proto = atoi(argv[1]);
    const char *host = argv[2];
    int port = atoi(argv[3]);

    anytv_client *c = anytv_client_new_proto(proto);
    if (!c) return 3;
    if (strcmp(argv[4], "-") != 0) {
        FILE *f = fopen(argv[4], "rb");
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
    printf("connect rc=%d proto=%s model=%s err=%s\n", rc, anytv_client_proto_name(c),
           anytv_client_model(c), anytv_client_error(c));
    if (rc != 0) { anytv_client_free(c); return 1; }

    static char buf[96 * 1024];
    rc = anytv_client_apps(c, buf, sizeof(buf));
    printf("apps rc=%d err=%s\n", rc, anytv_client_error(c));
    if (rc == 0) {
        int count = 0, with_icon = 0;
        char *line = strtok(buf, "\n");
        while (line) {
            char *name = strchr(line, '\t');
            char *icon = NULL;
            if (name) {
                *name++ = 0;
                icon = strchr(name, '\t');
                if (icon) *icon++ = 0;
            }
            printf("  id=%-34s name=%-22s icon=%s\n", line,
                   name ? name : "", (icon && *icon) ? icon : "(sem icone)");
            count++;
            if (icon && *icon) with_icon++;
            line = strtok(NULL, "\n");
        }
        printf("total=%d com_icone=%d\n", count, with_icon);
    }
    anytv_client_free(c);
    return rc == 0 ? 0 : 1;
}
