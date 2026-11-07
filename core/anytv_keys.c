/* anytv_keys.c - keycode table, shell escaping and dumpsys parsing. */
#include "anytv_keys.h"
#include <string.h>
#include <stdio.h>

static const anytv_key KEYS[] = {
    {"HOME", 3}, {"BACK", 4}, {"CALL", 5}, {"ENDCALL", 6},
    {"DPAD_UP", 19}, {"DPAD_DOWN", 20}, {"DPAD_LEFT", 21}, {"DPAD_RIGHT", 22},
    {"DPAD_CENTER", 23}, {"VOLUME_UP", 24}, {"VOLUME_DOWN", 25}, {"POWER", 26},
    {"CAMERA", 27}, {"CLEAR", 28}, {"A", 29}, {"TAB", 61}, {"SPACE", 62},
    {"ENTER", 66}, {"DEL", 67}, {"GRAVE", 68}, {"MINUS", 69}, {"EQUALS", 70},
    {"MENU", 82}, {"NOTIFICATION", 83}, {"SEARCH", 84},
    {"MEDIA_PLAY_PAUSE", 85}, {"MEDIA_STOP", 86}, {"MEDIA_NEXT", 87},
    {"MEDIA_PREVIOUS", 88}, {"MEDIA_REWIND", 89}, {"MEDIA_FAST_FORWARD", 90},
    {"MUTE", 91}, {"PAGE_UP", 92}, {"PAGE_DOWN", 93},
    {"MEDIA_PLAY", 126}, {"MEDIA_PAUSE", 127}, {"MEDIA_RECORD", 130},
    {"FORWARD", 125}, {"MEDIA_CLOSE", 128}, {"MEDIA_EJECT", 129},
    {"MUTE_MIC", 131},
    {"CHANNEL_UP", 166}, {"CHANNEL_DOWN", 167}, {"INFO", 165},
    {"SETTINGS", 176}, {"TV_INPUT", 178}, {"TV", 170}, {"GUIDE", 172},
    {"APP_SWITCH", 187}, {"ASSIST", 219}, {"VOICE_ASSIST", 231},
    {"SLEEP", 223}, {"WAKEUP", 224}, {"SOFT_SLEEP", 276},
    {"MUTE_ALL", 92}, {"BRIGHTNESS_UP", 221}, {"BRIGHTNESS_DOWN", 220},
    {"BUTTON_1", 188}, {"BUTTON_2", 189}, {"BUTTON_3", 190}, {"BUTTON_4", 191},
    {"0", 7}, {"1", 8}, {"2", 9}, {"3", 10}, {"4", 11},
    {"5", 12}, {"6", 13}, {"7", 14}, {"8", 15}, {"9", 16},
};

const anytv_key *anytv_key_table(size_t *count) {
    if (count) *count = sizeof(KEYS) / sizeof(KEYS[0]);
    return KEYS;
}

int anytv_key_code(const char *name) {
    if (!name) return -1;
    for (size_t i = 0; i < sizeof(KEYS) / sizeof(KEYS[0]); i++) {
        if (strcmp(KEYS[i].name, name) == 0) return KEYS[i].code;
    }
    /* allow raw numeric codes */
    int v = 0;
    if (name[0] >= '0' && name[0] <= '9') {
        for (const char *p = name; *p; p++) {
            if (*p < '0' || *p > '9') return -1;
            v = v * 10 + (*p - '0');
            if (v > 1000) return -1;
        }
        return v;
    }
    return -1;
}

const char *anytv_key_name(int code) {
    for (size_t i = 0; i < sizeof(KEYS) / sizeof(KEYS[0]); i++) {
        if (KEYS[i].code == code) return KEYS[i].name;
    }
    return "UNKNOWN";
}

int anytv_keyevent_command(int code, int repeat, char *out, size_t outlen) {
    if (repeat < 1) repeat = 1;
    if (repeat == 1) return snprintf(out, outlen, "input keyevent %d", code);
    /* repeated presses: several keyevents in one shell call */
    int n = snprintf(out, outlen, "input keyevent");
    for (int i = 0; i < repeat; i++) {
        if (n < 0 || (size_t)n >= outlen) return -1;
        n += snprintf(out + n, outlen - (size_t)n, " %d", code);
    }
    return n;
}

/* `input text` treats %s as a space and needs single quotes escaped. */
int anytv_input_text_command(const char *utf8, char *out, size_t outlen) {
    if (!utf8) return -1;
    size_t o = 0;
    const char *prefix = "input text '";
    size_t pl = strlen(prefix);
    if (pl + 1 >= outlen) return -1;
    memcpy(out, prefix, pl);
    o = pl;
    for (const char *p = utf8; *p; p++) {
        if (*p == ' ') {
            if (o + 2 >= outlen) return -1;
            out[o++] = '%'; out[o++] = 's';
        } else if (*p == '\'') {
            if (o + 4 >= outlen) return -1;
            /* close quote, escaped quote, reopen */
            memcpy(out + o, "'\\''", 4);
            o += 4;
        } else if (*p == '\n' || *p == '\r') {
            continue;
        } else {
            if (o + 1 >= outlen) return -1;
            out[o++] = *p;
        }
    }
    if (o + 2 >= outlen) return -1;
    out[o++] = '\'';
    out[o] = 0;
    return (int)o;
}

int anytv_parse_launchable(const char *dumpsys, char pkgs[][128], int max) {
    if (!dumpsys || max <= 0) return 0;
    int count = 0;
    char current[128] = {0};
    int current_done = 0; /* already recorded this package */
    const char *line = dumpsys;
    while (*line) {
        const char *eol = strchr(line, '\n');
        size_t len = eol ? (size_t)(eol - line) : strlen(line);
        char scratch[256];
        size_t copy = len < sizeof(scratch) - 1 ? len : sizeof(scratch) - 1;
        memcpy(scratch, line, copy);
        scratch[copy] = 0;

        const char *marker = strstr(scratch, "Package [");
        if (marker) {
            const char *start = marker + strlen("Package [");
            const char *end = strchr(start, ']');
            if (end) {
                size_t n = (size_t)(end - start);
                if (n < sizeof(current)) {
                    memcpy(current, start, n);
                    current[n] = 0;
                    current_done = 0;
                }
            }
        } else if (current[0] && !current_done &&
                   (strstr(scratch, "android.intent.category.LEANBACK_LAUNCHER") ||
                    strstr(scratch, "android.intent.category.LAUNCHER"))) {
            if (count < max) {
                snprintf(pkgs[count], 128, "%s", current);
                count++;
            }
            current_done = 1;
        }
        if (!eol) break;
        line = eol + 1;
    }
    return count;
}

int anytv_launch_command(const char *pkg, char *out, size_t outlen) {
    return snprintf(out, outlen,
                    "monkey -p %s -c android.intent.category.LEANBACK_LAUNCHER 1 2>/dev/null || "
                    "monkey -p %s -c android.intent.category.LAUNCHER 1", pkg, pkg);
}

int anytv_screencap_command(char *out, size_t outlen) {
    return snprintf(out, outlen, "screencap -p /data/local/tmp/anytv_shot.png");
}
