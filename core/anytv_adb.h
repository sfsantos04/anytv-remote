/* anytv_adb.h - ADB host protocol client (TCP), usable from iOS and Linux. */
#ifndef ANYTV_ADB_H
#define ANYTV_ADB_H

#include <stdint.h>
#include <stddef.h>
#include "anytv_rsa.h"

#define ADB_DEFAULT_PORT 5555
#define ADB_MAX_PAYLOAD (256 * 1024)

typedef struct {
    int fd;
    uint32_t max_payload;
    uint32_t next_local_id;
    uint32_t banner_flags;
    char device_banner[512];   /* filled from the CNXN device banner */
    char device_model[128];
    char device_version[64];
    rsa_key *key;              /* borrowed, may be NULL for already-authorized devices */
    int last_error;
    char last_error_text[256];
    int verbose;               /* print every frame (debugging aid) */
    int sig_order;             /* 0 = big endian, 1 = reversed, 2 = word swapped */
    /* live stream state (screenrecord -> H.264 over shell_v2) */
    uint32_t stream_lid;
    uint32_t stream_rid;
    int stream_active;
    uint8_t sv2_hdr[5];
    size_t sv2_hdr_len;
    uint32_t sv2_remaining;
    int sv2_discard;
    int stream_v2;
    /* leftover of a raw frame that did not fit in the caller's buffer */
    uint8_t *raw_left;
    uint32_t raw_left_len;
    uint32_t raw_left_off;
} adb_conn;

#define ADB_OK 0
#define ADB_ERR_SOCKET -1
#define ADB_ERR_PROTOCOL -2
#define ADB_ERR_AUTH_REJECTED -3
#define ADB_ERR_AUTH_NEEDED -4
#define ADB_ERR_TIMEOUT -5
#define ADB_ERR_IO -6

/* Connect + handshake + auth. timeout_ms applies to each socket operation.
 * auth_wait_ms is how long to wait for the user to accept the key on the TV. */
int adb_connect(adb_conn *c, const char *host, int port, rsa_key *key, int timeout_ms);
int adb_connect_ex(adb_conn *c, const char *host, int port, rsa_key *key, int timeout_ms,
                   int auth_wait_ms);
void adb_close(adb_conn *c);

/* Run a shell command; writes up to outlen bytes (always NUL terminated when outlen>0).
 * Returns the exit code, or a negative ADB_ERR_*. */
int adb_shell(adb_conn *c, const char *command, char *out, size_t outlen, int timeout_ms);

/* Opens any service and captures everything it writes as text (sdb, for one,
 * answers on "shell:0 <command>"). Returns 0 or a negative ADB_ERR_*. */
int adb_service_capture(adb_conn *c, const char *service, char *out, size_t outlen,
                        int timeout_ms);

/* Read a whole remote file over the sync service (binary safe). Returns bytes read or <0. */
int adb_pull(adb_conn *c, const char *remote_path, uint8_t *out, size_t outlen, int timeout_ms);

/* Write a file to the device over the sync service. Returns ADB_OK or <0. */
int adb_push(adb_conn *c, const char *remote_path, const uint8_t *data, size_t len, int timeout_ms);

/* Remove a remote file. */
int adb_rm(adb_conn *c, const char *remote_path, int timeout_ms);

/* Quick liveness: does host:port speak ADB? */
int adb_probe(const char *host, int port, int timeout_ms);

/* Debug: open a raw service and dump the frames the device sends. */
int anytv_debug_service(adb_conn *c, const char *service);

/* Opens any device service as a raw byte stream (used for localabstract:scrcpy). */
int adb_stream_start_service(adb_conn *c, const char *service, int timeout_ms);

/* Live stream: runs a shell command and hands its stdout back chunk by chunk, so
 * a long-running producer (screenrecord) can be decoded while it writes.
 * start returns ADB_OK; read returns bytes copied (>0), 0 when the stream ended,
 * or a negative ADB_ERR_*. stop closes the stream (the child dies with it). */
int adb_stream_start(adb_conn *c, const char *command, int timeout_ms);
int adb_stream_read(adb_conn *c, uint8_t *out, size_t outlen, int timeout_ms);
void adb_stream_stop(adb_conn *c);

#endif
