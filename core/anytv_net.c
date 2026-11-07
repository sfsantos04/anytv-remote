/* anytv_net.c - sockets, HTTP and Wake-on-LAN for the non-ADB transports. */
#define _POSIX_C_SOURCE 200809L
#include "anytv_net.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>

int net_connect(const char *host, int port, int timeout_ms) {
    struct addrinfo hints, *res = NULL, *rp;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    char portstr[16];
    snprintf(portstr, sizeof(portstr), "%d", port);
    if (getaddrinfo(host, portstr, &hints, &res) != 0) return NET_ERR_CONNECT;

    int fd = -1;
    for (rp = res; rp; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;
        int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        int rc = connect(fd, rp->ai_addr, rp->ai_addrlen);
        if (rc != 0 && errno == EINPROGRESS) {
            struct pollfd pfd = { .fd = fd, .events = POLLOUT };
            if (poll(&pfd, 1, timeout_ms) == 1) {
                int soerr = 0;
                socklen_t slen = sizeof(soerr);
                getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &slen);
                rc = soerr == 0 ? 0 : -1;
            } else {
                rc = -1;
            }
        }
        if (rc == 0) {
            fcntl(fd, F_SETFL, flags);
            int one = 1;
            setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
            break;
        }
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd >= 0 ? fd : NET_ERR_CONNECT;
}

int net_send_all(int fd, const void *buf, size_t len) {
    const uint8_t *p = (const uint8_t *)buf;
    size_t sent = 0;
    while (sent < len) {
        ssize_t w = send(fd, p + sent, len - sent, 0);
        if (w < 0) {
            if (errno == EINTR) continue;
            return NET_ERR_IO;
        }
        sent += (size_t)w;
    }
    return NET_OK;
}

int net_recv_some(int fd, void *buf, size_t len, int timeout_ms) {
    struct pollfd pfd = { .fd = fd, .events = POLLIN };
    for (;;) {
        int pr = poll(&pfd, 1, timeout_ms);
        if (pr == 0) return NET_ERR_TIMEOUT;
        if (pr < 0) {
            if (errno == EINTR) continue;
            return NET_ERR_IO;
        }
        ssize_t r = recv(fd, buf, len, 0);
        if (r < 0) {
            if (errno == EINTR) continue;
            return NET_ERR_IO;
        }
        return (int)r;
    }
}

int net_recv_all(int fd, void *buf, size_t len, int timeout_ms) {
    uint8_t *p = (uint8_t *)buf;
    size_t got = 0;
    while (got < len) {
        int r = net_recv_some(fd, p + got, len - got, timeout_ms);
        if (r <= 0) return r == 0 ? NET_ERR_IO : r;
        got += (size_t)r;
    }
    return NET_OK;
}

void net_close(int fd) {
    if (fd >= 0) close(fd);
}

static int parse_status(const char *headers) {
    const char *sp = strchr(headers, ' ');
    if (!sp) return -1;
    return atoi(sp + 1);
}

int http_request(const char *host, int port, const char *method, const char *path,
                 const char *content_type, const char *body, int timeout_ms,
                 char *out, size_t outlen, http_response *resp) {
    int fd = net_connect(host, port, timeout_ms);
    if (fd < 0) return NET_ERR_CONNECT;

    size_t body_len = body ? strlen(body) : 0;
    char head[1024];
    int n = snprintf(head, sizeof(head),
                     "%s %s HTTP/1.1\r\nHost: %s:%d\r\nConnection: close\r\n"
                     "Accept: */*\r\nUser-Agent: AnyTV/1.1\r\n",
                     method, path, host, port);
    if (n < 0 || (size_t)n >= sizeof(head)) { net_close(fd); return NET_ERR_PROTO; }
    if (body && content_type) {
        int more = snprintf(head + n, sizeof(head) - (size_t)n,
                            "Content-Type: %s\r\nContent-Length: %zu\r\n",
                            content_type, body_len);
        if (more < 0) { net_close(fd); return NET_ERR_PROTO; }
        n += more;
    }
    int tail = snprintf(head + n, sizeof(head) - (size_t)n, "\r\n");
    if (tail < 0) { net_close(fd); return NET_ERR_PROTO; }
    n += tail;

    if (net_send_all(fd, head, (size_t)n) != NET_OK ||
        (body_len && net_send_all(fd, body, body_len) != NET_OK)) {
        net_close(fd);
        return NET_ERR_IO;
    }

    /* read everything the peer sends (Connection: close) */
    size_t cap = 64 * 1024;
    char *raw = (char *)malloc(cap);
    if (!raw) { net_close(fd); return NET_ERR_IO; }
    size_t got = 0;
    for (;;) {
        if (got + 4096 > cap) {
            size_t bigger = cap * 2;
            char *tmp = (char *)realloc(raw, bigger);
            if (!tmp) { free(raw); net_close(fd); return NET_ERR_IO; }
            raw = tmp;
            cap = bigger;
        }
        int r = net_recv_some(fd, raw + got, cap - got - 1, timeout_ms);
        if (r == NET_ERR_TIMEOUT) break;
        if (r <= 0) break;
        got += (size_t)r;
    }
    net_close(fd);
    raw[got] = 0;

    char *sep = strstr(raw, "\r\n\r\n");
    if (!sep) { free(raw); return NET_ERR_PROTO; }
    *sep = 0;
    int status = parse_status(raw);
    size_t header_len = (size_t)(sep - raw) + 4;
    if (resp) {
        resp->status = status;
        resp->content_type[0] = 0;
        resp->body_len = got > header_len ? got - header_len : 0;
        const char *ct = strstr(raw, "Content-Type:");
        if (ct) {
            ct += strlen("Content-Type:");
            while (*ct == ' ') ct++;
            size_t i = 0;
            while (*ct && *ct != '\r' && *ct != '\n' && i < sizeof(resp->content_type) - 1) {
                resp->content_type[i++] = *ct++;
            }
            resp->content_type[i] = 0;
        }
    }
    size_t copy = got > header_len ? got - header_len : 0;
    if (out && outlen) {
        size_t take = copy < outlen - 1 ? copy : outlen - 1;
        memcpy(out, raw + header_len, take);
        out[take] = 0;
    }
    free(raw);
    return NET_OK;
}

int net_parse_mac(const char *text, uint8_t out[6]) {
    uint8_t bytes[6];
    int count = 0;
    const char *p = text;
    while (*p && count < 6) {
        while (*p == ':' || *p == '-' || *p == '.') p++;
        if (!isxdigit((unsigned char)p[0])) break;
        int hi = isdigit((unsigned char)p[0]) ? p[0] - '0'
                 : tolower((unsigned char)p[0]) - 'a' + 10;
        int lo = 0;
        if (isxdigit((unsigned char)p[1])) {
            lo = isdigit((unsigned char)p[1]) ? p[1] - '0'
                 : tolower((unsigned char)p[1]) - 'a' + 10;
            p += 2;
        } else {
            p += 1;
        }
        bytes[count++] = (uint8_t)((hi << 4) | lo);
    }
    if (count != 6) return -1;
    memcpy(out, bytes, 6);
    return 0;
}

int net_wol(const char *mac, int port) {
    uint8_t addr[6];
    if (net_parse_mac(mac, addr) != 0) return NET_ERR_PROTO;

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return NET_ERR_IO;
    int broadcast = 1;
    setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));

    uint8_t packet[102];
    memset(packet, 0xff, 6);
    for (int i = 0; i < 16; i++) memcpy(packet + 6 + i * 6, addr, 6);

    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons((uint16_t)(port > 0 ? port : 9));
    dest.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    ssize_t sent = sendto(fd, packet, sizeof(packet), 0,
                          (struct sockaddr *)&dest, sizeof(dest));
    close(fd);
    return sent == (ssize_t)sizeof(packet) ? NET_OK : NET_ERR_IO;
}
