/* anytv_ws.c - WebSocket client used by the webOS and Tizen transports. */
#define _POSIX_C_SOURCE 200809L
#include "anytv_ws.h"
#include "anytv_net.h"
#include "anytv_sha1.h"
#include "anytv_rsa.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char WS_GUID[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

static int accept_for_key(const char *key, char *out, size_t outlen) {
    char buf[128];
    int n = snprintf(buf, sizeof(buf), "%s%s", key, WS_GUID);
    if (n < 0) return -1;
    uint8_t digest[20];
    sha1_digest(buf, (size_t)n, digest);
    return anytv_base64_encode(digest, sizeof(digest), out, outlen) < 0 ? -1 : 0;
}

int ws_connect(ws_conn *ws, const char *host, int port, const char *path,
               int timeout_ms, char *err, size_t errlen) {
    ws->fd = -1;
    ws->closed = 0;
    if (err && errlen) err[0] = 0;

    uint8_t nonce[16];
    if (bn_random_bytes(nonce, sizeof(nonce)) != 0) return WS_ERR_CONNECT;
    char key[32];
    if (anytv_base64_encode(nonce, sizeof(nonce), key, sizeof(key)) < 0) return WS_ERR_CONNECT;

    ws->fd = net_connect(host, port, timeout_ms);
    if (ws->fd < 0) {
        if (err && errlen) snprintf(err, errlen, "sem conexao em %s:%d", host, port);
        return WS_ERR_CONNECT;
    }

    char req[768];
    int n = snprintf(req, sizeof(req),
                     "GET %s HTTP/1.1\r\nHost: %s:%d\r\nUpgrade: websocket\r\n"
                     "Connection: Upgrade\r\nSec-WebSocket-Key: %s\r\n"
                     "Sec-WebSocket-Version: 13\r\n\r\n",
                     path, host, port, key);
    if (n < 0 || (size_t)n >= sizeof(req)) { ws_close(ws); return WS_ERR_HANDSHAKE; }
    if (net_send_all(ws->fd, req, (size_t)n) != NET_OK) { ws_close(ws); return WS_ERR_IO; }

    /* read the HTTP response headers */
    char resp[2048];
    size_t got = 0;
    while (got < sizeof(resp) - 1) {
        int r = net_recv_some(ws->fd, resp + got, 1, timeout_ms);
        if (r <= 0) {
            if (err && errlen) snprintf(err, errlen, "sem resposta do handshake");
            ws_close(ws);
            return r == NET_ERR_TIMEOUT ? WS_ERR_TIMEOUT : WS_ERR_HANDSHAKE;
        }
        got += (size_t)r;
        resp[got] = 0;
        if (got >= 4 && strcmp(resp + got - 4, "\r\n\r\n") == 0) break;
    }

    if (!strstr(resp, "101")) {
        if (err && errlen) {
            const char *eol = strchr(resp, '\r');
            size_t len = eol ? (size_t)(eol - resp) : strlen(resp);
            if (len > errlen - 1) len = errlen - 1;
            memcpy(err, resp, len);
            err[len] = 0;
        }
        ws_close(ws);
        return WS_ERR_HANDSHAKE;
    }

    char expect[64];
    if (accept_for_key(key, expect, sizeof(expect)) != 0) { ws_close(ws); return WS_ERR_HANDSHAKE; }
    if (!strstr(resp, expect)) {
        if (err && errlen) snprintf(err, errlen, "Sec-WebSocket-Accept invalido");
        ws_close(ws);
        return WS_ERR_HANDSHAKE;
    }
    return WS_OK;
}

static int send_frame(ws_conn *ws, uint8_t opcode, const uint8_t *payload, size_t len) {
    uint8_t header[14];
    size_t hlen = 2;
    header[0] = (uint8_t)(0x80 | opcode);
    if (len < 126) {
        header[1] = (uint8_t)(0x80 | len);
    } else if (len < 65536) {
        header[1] = 0x80 | 126;
        header[2] = (uint8_t)(len >> 8);
        header[3] = (uint8_t)(len & 0xff);
        hlen = 4;
    } else {
        header[1] = 0x80 | 127;
        for (int i = 0; i < 8; i++) header[2 + i] = (uint8_t)((len >> (56 - 8 * i)) & 0xff);
        hlen = 10;
    }
    uint8_t mask[4];
    if (bn_random_bytes(mask, sizeof(mask)) != 0) return WS_ERR_IO;
    memcpy(header + hlen, mask, 4);
    hlen += 4;

    uint8_t *frame = (uint8_t *)malloc(hlen + len);
    if (!frame) return WS_ERR_IO;
    memcpy(frame, header, hlen);
    for (size_t i = 0; i < len; i++) frame[hlen + i] = payload[i] ^ mask[i % 4];
    int rc = net_send_all(ws->fd, frame, hlen + len);
    free(frame);
    return rc == NET_OK ? WS_OK : WS_ERR_IO;
}

int ws_send_text(ws_conn *ws, const char *text) {
    if (!ws || ws->fd < 0 || ws->closed) return WS_ERR_IO;
    return send_frame(ws, 0x1, (const uint8_t *)text, strlen(text));
}

static int read_exact(ws_conn *ws, uint8_t *buf, size_t len, int timeout_ms) {
    int r = net_recv_all(ws->fd, buf, len, timeout_ms);
    if (r == NET_OK) return WS_OK;
    return r == NET_ERR_TIMEOUT ? WS_ERR_TIMEOUT : WS_ERR_IO;
}

/* Reads a frame; returns payload size (may be 0), sets opcode. */
static int read_frame(ws_conn *ws, uint8_t *opcode, uint8_t *buf, size_t cap,
                      int timeout_ms) {
    uint8_t head[2];
    int rc = read_exact(ws, head, 2, timeout_ms);
    if (rc != WS_OK) return rc;
    *opcode = (uint8_t)(head[0] & 0x0f);
    int masked = head[1] & 0x80;
    uint64_t len = head[1] & 0x7f;
    if (len == 126) {
        uint8_t ext[2];
        if ((rc = read_exact(ws, ext, 2, timeout_ms)) != WS_OK) return rc;
        len = ((uint64_t)ext[0] << 8) | ext[1];
    } else if (len == 127) {
        uint8_t ext[8];
        if ((rc = read_exact(ws, ext, 8, timeout_ms)) != WS_OK) return rc;
        len = 0;
        for (int i = 0; i < 8; i++) len = (len << 8) | ext[i];
    }
    uint8_t mask[4] = {0, 0, 0, 0};
    if (masked) {
        if ((rc = read_exact(ws, mask, 4, timeout_ms)) != WS_OK) return rc;
    }
    if (len > cap) len = cap;
    if (len) {
        if ((rc = read_exact(ws, buf, (size_t)len, timeout_ms)) != WS_OK) return rc;
        if (masked) {
            for (uint64_t i = 0; i < len; i++) buf[i] ^= mask[i % 4];
        }
    }
    return (int)len;
}

int ws_recv_text(ws_conn *ws, char *out, size_t outlen, int timeout_ms) {
    if (!ws || ws->fd < 0) return WS_ERR_IO;
    uint8_t *buf = (uint8_t *)malloc(outlen);
    if (!buf) return WS_ERR_IO;
    size_t used = 0;
    int rc = WS_ERR_IO;
    for (;;) {
        uint8_t opcode = 0;
        int n = read_frame(ws, &opcode, buf, outlen - used - 1, timeout_ms);
        if (n < 0) { rc = n; break; }
        if (opcode == 0x8) { /* close */
            ws->closed = 1;
            if (used) break;
            rc = WS_ERR_IO;
            break;
        }
        if (opcode == 0x9) { /* ping -> pong */
            send_frame(ws, 0xA, buf, (size_t)n);
            continue;
        }
        if (opcode == 0xA) continue;
        if (n > 0 && used + (size_t)n < outlen) {
            memcpy((uint8_t *)out + used, buf, (size_t)n);
            used += (size_t)n;
        }
        /* FIN bit: read_frame drops it, so a short read means "keep reading"
         * only when the peer fragments; protocols we speak send whole messages. */
        rc = (int)used;
        break;
    }
    free(buf);
    if (rc >= 0) out[used] = 0;
    return rc;
}

void ws_close(ws_conn *ws) {
    if (!ws) return;
    if (ws->fd >= 0) net_close(ws->fd);
    ws->fd = -1;
    ws->closed = 1;
}
