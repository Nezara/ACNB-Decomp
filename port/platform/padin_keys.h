/* padin bindings tables (phase 3 step 3 P4), header-only so C hosts (the settings launcher) can use them without
 * linking padin: the bindable pad actions (libpad buttons + digital stick directions) with their default keys, and the
 * key names <-> USB HID usage (= SDL_Scancode) <-> Windows virtual-key table. Generic: no ACNB addresses (Last Raven
 * reusable). Settings form (the host's, e.g. acnb.ini [input]): key_<action name> = <key name>[,<key name>], "none" or
 * empty = unbound; names are matched case-insensitively. F1-F12 are not in the table (host keys: perf overlay, replay
 * segment marks, fullscreen, screenshot). */
#pragma once
#include <stdint.h>

#define PADIN_NACT 24      /* actions 0..15 = libpad button bits (ps2_pad.h PadFrame), 16..23 = stick directions */
#define PADIN_KEYS_PER 2   /* keys per action */

enum {
    PADIN_A_LSTICK_UP = 16, PADIN_A_LSTICK_DOWN, PADIN_A_LSTICK_LEFT, PADIN_A_LSTICK_RIGHT,
    PADIN_A_RSTICK_UP, PADIN_A_RSTICK_DOWN, PADIN_A_RSTICK_LEFT, PADIN_A_RSTICK_RIGHT,
};

typedef struct PadinAction { const char *name, *label, *def; } PadinAction;
/* defaults = the hybrid's keyboard layout before P4 (runtime/video.c poll_input) */
static const PadinAction padin_actions[PADIN_NACT] = {
    {"l2", "L2", "1"}, {"r2", "R2", "2"}, {"l1", "L1", "Q"}, {"r1", "R1", "W"},
    {"triangle", "Triangle", "S"}, {"circle", "Circle", "X"}, {"cross", "Cross", "Z"}, {"square", "Square", "A"},
    {"select", "Select", "Backspace"}, {"l3", "L3", "E"}, {"r3", "R3", "R"}, {"start", "Start", "Return"},
    {"up", "D-pad up", "Up"}, {"right", "D-pad right", "Right"}, {"down", "D-pad down", "Down"}, {"left", "D-pad left", "Left"},
    {"lstick_up", "Left stick up", "I"}, {"lstick_down", "Left stick down", "K"},
    {"lstick_left", "Left stick left", "J"}, {"lstick_right", "Left stick right", "L"},
    {"rstick_up", "Right stick up", ""}, {"rstick_down", "Right stick down", ""},
    {"rstick_left", "Right stick left", ""}, {"rstick_right", "Right stick right", ""},
};

typedef struct PadinKey { const char *name; uint16_t hid; uint16_t vk; } PadinKey;
static const PadinKey padin_keys[] = {
    {"A", 4, 'A'}, {"B", 5, 'B'}, {"C", 6, 'C'}, {"D", 7, 'D'}, {"E", 8, 'E'}, {"F", 9, 'F'}, {"G", 10, 'G'},
    {"H", 11, 'H'}, {"I", 12, 'I'}, {"J", 13, 'J'}, {"K", 14, 'K'}, {"L", 15, 'L'}, {"M", 16, 'M'}, {"N", 17, 'N'},
    {"O", 18, 'O'}, {"P", 19, 'P'}, {"Q", 20, 'Q'}, {"R", 21, 'R'}, {"S", 22, 'S'}, {"T", 23, 'T'}, {"U", 24, 'U'},
    {"V", 25, 'V'}, {"W", 26, 'W'}, {"X", 27, 'X'}, {"Y", 28, 'Y'}, {"Z", 29, 'Z'},
    {"1", 30, '1'}, {"2", 31, '2'}, {"3", 32, '3'}, {"4", 33, '4'}, {"5", 34, '5'}, {"6", 35, '6'}, {"7", 36, '7'},
    {"8", 37, '8'}, {"9", 38, '9'}, {"0", 39, '0'},
    {"Return", 40, 0x0d}, {"Escape", 41, 0x1b}, {"Backspace", 42, 0x08}, {"Tab", 43, 0x09}, {"Space", 44, 0x20},
    {"Minus", 45, 0xbd}, {"Equals", 46, 0xbb}, {"LeftBracket", 47, 0xdb}, {"RightBracket", 48, 0xdd},
    {"Backslash", 49, 0xdc}, {"Semicolon", 51, 0xba}, {"Apostrophe", 52, 0xde}, {"Grave", 53, 0xc0},
    {"Comma", 54, 0xbc}, {"Period", 55, 0xbe}, {"Slash", 56, 0xbf}, {"CapsLock", 57, 0x14},
    {"Insert", 73, 0x2d}, {"Home", 74, 0x24}, {"PageUp", 75, 0x21}, {"Delete", 76, 0x2e}, {"End", 77, 0x23},
    {"PageDown", 78, 0x22}, {"Right", 79, 0x27}, {"Left", 80, 0x25}, {"Down", 81, 0x28}, {"Up", 82, 0x26},
    {"KPDivide", 84, 0x6f}, {"KPMultiply", 85, 0x6a}, {"KPMinus", 86, 0x6d}, {"KPPlus", 87, 0x6b},
    {"KP1", 89, 0x61}, {"KP2", 90, 0x62}, {"KP3", 91, 0x63}, {"KP4", 92, 0x64}, {"KP5", 93, 0x65},
    {"KP6", 94, 0x66}, {"KP7", 95, 0x67}, {"KP8", 96, 0x68}, {"KP9", 97, 0x69}, {"KP0", 98, 0x60},
    {"KPPeriod", 99, 0x6e},
    {"LCtrl", 224, 0xa2}, {"LShift", 225, 0xa0}, {"LAlt", 226, 0xa4}, {"RCtrl", 228, 0xa3}, {"RShift", 229, 0xa1},
    {"RAlt", 230, 0xa5},
};
#define PADIN_NKEYS ((int)(sizeof padin_keys / sizeof *padin_keys))

static inline int padin_key_ieq(const char *a, const char *b, int n)   /* n < 0: whole strings */
{
    for (int i = 0; n < 0 || i < n; i++) {
        char x = a[i], y = b[i];
        if (x >= 'a' && x <= 'z') x -= 32;
        if (y >= 'a' && y <= 'z') y -= 32;
        if (x != y) return 0;
        if (!x) return 1;
    }
    return !b[n];
}
/* key name (n chars, n < 0 = whole string) -> table index, -1 = unknown */
static inline int padin_key_find(const char *name, int n)
{
    for (int i = 0; i < PADIN_NKEYS; i++) if (padin_key_ieq(name, padin_keys[i].name, n)) return i;
    return -1;
}
static inline int padin_key_by_vk(unsigned vk)
{
    for (int i = 0; i < PADIN_NKEYS; i++) if (padin_keys[i].vk == vk) return i;
    return -1;
}
static inline int padin_key_by_hid(unsigned hid)
{
    for (int i = 0; i < PADIN_NKEYS; i++) if (padin_keys[i].hid == hid) return i;
    return -1;
}
/* "Z" / "Z, Space" / "none" / "" -> up to PADIN_KEYS_PER HID codes (0 = none); returns 0 if a name is unknown (the
 * known ones are still stored), 1 if all are fine */
static inline int padin_parse_keys(const char *s, uint16_t out[PADIN_KEYS_PER])
{
    int n = 0, ok = 1;
    for (int k = 0; k < PADIN_KEYS_PER; k++) out[k] = 0;
    while (s && *s) {
        while (*s == ' ' || *s == '\t' || *s == ',') s++;
        const char *e = s;
        while (*e && *e != ',' && *e != ' ' && *e != '\t') e++;
        if (e == s) break;
        if (!padin_key_ieq(s, "none", (int)(e - s))) {
            int i = padin_key_find(s, (int)(e - s));
            if (i < 0) ok = 0;
            else if (n < PADIN_KEYS_PER) out[n++] = padin_keys[i].hid;
        }
        s = e;
    }
    return ok;
}
