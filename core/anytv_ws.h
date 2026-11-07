/* anytv_ws.h - minimal RFC 6455 WebSocket client (no extensions, no TLS). */
#ifndef ANYTV_WS_H
#define ANYTV_WS_H

#include <stdint.h>
#include <stddef.h>

#define WS_OK 0
#define WS_ERR_CONNECT -1
#define WS_ERR_HANDSHAKE -2
#define WS_ERR_IO -3
#define WS_ERR_TIMEOUT -4

typedef struct {
    int fd;
    int closed;
} ws_conn;

int ws_connect(ws_conn *ws, const char *host, int port, const char *path,
               int timeout_ms, char *err, size_t errlen);
int ws_send_text(ws_conn *ws, const char *text);
/* Reads one complete text message (fragments joined, ping answered). */
int ws_recv_text(ws_conn *ws, char *out, size_t outlen, int timeout_ms);
void ws_close(ws_conn *ws);

#endif
