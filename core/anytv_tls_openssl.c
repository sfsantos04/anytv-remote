/* anytv_tls_openssl.c - TLS for the Linux build.
 * The container has libssl.so.3 but no headers, so the handful of symbols we
 * need are declared here instead of including <openssl/ssl.h>. */
#ifdef ANYTV_HOST_LINUX

#define _POSIX_C_SOURCE 200809L
#include "anytv_tls.h"
#include "anytv_net.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <poll.h>
#include <unistd.h>
#include <fcntl.h>

/* ---- minimal OpenSSL declarations (opaque types) ---- */
typedef struct ssl_ctx_st SSL_CTX;
typedef struct ssl_st SSL;
extern const void *TLS_client_method(void);
extern SSL_CTX *SSL_CTX_new(const void *method);
extern void SSL_CTX_free(SSL_CTX *ctx);
extern SSL *SSL_new(SSL_CTX *ctx);
extern void SSL_free(SSL *ssl);
extern int SSL_set_fd(SSL *ssl, int fd);
extern int SSL_connect(SSL *ssl);
extern int SSL_read(SSL *ssl, void *buf, int num);
extern int SSL_write(SSL *ssl, const void *buf, int num);
extern int SSL_get_error(const SSL *ssl, int ret);
extern void SSL_CTX_set_verify(SSL_CTX *ctx, int mode, void *cb);
extern long SSL_ctrl(SSL *ssl, int cmd, long larg, void *parg);

#define SSL_CTRL_SET_TLSEXT_HOSTNAME 55
#define TLSEXT_NAMETYPE_host_name 0
#define SSL_ERROR_WANT_READ 2
#define SSL_ERROR_WANT_WRITE 3
#define SSL_ERROR_ZERO_RETURN 6

struct anytv_tls {
    int fd;
    SSL_CTX *ctx;
    SSL *ssl;
};

static int wait_io(int fd, int for_write, int timeout_ms) {
    struct pollfd pfd = { .fd = fd, .events = for_write ? POLLOUT : POLLIN };
    int rc = poll(&pfd, 1, timeout_ms);
    if (rc == 0) return -2;
    if (rc < 0) return -1;
    return 0;
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
    /* non-blocking: SSL_connect must return WANT_READ instead of blocking
     * forever when the peer never answers (a plain HTTP port, for instance) */
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    t->ctx = SSL_CTX_new(TLS_client_method());
    if (!t->ctx) { net_close(fd); free(t); return NULL; }
    SSL_CTX_set_verify(t->ctx, 0 /* SSL_VERIFY_NONE: TV certificate is self-signed */, NULL);
    t->ssl = SSL_new(t->ctx);
    if (!t->ssl) { SSL_CTX_free(t->ctx); net_close(fd); free(t); return NULL; }
    SSL_set_fd(t->ssl, fd);
    SSL_ctrl(t->ssl, SSL_CTRL_SET_TLSEXT_HOSTNAME, TLSEXT_NAMETYPE_host_name, (void *)host);

    for (;;) {
        int rc = SSL_connect(t->ssl);
        if (rc == 1) break;
        int e = SSL_get_error(t->ssl, rc);
        if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) {
            if (wait_io(fd, e == SSL_ERROR_WANT_WRITE, timeout_ms) != 0) {
                if (err && errlen) snprintf(err, errlen, "TLS timeout no handshake");
                anytv_tls_close(t);
                return NULL;
            }
            continue;
        }
        if (err && errlen) snprintf(err, errlen, "TLS handshake falhou (erro %d)", e);
        anytv_tls_close(t);
        return NULL;
    }
    return t;
}

int anytv_tls_send(anytv_tls *t, const void *buf, size_t len) {
    if (!t || !t->ssl) return -1;
    const uint8_t *p = (const uint8_t *)buf;
    size_t sent = 0;
    while (sent < len) {
        int rc = SSL_write(t->ssl, p + sent, (int)(len - sent));
        if (rc > 0) { sent += (size_t)rc; continue; }
        int e = SSL_get_error(t->ssl, rc);
        if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) {
            int for_write = e == SSL_ERROR_WANT_WRITE;
            if (wait_io(t->fd, for_write, 4000) == -2) return -2;
            continue;
        }
        return -1;
    }
    return (int)sent;
}

int anytv_tls_recv(anytv_tls *t, void *buf, size_t len, int timeout_ms) {
    if (!t || !t->ssl) return -1;
    for (;;) {
        int rc = SSL_read(t->ssl, buf, (int)len);
        if (rc > 0) return rc;
        int e = SSL_get_error(t->ssl, rc);
        if (e == SSL_ERROR_ZERO_RETURN) return 0;
        if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) {
            int w = wait_io(t->fd, e == SSL_ERROR_WANT_WRITE, timeout_ms);
            if (w == -2) return -2;
            if (w < 0) return -1;
            continue;
        }
        return -1;
    }
}

int anytv_tls_fd(anytv_tls *t) { return t ? t->fd : -1; }

void anytv_tls_close(anytv_tls *t) {
    if (!t) return;
    if (t->ssl) SSL_free(t->ssl);
    if (t->ctx) SSL_CTX_free(t->ctx);
    if (t->fd >= 0) net_close(t->fd);
    free(t);
}

#endif /* ANYTV_HOST_LINUX */
