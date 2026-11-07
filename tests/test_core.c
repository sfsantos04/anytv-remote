/* test_core.c - unit tests for the command builders and dumpsys parser. */
#include "../core/anytv_keys.h"
#include <stdio.h>
#include <string.h>

static int fails = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL %s\n", msg); fails++; } } while (0)

int main(void) {
    /* key codes */
    CHECK(anytv_key_code("DPAD_DOWN") == 20, "dpad down");
    CHECK(anytv_key_code("HOME") == 3, "home");
    CHECK(anytv_key_code("POWER") == 26, "power");
    CHECK(anytv_key_code("26") == 26, "numeric code");
    CHECK(anytv_key_code("NOPE") == -1, "unknown key");
    CHECK(strcmp(anytv_key_name(23), "DPAD_CENTER") == 0, "reverse lookup");

    /* key event builder */
    char buf[256];
    anytv_keyevent_command(20, 1, buf, sizeof buf);
    CHECK(strcmp(buf, "input keyevent 20") == 0, "single keyevent");
    anytv_keyevent_command(20, 3, buf, sizeof buf);
    CHECK(strcmp(buf, "input keyevent 20 20 20") == 0, "repeated keyevent");

    /* text escaping: spaces become %s, quotes survive the shell */
    anytv_input_text_command("oi tv", buf, sizeof buf);
    CHECK(strcmp(buf, "input text 'oi%stv'") == 0, "space escape");
    anytv_input_text_command("d'agua", buf, sizeof buf);
    CHECK(strcmp(buf, "input text 'd'\\''agua'") == 0, "quote escape");
    anytv_input_text_command("a\nb", buf, sizeof buf);
    CHECK(strcmp(buf, "input text 'ab'") == 0, "newlines dropped");

    /* launch + screencap commands */
    anytv_launch_command("com.netflix.ninja", buf, sizeof buf);
    CHECK(strstr(buf, "monkey -p com.netflix.ninja") != NULL, "launch command");
    CHECK(strstr(buf, "LEANBACK_LAUNCHER") != NULL, "launch prefers tv launcher");
    anytv_screencap_command(buf, sizeof buf);
    CHECK(strstr(buf, "screencap -p") != NULL, "screencap command");

    /* dumpsys fixture: two launchable packages, one duplicated category */
    const char *fixture =
        "Packages:\n"
        "  Package [com.netflix.ninja] (1a2b):\n"
        "    userId=10085\n"
        "    Activities:\n"
        "      com.netflix.ninja/.MainActivity filter 1a2c:\n"
        "        Action: \"android.intent.action.MAIN\"\n"
        "        Category: \"android.intent.category.LEANBACK_LAUNCHER\"\n"
        "        Category: \"android.intent.category.LAUNCHER\"\n"
        "  Package [com.google.android.youtube.tv] (2b3c):\n"
        "      com.google.android.youtube.tv/.MainActivity filter 2b3d:\n"
        "        Category: \"android.intent.category.LEANBACK_LAUNCHER\"\n"
        "  Package [com.system.service] (3c4d):\n"
        "      com.system.service/.Boot filter 3c4e:\n"
        "        Category: \"android.intent.category.DEFAULT\"\n";
    char pkgs[8][128];
    int count = anytv_parse_launchable(fixture, pkgs, 8);
    CHECK(count == 2, "two launchable packages");
    CHECK(count == 2 && strcmp(pkgs[0], "com.netflix.ninja") == 0, "first package");
    CHECK(count == 2 && strcmp(pkgs[1], "com.google.android.youtube.tv") == 0, "second package");

    printf("fails=%d\n", fails);
    return fails ? 1 : 0;
}
