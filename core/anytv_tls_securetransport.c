/* anytv_tls_securetransport.c - TLS for the iOS build (SecureTransport).
 * The TVs present self-signed certificates, so the server auth step is accepted
 * without verification; that is what every remote-control app does. */
#ifndef ANYTV_HOST_LINUX

#include "anytv_tls.h"
#include "anytv_net.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <Security/SecureTransport.h>
#include <Security/Security.h>

struct anytv_tls {
    int fd;
    SSLContextRef ctx;
    int closed;
};

static OSStatus io_read(SSLConnectionRef connection, void *data, size_t *dataLength) {
    int fd = (int)(intptr_t)connection;
    size_t want = *dataLength;
    ssize_t got = read(fd, data, want);
    if (got > 0) {
        *dataLength = (size_t)got;
        return (size_t)got < want ? errSSLWouldBlock : noErr;
    }
    if (got == 0) {
        *dataLength = 0;
        return errSSLClosedGraceful;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
        *dataLength = 0;
        return errSSLWouldBlock;
    }
    *dataLength = 0;
    return errSSLClosedAbort;
}

static OSStatus io_write(SSLConnectionRef connection, const void *data, size_t *dataLength) {
    int fd = (int)(intptr_t)connection;
    size_t want = *dataLength;
    ssize_t sent = write(fd, data, want);
    if (sent > 0) {
        *dataLength = (size_t)sent;
        return (size_t)sent < want ? errSSLWouldBlock : noErr;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
        *dataLength = 0;
        return errSSLWouldBlock;
    }
    *dataLength = 0;
    return errSSLClosedAbort;
}

anytv_tls *anytv_tls_connect(const char *host, int port, int timeout_ms,
                             char *err, size_t errlen) {
    if (err && errlen) err[0] = 0;
    int fd = net_connect(host, port, timeout_ms);
    if (fd < 0) {
        if (err && errlen) snprintf(err, errlen, "sem conexao TLS em %s:%d", host, port);
        return NULL;
    }
    anytv_tls *t = (anytv_tls *)calloc(1, sizeof(anytv_tls));
    if (!t) { net_close(fd); return NULL; }
    t->fd = fd;
    t->ctx = SSLCreateContext(NULL, kSSLClientSide, kSSLStreamType);
    if (!t->ctx) { net_close(fd); free(t); return NULL; }

    SSLSetIOFuncs(t->ctx, io_read, io_write);
    SSLSetConnection(t->ctx, (SSLConnectionRef)(intptr_t)fd);
    SSLSetPeerDomainName(t->ctx, host, strlen(host));
    SSLSetSessionOption(t->ctx, kSSLSessionOptionBreakOnServerAuth, true);
    /* these TVs use self-signed certificates; with BreakOnServerAuth the
     * handshake resumes after the server certificate arrives, which accepts it */

    for (int attempt = 0; attempt < 8; attempt++) {
        OSStatus st = SSLHandshake(t->ctx);
        if (st == noErr) return t;
        if (st == errSSLServerAuthCompleted) continue; /* accept the TV certificate */
        if (st == errSSLWouldBlock) continue;
        if (err && errlen) snprintf(err, errlen, "TLS handshake falhou (%d)", (int)st);
        anytv_tls_close(t);
        return NULL;
    }
    if (err && errlen) snprintf(err, errlen, "TLS handshake sem progresso");
    anytv_tls_close(t);
    return NULL;
}

int anytv_tls_send(anytv_tls *t, const void *buf, size_t len) {
    if (!t || !t->ctx) return -1;
    const uint8_t *p = (const uint8_t *)buf;
    size_t sent = 0;
    while (sent < len) {
        size_t written = 0;
        OSStatus st = SSLWrite(t->ctx, p + sent, len - sent, &written);
        sent += written;
        if (st == noErr) continue;
        if (st == errSSLWouldBlock) continue;
        return -1;
    }
    return (int)sent;
}

int anytv_tls_recv(anytv_tls *t, void *buf, size_t len, int timeout_ms) {
    if (!t || !t->ctx) return -1;
    for (int spin = 0; spin < timeout_ms / 10 + 1; spin++) {
        size_t got = 0;
        OSStatus st = SSLRead(t->ctx, buf, len, &got);
        if (got > 0) return (int)got;
        if (st == errSSLWouldBlock) {
            usleep(10 * 1000);
            continue;
        }
        if (st == errSSLClosedGraceful || st == errSSLClosedNoNotify) return 0;
        return -1;
    }
    return -2;
}

int anytv_tls_fd(anytv_tls *t) { return t ? t->fd : -1; }

void anytv_tls_close(anytv_tls *t) {
    if (!t) return;
    if (t->ctx) {
        SSLClose(t->ctx);
        CFRelease(t->ctx);
    }
    if (t->fd >= 0) net_close(t->fd);
    free(t);
}

#endif /* !ANYTV_HOST_LINUX */
