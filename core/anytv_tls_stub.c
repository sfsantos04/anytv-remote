/* anytv_tls_stub.c - TLS desligado. Serve para builds estaticos de teste (um
 * container Android sem libssl) onde so o transporte ADB e exercitado: qualquer
 * tentativa de TLS falha na hora, em vez de arrastar a OpenSSL para o link. */
#include "anytv_tls.h"
#include <stdio.h>

anytv_tls *anytv_tls_connect(const char *host, int port, int timeout_ms,
                             char *err, size_t errlen) {
    (void)host; (void)port; (void)timeout_ms;
    if (err && errlen) snprintf(err, errlen, "TLS desligado neste build");
    return NULL;
}

int anytv_tls_send(anytv_tls *t, const void *buf, size_t len) {
    (void)t; (void)buf; (void)len;
    return -1;
}

int anytv_tls_recv(anytv_tls *t, void *buf, size_t len, int timeout_ms) {
    (void)t; (void)buf; (void)len; (void)timeout_ms;
    return -1;
}

int anytv_tls_fd(anytv_tls *t) {
    (void)t;
    return -1;
}

void anytv_tls_close(anytv_tls *t) {
    (void)t;
}
