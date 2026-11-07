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
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <poll.h>
#include <strings.h>

/* SSDP M-SEARCH: every smart TV answers with a SERVER header and a LOCATION,
 * which reveals both the brand and the real IP (port guesses are unreliable). */
static void ssdp_scan(const char *unicast_target, const char *st_query, int wait_ms) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) { printf("ssdp: socket falhou\n"); return; }
    int reuse = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    char msg[512];
    snprintf(msg, sizeof(msg),
             "M-SEARCH * HTTP/1.1\r\n"
             "HOST: 239.255.255.250:1900\r\n"
             "MAN: \"ssdp:discover\"\r\n"
             "MX: 2\r\n"
             "ST: %s\r\n\r\n",
             st_query ? st_query : "ssdp:all");

    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(1900);
    if (unicast_target) {
        dest.sin_addr.s_addr = inet_addr(unicast_target);
    } else {
        dest.sin_addr.s_addr = inet_addr("239.255.255.250");
        unsigned char ttl = 2;
        setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
    }
    for (int i = 0; i < 3; i++) {
        sendto(fd, msg, strlen(msg), 0, (struct sockaddr *)&dest, sizeof(dest));
    }

    char seen[64][32];
    int seen_count = 0;
    long long deadline = 0;
    for (int tick = 0; tick < wait_ms / 100; tick++) {
        struct pollfd pfd = { .fd = fd, .events = POLLIN };
        if (poll(&pfd, 1, 100) <= 0) continue;
        char buf[2048];
        struct sockaddr_in from;
        socklen_t flen = sizeof(from);
        ssize_t n = recvfrom(fd, buf, sizeof(buf) - 1, 0, (struct sockaddr *)&from, &flen);
        if (n <= 0) continue;
        buf[n] = 0;
        char ip[32];
        snprintf(ip, sizeof(ip), "%s", inet_ntoa(from.sin_addr));
        int dup = 0;
        for (int i = 0; i < seen_count; i++) if (strcmp(seen[i], ip) == 0) dup = 1;
        if (dup) continue;
        if (seen_count < 64) snprintf(seen[seen_count++], 32, "%s", ip);

        char server[128] = "", location[256] = "", st[96] = "";
        for (char *line = strtok(buf, "\r\n"); line; line = strtok(NULL, "\r\n")) {
            if (!strncasecmp(line, "SERVER:", 7)) snprintf(server, sizeof(server), "%s", line + 7);
            else if (!strncasecmp(line, "LOCATION:", 9)) snprintf(location, sizeof(location), "%s", line + 9);
            else if (!strncasecmp(line, "ST:", 3)) snprintf(st, sizeof(st), "%s", line + 3);
        }
        printf("%-16s server=%s\n", ip, server[0] ? server : "?");
        if (location[0]) printf("                 location=%s\n", location);
        if (st[0]) printf("                 st=%s\n", st);
    }
    if (seen_count == 0) printf("ssdp: nenhuma resposta (%s)\n",
                                unicast_target ? "unicast" : "multicast");
    close(fd);
    (void)deadline;
}

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
        printf("     %s --ssdp [ip-unicast]\n", argv[0]);
        return 2;
    }
    if (!strcmp(argv[1], "--ssdp")) {
        const char *target = (argc > 2 && strcmp(argv[2], "-") != 0) ? argv[2] : NULL;
        const char *st = argc > 3 ? argv[3] : NULL;
        ssdp_scan(target, st, 4000);
        return 0;
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
