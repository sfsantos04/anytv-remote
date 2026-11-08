/* anytv_adb.c - ADB host-side protocol: CNXN/AUTH handshake, services, sync pull.
 * Wire format (little endian):
 *   struct message { u32 command, arg0, arg1, data_length, data_check, magic; }
 * magic is the bitwise inverse of command. */
#ifdef ANYTV_HOST_LINUX
#define _POSIX_C_SOURCE 200809L
#endif

#include "anytv_adb.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef ANYTV_HOST_LINUX
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <errno.h>
#endif

#define A_SYNC 0x434e5953u
#define A_CNXN 0x4e584e43u
#define A_OPEN 0x4e45504fu
#define A_OKAY 0x59414b4fu
#define A_CLSE 0x45534c43u
#define A_WRTE 0x45545257u
#define A_AUTH 0x48545541u
#define A_STLS 0x534c5453u

#define ADB_AUTH_TOKEN 1
#define ADB_AUTH_SIGNATURE 2
#define ADB_AUTH_RSAPUBLICKEY 3

#define ADB_VERSION 0x01000001u

static void set_err(adb_conn *c, int code, const char *msg) {
    c->last_error = code;
    snprintf(c->last_error_text, sizeof(c->last_error_text), "%s", msg ? msg : "");
}

static uint32_t get_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void put_le32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static int write_all(adb_conn *c, const void *buf, size_t len) {
    const uint8_t *p = (const uint8_t *)buf;
    while (len) {
#ifdef ANYTV_HOST_LINUX
        ssize_t w = send(c->fd, p, len, 0);
        if (w <= 0) {
            if (w < 0 && (errno == EINTR)) continue;
            return -1;
        }
#else
        extern int anytv_tcp_send(int fd, const void *buf, size_t len);
        int w = anytv_tcp_send(c->fd, p, len);
        if (w <= 0) return -1;
#endif
        p += w;
        len -= (size_t)w;
    }
    return 0;
}

static int read_all(adb_conn *c, void *buf, size_t len, int timeout_ms) {
    uint8_t *p = (uint8_t *)buf;
    while (len) {
#ifdef ANYTV_HOST_LINUX
        struct pollfd pfd = { .fd = c->fd, .events = POLLIN };
        int pr = poll(&pfd, 1, timeout_ms);
        if (pr == 0) return -2;
        if (pr < 0) { if (errno == EINTR) continue; return -1; }
        ssize_t r = recv(c->fd, p, len, 0);
        if (r <= 0) {
            if (r < 0 && errno == EINTR) continue;
            return -1;
        }
#else
        extern int anytv_tcp_recv(int fd, void *buf, size_t len, int timeout_ms);
        int r = anytv_tcp_recv(c->fd, p, len, timeout_ms);
        if (r == 0) return 1;      /* peer closed */
        if (r < 0) return -1;
#endif
        p += r;
        len -= (size_t)r;
    }
    return 0;
}

static int send_msg(adb_conn *c, uint32_t cmd, uint32_t a0, uint32_t a1,
                    const void *data, uint32_t len) {
    uint8_t hdr[24];
    uint32_t checksum = 0;
    const uint8_t *d = (const uint8_t *)data;
    for (uint32_t i = 0; i < len; i++) checksum += d[i];
    put_le32(hdr + 0, cmd);
    put_le32(hdr + 4, a0);
    put_le32(hdr + 8, a1);
    put_le32(hdr + 12, len);
    put_le32(hdr + 16, checksum);
    put_le32(hdr + 20, cmd ^ 0xffffffffu);
    if (write_all(c, hdr, sizeof(hdr)) != 0) return ADB_ERR_IO;
    if (len && write_all(c, data, len) != 0) return ADB_ERR_IO;
    return ADB_OK;
}

typedef struct {
    uint32_t cmd, a0, a1, len;
} msg_hdr;

static int read_msg(adb_conn *c, msg_hdr *h, uint8_t *payload, size_t payload_cap, int timeout_ms) {
    uint8_t hdr[24];
    int rc = read_all(c, hdr, sizeof(hdr), timeout_ms);
    if (rc == -2) return ADB_ERR_TIMEOUT;
    if (rc != 0) return ADB_ERR_IO;
    h->cmd = get_le32(hdr + 0);
    h->a0 = get_le32(hdr + 4);
    h->a1 = get_le32(hdr + 8);
    h->len = get_le32(hdr + 12);
    uint32_t check = get_le32(hdr + 16);
    uint32_t magic = get_le32(hdr + 20);
    if (magic != (h->cmd ^ 0xffffffffu)) {
        if (getenv("ANYTV_DEBUG")) {
            fprintf(stderr, "[read] magic ruim cmd=%08x magic=%08x len=%u\n",
                    h->cmd, magic, h->len);
        }
        return ADB_ERR_PROTOCOL;
    }
    if (h->len > payload_cap) {
        if (getenv("ANYTV_DEBUG")) {
            fprintf(stderr, "[read] payload grande demais len=%u cap=%zu\n", h->len, payload_cap);
        }
        return ADB_ERR_PROTOCOL;
    }
    if (h->len) {
        rc = read_all(c, payload, h->len, timeout_ms);
        if (rc == -2) return ADB_ERR_TIMEOUT;
        if (rc != 0) return ADB_ERR_IO;
        /* adbd has been seen sending data_check=0 for some packets; only enforce when set */
        if (check != 0) {
            uint32_t sum = 0;
            for (uint32_t i = 0; i < h->len; i++) sum += payload[i];
            if (sum != check) return ADB_ERR_PROTOCOL;
        }
    }
    return ADB_OK;
}

#ifdef ANYTV_HOST_LINUX
static int tcp_connect(const char *host, int port, int timeout_ms) {
    struct addrinfo hints, *res = NULL, *rp;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    char portstr[16];
    snprintf(portstr, sizeof(portstr), "%d", port);
    if (getaddrinfo(host, portstr, &hints, &res) != 0) return -1;
    int fd = -1;
    for (rp = res; rp; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;
        struct timeval tv = { timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd >= 0) {
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    }
    return fd;
}
#else
static int tcp_connect(const char *host, int port, int timeout_ms) {
    extern int anytv_tcp_connect(const char *host, int port, int timeout_ms);
    return anytv_tcp_connect(host, port, timeout_ms);
}
#endif

int adb_probe(const char *host, int port, int timeout_ms) {
    int fd = tcp_connect(host, port, timeout_ms);
    if (fd < 0) return 0;
#ifdef ANYTV_HOST_LINUX
    close(fd);
#else
    extern void anytv_tcp_close(int fd);
    anytv_tcp_close(fd);
#endif
    return 1;
}

static void parse_banner(adb_conn *c, const uint8_t *payload, uint32_t len) {
    size_t n = len < sizeof(c->device_banner) - 1 ? len : sizeof(c->device_banner) - 1;
    memcpy(c->device_banner, payload, n);
    c->device_banner[n] = 0;
    const char *props = strstr(c->device_banner, "::");
    if (!props) return;
    props += 2;
    const char *keys[2] = { "ro.product.model=", "ro.product.name=" };
    for (int k = 0; k < 2; k++) {
        const char *found = strstr(props, keys[k]);
        if (!found) continue;
        found += strlen(keys[k]);
        size_t i = 0;
        while (found[i] && found[i] != ';' && i < sizeof(c->device_model) - 1) {
            c->device_model[i] = found[i];
            i++;
        }
        c->device_model[i] = 0;
        break;
    }
    const char *ver = strstr(props, "ro.build.version.release=");
    if (ver) {
        ver += strlen("ro.build.version.release=");
        size_t i = 0;
        while (ver[i] && ver[i] != ';' && i < sizeof(c->device_version) - 1) {
            c->device_version[i] = ver[i];
            i++;
        }
        c->device_version[i] = 0;
    }
}

int adb_connect(adb_conn *c, const char *host, int port, rsa_key *key, int timeout_ms) {
    return adb_connect_ex(c, host, port, key, timeout_ms, 20000);
}

int adb_connect_ex(adb_conn *c, const char *host, int port, rsa_key *key, int timeout_ms,
                   int auth_wait_ms) {
    int verbose = c->verbose;
    int sig_order = c->sig_order;
    memset(c, 0, sizeof(*c));
    c->verbose = verbose;
    c->sig_order = sig_order;
    c->fd = -1;
    c->key = key;
    c->max_payload = 256 * 1024;
    c->next_local_id = 1;

    c->fd = tcp_connect(host, port, timeout_ms);
    if (c->fd < 0) { set_err(c, ADB_ERR_SOCKET, "connect failed"); return ADB_ERR_SOCKET; }

    char banner[256];
    int blen = snprintf(banner, sizeof(banner),
                        "host::features=shell_v2,cmd,stat_v2,ls_v2,fixed_push_mkdir,apex,abb,abb_exec,remount_shell");
    if (send_msg(c, A_CNXN, ADB_VERSION, ADB_MAX_PAYLOAD, banner, (uint32_t)blen) != ADB_OK) {
        set_err(c, ADB_ERR_IO, "send CNXN failed");
        adb_close(c);
        return ADB_ERR_IO;
    }

    uint8_t *payload = (uint8_t *)malloc(ADB_MAX_PAYLOAD + 16);
    if (!payload) { adb_close(c); return ADB_ERR_IO; }

    int rc = ADB_ERR_PROTOCOL;
    int tokens_seen = 0;
    for (int rounds = 0; rounds < 8; rounds++) {
        msg_hdr h;
        int wait_ms = (tokens_seen >= 2) ? auth_wait_ms : timeout_ms;
        int r = read_msg(c, &h, payload, ADB_MAX_PAYLOAD, wait_ms);
        if (r != ADB_OK) { rc = r; set_err(c, r, "handshake read failed"); break; }
        if (c->verbose) {
            printf("[adb] recv cmd=0x%08x a0=%u a1=%u len=%u\n", h.cmd, h.a0, h.a1, h.len);
        }

        if (h.cmd == A_CNXN) {
            parse_banner(c, payload, h.len);
            c->max_payload = h.a1 ? h.a1 : ADB_MAX_PAYLOAD;
            if (c->max_payload > ADB_MAX_PAYLOAD) c->max_payload = ADB_MAX_PAYLOAD;
            rc = ADB_OK;
            break;
        }
        if (h.cmd == A_AUTH && h.a0 == ADB_AUTH_TOKEN) {
            /* A_ADB_AUTH_TOKEN_ADB_V1: first answer with a signature, then offer the key */
            tokens_seen++;
            if (c->verbose) {
                printf("[adb] AUTH token #%d (%u bytes)\n", tokens_seen, h.len);
            }
            if (!c->key) { rc = ADB_ERR_AUTH_NEEDED; set_err(c, rc, "device requires a key"); break; }
            if (tokens_seen <= 2) {
                /* adbd may rotate the token before verifying, so answer the first two
                 * tokens with a signature and only then offer the public key */
                uint8_t sig[RSA_BYTES];
                if (rsa_sign_sha1(c->key, payload, h.len, sig) != 0) {
                    rc = ADB_ERR_PROTOCOL; set_err(c, rc, "signing failed"); break;
                }
                if (c->sig_order == 1) {
                    for (int i = 0; i < RSA_BYTES / 2; i++) {
                        uint8_t t = sig[i];
                        sig[i] = sig[RSA_BYTES - 1 - i];
                        sig[RSA_BYTES - 1 - i] = t;
                    }
                } else if (c->sig_order == 2) {
                    for (int w = 0; w < RSA_BYTES / 4; w++) {
                        for (int b = 0; b < 2; b++) {
                            uint8_t t = sig[w * 4 + b];
                            sig[w * 4 + b] = sig[w * 4 + 3 - b];
                            sig[w * 4 + 3 - b] = t;
                        }
                    }
                }
                if (c->verbose) {
                    printf("[adb] sending SIGNATURE order=%d first=%02x%02x%02x%02x\n",
                           c->sig_order, sig[0], sig[1], sig[2], sig[3]);
                }
                if (getenv("ANYTV_DUMP")) {
                    printf("TOKEN ");
                    for (uint32_t k = 0; k < h.len; k++) printf("%02x", payload[k]);
                    printf("\nSIG ");
                    for (int k = 0; k < RSA_BYTES; k++) printf("%02x", sig[k]);
                    printf("\nPUB ");
                    char b64dump[RSA_PUB_BLOB_MAX * 2];
                    rsa_public_adb_b64(c->key, b64dump, sizeof(b64dump));
                    printf("%s\n", b64dump);
                }
                if (send_msg(c, A_AUTH, ADB_AUTH_SIGNATURE, 0, sig, RSA_BYTES) != ADB_OK) {
                    rc = ADB_ERR_IO; set_err(c, rc, "send AUTH signature failed"); break;
                }
            } else {
                if (tokens_seen > 3) {
                    rc = ADB_ERR_AUTH_REJECTED;
                    set_err(c, rc, "device rejected every key form");
                    break;
                }
                /* third token: very old adbd only understands the legacy key form */
                char b64[RSA_PUB_BLOB_MAX * 2];
                size_t bl = (tokens_seen == 2) ? rsa_public_adb_b64(c->key, b64, sizeof(b64))
                                               : rsa_public_adb_b64_legacy(c->key, b64, sizeof(b64));
                if (bl == 0) { rc = ADB_ERR_PROTOCOL; set_err(c, rc, "pubkey encode failed"); break; }
                b64[bl] = 0;
                if (c->verbose) {
                    printf("[adb] sending RSAPUBLICKEY #%d len=%zu\n", tokens_seen - 1, bl);
                }
                if (send_msg(c, A_AUTH, ADB_AUTH_RSAPUBLICKEY, 0, b64, (uint32_t)bl + 1) != ADB_OK) {
                    rc = ADB_ERR_IO; set_err(c, rc, "send AUTH pubkey failed"); break;
                }
            }
            continue;
        }
        if (h.cmd == A_STLS) {
            rc = ADB_ERR_AUTH_REJECTED;
            set_err(c, rc, "device wants TLS (adb pair), not classic ADB");
            break;
        }
        if (h.cmd == A_OKAY || h.cmd == A_WRTE) {
            /* auth-less devices may already be talking services; keep waiting */
            continue;
        }
        rc = ADB_ERR_PROTOCOL;
        set_err(c, rc, "unexpected command during handshake");
        break;
    }

    free(payload);
    if (rc != ADB_OK) { adb_close(c); return rc; }
    return ADB_OK;
}

void adb_close(adb_conn *c) {
    if (!c) return;
    if (c->raw_left) {
        free(c->raw_left);
        c->raw_left = NULL;
        c->raw_left_len = 0;
        c->raw_left_off = 0;
    }
    c->stream_active = 0;
    if (c->fd >= 0) {
#ifdef ANYTV_HOST_LINUX
        close(c->fd);
#else
        extern void anytv_tcp_close(int fd);
        anytv_tcp_close(c->fd);
#endif
        c->fd = -1;
    }
}

/* Open a service and return the local id / remote id pair.
 * The service name travels NUL-terminated (size + 1), exactly like AOSP's client. */
static int open_service(adb_conn *c, const char *service, uint32_t *local_id, uint32_t *remote_id,
                        int timeout_ms) {
    uint32_t lid = c->next_local_id++;
    size_t slen = strlen(service);
    if (slen + 1 > 4096) return ADB_ERR_PROTOCOL;
    char name[4096];
    memcpy(name, service, slen);
    name[slen] = 0;
    if (send_msg(c, A_OPEN, lid, 0, name, (uint32_t)slen + 1) != ADB_OK) {
        set_err(c, ADB_ERR_IO, "send OPEN failed");
        return ADB_ERR_IO;
    }
    uint8_t *payload = (uint8_t *)malloc(ADB_MAX_PAYLOAD + 16);
    if (!payload) return ADB_ERR_IO;
    int rc = ADB_ERR_PROTOCOL;
    for (int i = 0; i < 16; i++) {
        msg_hdr h;
        int r = read_msg(c, &h, payload, ADB_MAX_PAYLOAD, timeout_ms);
        if (r != ADB_OK) { rc = r; break; }
        /* frames from previous streams (a stale CLSE, for instance) must not
         * abort the new service: only ids that belong to it count */
        if (h.cmd == A_OKAY && h.a1 == lid) { *local_id = lid; *remote_id = h.a0; rc = ADB_OK; break; }
        if (h.cmd == A_CLSE && h.a1 == lid) { rc = ADB_ERR_PROTOCOL; set_err(c, rc, "service refused"); break; }
        if (h.cmd == A_WRTE && h.a1 == lid) { send_msg(c, A_OKAY, lid, h.a0, NULL, 0); continue; }
        continue;
    }
    free(payload);
    return rc;
}

/* Read from an open stream until CLSE, appending to out.
 * v2 == 1 means the service is shell_v2: every WRTE payload starts with an
 * id byte (0 stdin, 1 stdout, 2 stderr, 3 exit code). */
static int drain_stream(adb_conn *c, uint32_t lid, uint32_t rid, char *out, size_t outlen,
                        size_t *written, int *exit_code, int timeout_ms, int v2) {
    uint8_t *payload = (uint8_t *)malloc(ADB_MAX_PAYLOAD + 16);
    if (!payload) return ADB_ERR_IO;
    int rc = ADB_OK;
    for (;;) {
        msg_hdr h;
        int r = read_msg(c, &h, payload, ADB_MAX_PAYLOAD, timeout_ms);
        if (r != ADB_OK) { rc = r; break; }
        if (h.cmd == A_WRTE && h.a1 == lid) {
            const uint8_t *data = payload;
            uint32_t dlen = h.len;
            if (v2) {
                /* shell_v2 packet: id(1) + payload length (u32 LE) + bytes */
                uint32_t off = 0;
                dlen = 0;
                data = NULL;
                static uint8_t merged[ADB_MAX_PAYLOAD + 16];
                uint32_t mo = 0;
                while (off + 5 <= h.len) {
                    uint8_t id = payload[off];
                    uint32_t plen = get_le32(payload + off + 1);
                    off += 5;
                    if (plen > h.len - off) plen = h.len - off;
                    if (id == 3) {
                        if (plen >= 1 && exit_code) *exit_code = payload[off];
                    } else if (id == 1 || id == 2) {
                        if (mo + plen <= sizeof(merged)) {
                            memcpy(merged + mo, payload + off, plen);
                            mo += plen;
                        }
                    }
                    off += plen;
                }
                data = merged;
                dlen = mo;
            }
            if (dlen && out && *written + 1 < outlen) {
                size_t space = outlen - 1 - *written;
                size_t take = dlen < space ? dlen : space;
                memcpy(out + *written, data, take);
                *written += take;
            }
            if (send_msg(c, A_OKAY, lid, rid, NULL, 0) != ADB_OK) { rc = ADB_ERR_IO; break; }
        } else if (h.cmd == A_CLSE && h.a1 == lid) {
            break;
        } else if (h.cmd == A_OKAY) {
            continue;
        }
    }
    free(payload);
    return rc;
}

/* Legacy (non shell_v2) framing carries raw bytes. */
/* Esvazia o que o aparelho mandou sem bloquear; devolve OKAY dos WRTE. */
static void key_drain(adb_conn *c, int budget_ms) {
    uint8_t *payload = (uint8_t *)malloc(ADB_MAX_PAYLOAD + 16);
    if (!payload) return;
    int wait = budget_ms;
    for (;;) {
        msg_hdr h;
        int r = read_msg(c, &h, payload, ADB_MAX_PAYLOAD, wait);
        if (r != ADB_OK) break;
        if (h.cmd == A_WRTE && h.a1 == c->key_lid) {
            send_msg(c, A_OKAY, c->key_lid, c->key_rid, NULL, 0);
        } else if (h.cmd == A_CLSE && h.a1 == c->key_lid) {
            c->key_open = 0;
            break;
        }
        wait = 5;   /* depois do primeiro, só o que já está no buffer */
    }
    free(payload);
}

/* Teclas por um canal de shell aberto uma vez e reaproveitado. Cada "input
 * keyevent" custa um processo no aparelho; abrir e fechar servico a cada toque
 * somava mais uma ida e volta em cima disso. Sem esperar resposta, o toque
 * aparece na hora e o aparelho processa na ordem. */
int adb_key_line(adb_conn *c, const char *line, int timeout_ms) {
    if (!c || c->fd < 0) return ADB_ERR_IO;
    if (!c->key_open) {
        int rc = open_service(c, "shell,v2,raw:", &c->key_lid, &c->key_rid, timeout_ms);
        if (rc != ADB_OK) return adb_shell(c, line, NULL, 0, timeout_ms);
        c->key_open = 1;
    }
    key_drain(c, 4);
    char buf[512];
    int n = snprintf(buf, sizeof(buf), "%s\n", line);
    if (n <= 0 || send_msg(c, A_WRTE, c->key_lid, c->key_rid, (uint8_t *)buf,
                           (uint32_t)n) != ADB_OK) {
        c->key_open = 0;
        return adb_shell(c, line, NULL, 0, timeout_ms);
    }
    return ADB_OK;
}

int adb_shell(adb_conn *c, const char *command, char *out, size_t outlen, int timeout_ms) {
    /* socket morto nao pode virar comando: era o caminho do fechamento do app
     * quando o adbd do aparelho travava no meio do uso */
    if (!c || c->fd < 0) {
        if (c) set_err(c, ADB_ERR_IO, "conexao ADB caiu");
        if (out && outlen) out[0] = 0;
        return ADB_ERR_IO;
    }
    char service[1024];
    snprintf(service, sizeof(service), "shell,v2,raw:%s", command);
    int exit_code = 0;
    size_t written = 0;
    uint32_t lid = 0, rid = 0;
    if (out && outlen) out[0] = 0;
    int rc = open_service(c, service, &lid, &rid, timeout_ms);
    if (rc != ADB_OK) {
        /* older adbd: plain shell service, no exit code */
        snprintf(service, sizeof(service), "shell:%s", command);
        rc = open_service(c, service, &lid, &rid, timeout_ms);
        if (rc != ADB_OK) return rc;
        rc = drain_stream(c, lid, rid, out, outlen, &written, &exit_code, timeout_ms, 0);
        if (out && outlen) out[written] = 0;
        return rc == ADB_OK ? 0 : rc;
    }
    rc = drain_stream(c, lid, rid, out, outlen, &written, &exit_code, timeout_ms, 1);
    if (out && outlen) out[written] = 0;
    if (rc != ADB_OK) return rc;
    return exit_code;
}

int adb_stream_start(adb_conn *c, const char *command, int timeout_ms) {
    char service[1024];
    uint32_t lid = 0, rid = 0;
    c->stream_active = 0;
    c->sv2_hdr_len = 0;
    c->sv2_remaining = 0;
    c->sv2_discard = 0;
    c->stream_v2 = 1;
    snprintf(service, sizeof(service), "shell,v2,raw:%s", command);
    int rc = open_service(c, service, &lid, &rid, timeout_ms);
    if (rc != ADB_OK) {
        c->stream_v2 = 0;
        snprintf(service, sizeof(service), "shell:%s", command);
        rc = open_service(c, service, &lid, &rid, timeout_ms);
        if (rc != ADB_OK) return rc;
    }
    c->stream_lid = lid;
    c->stream_rid = rid;
    c->stream_active = 1;
    return ADB_OK;
}

int adb_stream_start_service(adb_conn *c, const char *service, int timeout_ms) {
    uint32_t lid = 0, rid = 0;
    c->stream_active = 0;
    c->sv2_hdr_len = 0;
    c->sv2_remaining = 0;
    c->sv2_discard = 0;
    c->stream_v2 = 0; /* raw service: payload goes straight to the caller */
    int rc = open_service(c, service, &lid, &rid, timeout_ms);
    if (rc != ADB_OK) return rc;
    c->stream_lid = lid;
    c->stream_rid = rid;
    c->stream_active = 1;
    return ADB_OK;
}

int adb_stream_write(adb_conn *c, const uint8_t *data, size_t len) {
    if (!c || c->fd < 0 || !c->stream_active) return ADB_ERR_IO;
    if (send_msg(c, A_WRTE, c->stream_lid, c->stream_rid, (uint8_t *)data, (uint32_t)len) != ADB_OK) {
        set_err(c, ADB_ERR_IO, "write no canal falhou");
        return ADB_ERR_IO;
    }
    /* Consome o OKAY da janela do transporte; sem isso, escritas grandes param
     * depois do primeiro buffer. Saida que chegar nesse meio-tempo e descartada. */
    static uint8_t sink[ADB_MAX_PAYLOAD + 16];
    for (int i = 0; i < 64; i++) {
        msg_hdr h;
        int r = read_msg(c, &h, sink, ADB_MAX_PAYLOAD, 200);
        if (r != ADB_OK) break;
        if (h.cmd == A_OKAY) return ADB_OK;
        if (h.cmd == A_WRTE && h.a1 == c->stream_lid) {
            send_msg(c, A_OKAY, c->stream_lid, c->stream_rid, NULL, 0);
        } else if (h.cmd == A_CLSE && h.a1 == c->stream_lid) {
            c->stream_active = 0;
            return ADB_ERR_IO;
        }
    }
    return ADB_OK;
}

int adb_stream_read(adb_conn *c, uint8_t *out, size_t outlen, int timeout_ms) {
    static uint8_t payload[ADB_MAX_PAYLOAD + 16];
    if (!c->stream_active || !out || !outlen) return 0;
    for (;;) {
        msg_hdr h;
        int r = read_msg(c, &h, payload, ADB_MAX_PAYLOAD, timeout_ms);
        if (r != ADB_OK) {
            /* A quiet moment (waiting for the next key frame) is not the end:
             * keep the stream alive and let the caller poll again. */
            if (r == ADB_ERR_TIMEOUT) return ADB_ERR_TIMEOUT;
            c->stream_active = 0;
            return r;
        }
        if (h.cmd == A_WRTE && h.a1 == c->stream_lid) {
            if (!c->stream_v2) {
                if (c->raw_left && c->raw_left_off < c->raw_left_len) {
                    uint32_t avail = c->raw_left_len - c->raw_left_off;
                    uint32_t n = avail < outlen ? avail : (uint32_t)outlen;
                    memcpy(out, c->raw_left + c->raw_left_off, n);
                    c->raw_left_off += n;
                    return (int)n;
                }
                uint32_t n = h.len < outlen ? h.len : (uint32_t)outlen;
                memcpy(out, payload, n);
                if (h.len > n) {
                    /* never drop bytes: a raw service (scrcpy) packs metadata and
                     * the first frame into the same write */
                    if (!c->raw_left) c->raw_left = (uint8_t *)malloc(ADB_MAX_PAYLOAD);
                    if (c->raw_left) {
                        memcpy(c->raw_left, payload + n, h.len - n);
                        c->raw_left_len = h.len - n;
                        c->raw_left_off = 0;
                    }
                } else {
                    c->raw_left_len = 0;
                    c->raw_left_off = 0;
                }
                send_msg(c, A_OKAY, c->stream_lid, c->stream_rid, NULL, 0);
                return (int)n;
            }
            size_t produced = 0;
            uint32_t off = 0;
            while (off < h.len) {
                if (c->sv2_remaining > 0) {
                    uint32_t take = h.len - off;
                    if (take > c->sv2_remaining) take = c->sv2_remaining;
                    if (!c->sv2_discard) {
                        size_t space = outlen - produced;
                        if (space > 0) {
                            size_t copy = take < space ? take : space;
                            memcpy(out + produced, payload + off, copy);
                            produced += copy;
                        }
                    }
                    off += take;
                    c->sv2_remaining -= take;
                } else if (c->sv2_hdr_len < sizeof(c->sv2_hdr)) {
                    c->sv2_hdr[c->sv2_hdr_len++] = payload[off++];
                    if (c->sv2_hdr_len == sizeof(c->sv2_hdr)) {
                        uint8_t id = c->sv2_hdr[0];
                        uint32_t plen = get_le32(c->sv2_hdr + 1);
                        c->sv2_hdr_len = 0;
                        if (id == 3) { /* exit packet: the producer finished */
                            send_msg(c, A_OKAY, c->stream_lid, c->stream_rid, NULL, 0);
                            c->stream_active = 0;
                            return (int)produced;
                        }
                        c->sv2_remaining = plen;
                        c->sv2_discard = (id != 1); /* stdout only; stderr is dropped */
                    }
                }
            }
            send_msg(c, A_OKAY, c->stream_lid, c->stream_rid, NULL, 0);
            if (produced > 0) return (int)produced;
            continue;
        }
        if (h.cmd == A_CLSE && h.a1 == c->stream_lid) {
            c->stream_active = 0;
            return 0;
        }
        if (h.cmd == A_OKAY) continue;
    }
}

void adb_stream_stop(adb_conn *c) {
    if (!c) return;
    if (c->stream_active) {
        send_msg(c, A_CLSE, c->stream_lid, c->stream_rid, NULL, 0);
        c->stream_active = 0;
    }
    /* Closing the pipe ends screenrecord, but a stuck child keeps the encoder
     * busy, so kill it explicitly as well. */
    char sink[128];
    adb_shell(c, "pkill -f screenrecord 2>/dev/null", sink, sizeof(sink), 3000);
}

static int sync_send(adb_conn *c, uint32_t lid, uint32_t rid, const void *data, uint32_t len) {
    return send_msg(c, A_WRTE, lid, rid, data, len);
}

int adb_service_capture(adb_conn *c, const char *service, char *out, size_t outlen,
                        int timeout_ms) {
    uint32_t lid = 0, rid = 0;
    size_t written = 0;
    int exit_code = 0;
    if (out && outlen) out[0] = 0;
    int rc = open_service(c, service, &lid, &rid, timeout_ms);
    if (rc != ADB_OK) return rc;
    rc = drain_stream(c, lid, rid, out, outlen, &written, &exit_code, timeout_ms, 0);
    if (out && outlen) out[written < outlen ? written : outlen - 1] = 0;
    return rc == ADB_OK ? 0 : rc;
}

/* Reads one sync response record and returns its tag as an ADB_* code. */
static int sync_read_record(adb_conn *c, uint32_t lid, uint32_t rid,
                            char *msg, size_t msglen, int timeout_ms) {
    static uint8_t payload[ADB_MAX_PAYLOAD + 16];
    for (;;) {
        msg_hdr h;
        int r = read_msg(c, &h, payload, ADB_MAX_PAYLOAD, timeout_ms);
        if (r != ADB_OK) return r;
        if (h.cmd == A_CLSE && h.a1 == lid) return ADB_ERR_PROTOCOL;
        if (h.cmd != A_WRTE || h.a1 != lid) continue;
        uint32_t tag = h.len >= 4 ? get_le32(payload) : 0;
        if (msg && msglen) {
            if (h.len > 8) {
                uint32_t n = h.len - 8;
                if (n > msglen - 1) n = msglen - 1;
                memcpy(msg, payload + 8, n);
                msg[n] = 0;
            } else {
                msg[0] = 0;
            }
        }
        send_msg(c, A_OKAY, lid, rid, NULL, 0);
        if (tag == A_OKAY) return ADB_OK;
        if (tag == 0x4c494146u) return ADB_ERR_IO;       /* FAIL */
        return ADB_ERR_PROTOCOL;
    }
}

int adb_push(adb_conn *c, const char *remote_path, const uint8_t *data, size_t len,
             int timeout_ms) {
    uint32_t lid = 0, rid = 0;
    int rc = open_service(c, "sync:", &lid, &rid, timeout_ms);
    if (rc != ADB_OK) return rc;

    uint8_t req[8];
    put_le32(req, 0x444e4553u); /* "SEND" */
    put_le32(req + 4, (uint32_t)strlen(remote_path));
    if (sync_send(c, lid, rid, req, 8) != ADB_OK) return ADB_ERR_IO;
    if (sync_send(c, lid, rid, remote_path, (uint32_t)strlen(remote_path)) != ADB_OK) return ADB_ERR_IO;

    static uint8_t chunk[64 * 1024 + 8];
    size_t off = 0;
    char msg[160];
    while (off < len) {
        size_t n = len - off;
        if (n > 64 * 1024) n = 64 * 1024;
        put_le32(chunk, 0x41544144u); /* DATA */
        put_le32(chunk + 4, (uint32_t)n);
        memcpy(chunk + 8, data + off, n);
        if (sync_send(c, lid, rid, chunk, (uint32_t)n + 8) != ADB_OK) return ADB_ERR_IO;
        off += n;
        if (sync_read_record(c, lid, rid, msg, sizeof(msg), timeout_ms) != ADB_OK) {
            set_err(c, ADB_ERR_IO, msg[0] ? msg : "push recusado");
            return ADB_ERR_IO;
        }
    }
    put_le32(chunk, 0x454e4f44u); /* DONE */
    put_le32(chunk + 4, 0);
    if (sync_send(c, lid, rid, chunk, 8) != ADB_OK) return ADB_ERR_IO;
    int final_rc = sync_read_record(c, lid, rid, msg, sizeof(msg), timeout_ms);
    send_msg(c, A_CLSE, lid, rid, NULL, 0);
    if (final_rc != ADB_OK) {
        set_err(c, ADB_ERR_IO, msg[0] ? msg : "push falhou");
        return ADB_ERR_IO;
    }
    return ADB_OK;
}

int adb_pull(adb_conn *c, const char *remote_path, uint8_t *out, size_t outlen, int timeout_ms) {
    uint32_t lid = 0, rid = 0;
    int rc = open_service(c, "sync:", &lid, &rid, timeout_ms);
    if (rc != ADB_OK) {
        if (getenv("ANYTV_DEBUG")) {
            fprintf(stderr, "[pull] open sync falhou rc=%d err=%s\n", rc, c->last_error_text);
        }
        return rc;
    }

    uint8_t req[8];
    put_le32(req, 0x56434552u); /* "RECV" as a little-endian uint32 */
    put_le32(req + 4, (uint32_t)strlen(remote_path));
    if (sync_send(c, lid, rid, req, 8) != ADB_OK) return ADB_ERR_IO;
    if (sync_send(c, lid, rid, remote_path, (uint32_t)strlen(remote_path)) != ADB_OK) return ADB_ERR_IO;

    uint8_t *payload = (uint8_t *)malloc(ADB_MAX_PAYLOAD + 16);
    if (!payload) return ADB_ERR_IO;
    size_t total = 0;
    rc = ADB_OK;
    int done = 0;

    /* The sync service is a byte stream: adbd splits one DATA record across
     * several WRTE frames, so records must be reassembled here instead of being
     * parsed per packet (this is what broke pulls on a real Android 7 box). */
    uint8_t hdr[8];
    size_t hdr_len = 0;
    uint32_t remaining = 0;     /* bytes left in the current DATA record */
    int in_data = 0;

    while (!done) {
        msg_hdr h;
        int r = read_msg(c, &h, payload, ADB_MAX_PAYLOAD, timeout_ms);
        if (r != ADB_OK) { rc = r; break; }
        if (h.cmd == A_WRTE && h.a1 == lid) {
            uint32_t off = 0;
            while (off < h.len && !done) {
                if (!in_data) {
                    size_t need = sizeof(hdr) - hdr_len;
                    size_t take = (h.len - off) < need ? (h.len - off) : need;
                    memcpy(hdr + hdr_len, payload + off, take);
                    hdr_len += take;
                    off += (uint32_t)take;
                    if (hdr_len < sizeof(hdr)) break;
                    uint32_t tag = get_le32(hdr);
                    uint32_t sz = get_le32(hdr + 4);
                    hdr_len = 0;
                    if (tag == 0x41544144u) {          /* DATA */
                        remaining = sz;
                        in_data = 1;
                        if (remaining == 0) in_data = 0;
                    } else if (tag == 0x454e4f44u) {    /* DONE */
                        done = 1;
                        rc = (int)total;
                    } else if (tag == 0x4c494146u) {    /* FAIL */
                        if (getenv("ANYTV_DEBUG")) {
                            char msg[128] = "";
                            uint32_t mlen = sz < (h.len - off) ? sz : (h.len - off);
                            if (mlen >= sizeof(msg)) mlen = sizeof(msg) - 1;
                            memcpy(msg, payload + off, mlen);
                            msg[mlen] = 0;
                            fprintf(stderr, "[pull] FAIL msg=\"%s\"\n", msg);
                        }
                        rc = ADB_ERR_IO;
                        set_err(c, rc, "pull failed (FAIL response)");
                        done = 1;
                    } else {
                        if (getenv("ANYTV_DEBUG")) {
                            fprintf(stderr, "[pull] tag desconhecido %08x (%02x%02x%02x%02x)\n",
                                    tag, hdr[0], hdr[1], hdr[2], hdr[3]);
                        }
                        rc = ADB_ERR_PROTOCOL;
                        done = 1;
                    }
                } else {
                    uint32_t avail = h.len - off;
                    uint32_t take = avail < remaining ? avail : remaining;
                    if (total < outlen) {
                        size_t space = outlen - total;
                        size_t copy = take < space ? take : space;
                        memcpy(out + total, payload + off, copy);
                        total += copy;
                    }
                    off += take;
                    remaining -= take;
                    if (remaining == 0) in_data = 0;
                }
            }
            if (send_msg(c, A_OKAY, lid, rid, NULL, 0) != ADB_OK) { rc = ADB_ERR_IO; break; }
        } else if (h.cmd == A_CLSE && h.a1 == lid) {
            rc = (int)total;
            done = 1;
        } else if (h.cmd == A_OKAY) {
            continue;
        }
    }
    free(payload);
    send_msg(c, A_CLSE, lid, rid, NULL, 0);
    return rc;
}

int adb_rm(adb_conn *c, const char *remote_path, int timeout_ms) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -f '%s'", remote_path);
    return adb_shell(c, cmd, NULL, 0, timeout_ms);
}

/* Debug helper used by the CLI: open a service and dump what comes back. */
int anytv_debug_service(adb_conn *c, const char *service) {
    uint32_t lid = 0, rid = 0;
    int rc = open_service(c, service, &lid, &rid, 5000);
    printf("open rc=%d lid=%u rid=%u err=%s\n", rc, lid, rid, c->last_error_text);
    if (rc != ADB_OK) return rc;
    uint8_t *payload = (uint8_t *)malloc(ADB_MAX_PAYLOAD + 16);
    if (!payload) return ADB_ERR_IO;
    for (int i = 0; i < 12; i++) {
        msg_hdr h;
        int r = read_msg(c, &h, payload, ADB_MAX_PAYLOAD, 3000);
        if (r != ADB_OK) {
            printf("frame read rc=%d (%s) err=%s\n", r, r == ADB_ERR_TIMEOUT ? "timeout" : "err",
                   c->last_error_text);
            break;
        }
        printf("frame cmd=0x%08x a0=%u a1=%u len=%u hex=", h.cmd, h.a0, h.a1, h.len);
        for (uint32_t k = 0; k < h.len && k < 12; k++) printf("%02x", payload[k]);
        printf(" text=\"");
        for (uint32_t k = 0; k < h.len && k < 40; k++) {
            unsigned char ch = payload[k];
            putchar(ch >= 32 && ch < 127 ? ch : '.');
        }
        printf("\"\n");
        if (h.cmd == A_WRTE) send_msg(c, A_OKAY, h.a1, h.a0, NULL, 0);
        if (h.cmd == A_CLSE) break;
    }
    free(payload);
    send_msg(c, A_CLSE, lid, rid, NULL, 0);
    return 0;
}
