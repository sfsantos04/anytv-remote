/* anytv_keys.h - Android key codes, text escaping and dumpsys app parsing. */
#ifndef ANYTV_KEYS_H
#define ANYTV_KEYS_H

#include <stddef.h>

typedef struct {
    const char *name;  /* canonical name, e.g. "DPAD_DOWN" */
    int code;
} anytv_key;

const anytv_key *anytv_key_table(size_t *count);
int anytv_key_code(const char *name);          /* -1 when unknown */
const char *anytv_key_name(int code);

/* Build `input text '...'` safe for the adb shell. Spaces become %s (the
 * convention accepted by Android's input command). Returns 0 on success. */
int anytv_input_text_command(const char *utf8, char *out, size_t outlen);

/* Build `input keyevent <code>` (repeated presses as taps). */
int anytv_keyevent_command(int code, int repeat, char *out, size_t outlen);

/* Parse `dumpsys package` into TV/mobile launchable packages.
 * Writes up to max entries, returns the count. */
int anytv_parse_launchable(const char *dumpsys, char pkgs[][128], int max);

/* Launch a package by its launcher category, works on all Android versions. */
int anytv_launch_command(const char *pkg, char *out, size_t outlen);

/* Screenshot: capture to a temp path then pull it (binary safe on every version). */
int anytv_screencap_command(char *out, size_t outlen);

#endif
