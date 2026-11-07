/* anytv_ws.h - minimal RFC 6455 WebSocket client, plain or TLS. */
#ifndef ANYTV_WS_H
#define ANYTV_WS_H

#include <stdint.h>
#include <stddef.h>
#include "anytv_tls.h"

#define WS_OK 0
#define WS_ERR_CONNECT -1
#define WS_ERR_HANDSHAKE -2
#define WS_ERR_IO -3
#define WS_ERR_TIMEOUT -4

typedef struct {
    int fd;
    anytv_tls *tls;
    int closed;
} ws_conn;

/* use_tls != 0 opens the socket through the TLS layer (Samsung 8002, LG 3001). */
int ws_connect(ws_conn *ws, const char *host, int port, const char *path,
               int use_tls, int timeout_ms, char *err, size_t errlen);
int ws_send_text(ws_conn *ws, const char *text);
int ws_ping(ws_conn *ws);
/* Reads one complete text message (fragments joined, ping answered). */
int ws_recv_text(ws_conn *ws, char *out, size_t outlen, int timeout_ms);
void ws_close(ws_conn *ws);

#endif
