/* anytv_net.h - POSIX sockets with timeouts, HTTP/1.1 client and Wake-on-LAN.
 * Works unchanged on Linux and iOS (iOS exposes the same BSD socket API). */
#ifndef ANYTV_NET_H
#define ANYTV_NET_H

#include <stdint.h>
#include <stddef.h>

#define NET_OK 0
#define NET_ERR_CONNECT -1
#define NET_ERR_TIMEOUT -2
#define NET_ERR_IO -3
#define NET_ERR_PROTO -4

int net_connect(const char *host, int port, int timeout_ms);
int net_send_all(int fd, const void *buf, size_t len);
/* returns bytes read, 0 on orderly shutdown, NET_ERR_TIMEOUT, NET_ERR_IO */
int net_recv_some(int fd, void *buf, size_t len, int timeout_ms);
int net_recv_all(int fd, void *buf, size_t len, int timeout_ms);
void net_close(int fd);

/* Minimal HTTP/1.1: one request/response, no keep-alive, no chunked bodies
 * (Roku, Philips JointSpace and DIAL all answer with Content-Length). */
typedef struct {
    int status;
    char content_type[64];
    size_t body_len;
} http_response;

int http_request(const char *host, int port, const char *method, const char *path,
                 const char *content_type, const char *body, int timeout_ms,
                 char *out, size_t outlen, http_response *resp);

/* Wake-on-LAN magic packet on the broadcast address. */
int net_wol(const char *mac, int port);

/* "aa:bb:cc:dd:ee:ff" or "aabbccddeeff" -> 6 bytes. Returns 0 on success. */
int net_parse_mac(const char *text, uint8_t out[6]);

#endif
