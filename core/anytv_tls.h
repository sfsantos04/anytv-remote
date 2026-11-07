/* anytv_tls.h - TLS transport abstraction.
 * Linux: OpenSSL (declared by hand, no headers needed). iOS: SecureTransport.
 * Both accept the TV's self-signed certificate, which is what these TVs use. */
#ifndef ANYTV_TLS_H
#define ANYTV_TLS_H

#include <stdint.h>
#include <stddef.h>

typedef struct anytv_tls anytv_tls;

anytv_tls *anytv_tls_connect(const char *host, int port, int timeout_ms,
                             char *err, size_t errlen);
int anytv_tls_send(anytv_tls *t, const void *buf, size_t len);
/* bytes read, 0 closed, -1 error, -2 timeout */
int anytv_tls_recv(anytv_tls *t, void *buf, size_t len, int timeout_ms);
int anytv_tls_fd(anytv_tls *t);
void anytv_tls_close(anytv_tls *t);

#endif
