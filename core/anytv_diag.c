/* anytv_diag.c - "what does this TV answer?" tool.
 * Probes the control ports and, for the websocket channels, prints the raw
 * HTTP response of the upgrade so a failure is visible instead of guessed.
 *
 * usage: anytv-diag <ip> [ip ...] */
#ifndef X_NO_MAIN

#include "anytv_net.h"
#include "anytv_ws.h"
#include "anytv_rsa.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef struct {
    int port;
    const char *name;
} port_hint;

static const port_hint PORTS[] = {
    {5555, "ADB (Android TV)"},
    {6466, "Android TV remote v2"},
    {8001, "Samsung Tizen ws"},
    {8002, "Samsung Tizen wss"},
    {55000, "Samsung antigo (AES)"},
    {9999, "Samsung antigo"},
    {8009, "DIAL/cast"},
    {8060, "Roku ECP"},
    {3000, "LG webOS ws"},
    {3001, "LG webOS wss"},
    {1925, "Philips JointSpace"},
};

static void raw_upgrade(const char *host, int port) {
    int fd = net_connect(host, port, 2500);
    if (fd < 0) {
        printf("    upgrade: sem conexao\n");
        return;
    }
    uint8_t nonce[16] = {0};
    bn_random_bytes(nonce, sizeof(nonce));
    char key[32];
    anytv_base64_encode(nonce, sizeof(nonce), key, sizeof(key));
    char req[512];
    int n = snprintf(req, sizeof(req),
                     "GET /api/v2/channels/samsung.remote.control?name=QW55VFY%%3D HTTP/1.1\r\n"
                     "Host: %s:%d\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                     "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n",
                     host, port, key);
    if (net_send_all(fd, req, (size_t)n) != NET_OK) {
        printf("    upgrade: envio falhou\n");
        net_close(fd);
        return;
    }
    char buf[512];
    int got = net_recv_some(fd, buf, sizeof(buf) - 1, 2500);
    if (got <= 0) {
        printf("    upgrade: sem resposta em 2,5s (porta aberta mas muda)\n");
    } else {
        buf[got] = 0;
        for (int i = 0; i < got; i++) if (buf[i] == '\r' || buf[i] == '\n') buf[i] = ' ';
        printf("    upgrade responde: %.200s\n", buf);
    }
    net_close(fd);
}

static void http_get(const char *host, int port, const char *path) {
    char body[2048];
    http_response resp;
    int rc = http_request(host, port, "GET", path, NULL, NULL, 2500,
                          body, sizeof(body), &resp);
    if (rc != NET_OK) {
        printf("    GET %s: falhou (%d)\n", path, rc);
        return;
    }
    printf("    GET %s -> HTTP %d (%u bytes)\n", path, resp.status, (unsigned)resp.body_len);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("uso: %s <ip> [ip ...]\n", argv[0]);
        return 2;
    }
    for (int a = 1; a < argc; a++) {
        const char *host = argv[a];
        printf("== %s\n", host);
        int open_count = 0;
        for (size_t i = 0; i < sizeof(PORTS) / sizeof(PORTS[0]); i++) {
            int fd = net_connect(host, PORTS[i].port, 700);
            if (fd < 0) continue;
            net_close(fd);
            open_count++;
            printf("  %5d aberta  %s\n", PORTS[i].port, PORTS[i].name);
        }
        if (open_count == 0) {
            printf("  nenhuma porta de controle aberta: TV desligada, em outra rede "
                   "ou com o controle remoto externo bloqueado\n");
            continue;
        }
        for (size_t i = 0; i < sizeof(PORTS) / sizeof(PORTS[0]); i++) {
            int fd = net_connect(host, PORTS[i].port, 700);
            if (fd < 0) continue;
            net_close(fd);
            if (PORTS[i].port == 8001 || PORTS[i].port == 8002) {
                printf("  [%d] %s\n", PORTS[i].port, PORTS[i].name);
                raw_upgrade(host, PORTS[i].port);
            } else if (PORTS[i].port == 8060) {
                printf("  [8060] Roku ECP\n");
                http_get(host, 8060, "/query/device-info");
            } else if (PORTS[i].port == 3000 || PORTS[i].port == 3001) {
                printf("  [%d] LG webOS\n", PORTS[i].port);
                raw_upgrade(host, PORTS[i].port);
            }
        }
    }
    printf("\ndica: Samsung precisa de 'Controle remoto' habilitado e da TV na mesma "
           "rede/VLAN do iPhone; o prompt so aparece quando o upgrade do WebSocket "
           "chega ao canal 8001.\n");
    return 0;
}
#endif
