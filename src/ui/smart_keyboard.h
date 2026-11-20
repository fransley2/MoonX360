#pragma once
/* MoonX360 Smart Virtual Keyboard — 5-Row Full PC Console Layout & Autocomplete Engine.
 * Platform-independent, zero-allocation UTF-8 editor, 6 field context modes,
 * 5-row full QWERTY (with secondary shift legends, Esc/Tab/Caps/Shift/Ctrl/Win/Alt/
 * Space/Arrows/Enter/Back), Accents page, Numeric IPv4 page, offline PT-BR/EN/ES
 * autocomplete, gamepad navigation with visual-X row alignment, analog deadzone/
 * hysteresis, held-X repeat, and experimental Dual-Stick mode. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "smart_keyboard_dict.h"

#define SK_TEXT_MAX           128
#define SK_COMPACT_ROWS       3
#define SK_COMPACT_COLS       10
#define SK_FULL_ROWS          5
#define SK_FULL_MAX_COLS      15
#define SK_NUM_COLS           3
#define SK_NUM_ROWS           4

/* Back-compat aliases */
#define SK_GRID_ROWS          SK_FULL_ROWS
#define SK_GRID_COLS          SK_FULL_MAX_COLS

#define SK_STICK_HIGH         13500
#define SK_STICK_LOW          9500
#define SK_DS_DEADZONE        7500
#define SK_REPEAT_DELAY_MS    280u
#define SK_REPEAT_RATE_MS     55u
#define SK_NAV_REPEAT_MS      105u

enum sk_mode {
    SK_MODE_TEXT = 0,
    SK_MODE_SEARCH,
    SK_MODE_PROFILE,
    SK_MODE_IP,
    SK_MODE_URL,
    SK_MODE_PASSWORD
};

enum sk_page {
    SK_PAGE_QWERTY = 0,
    SK_PAGE_SYMBOLS,
    SK_PAGE_ACCENTS,
    SK_PAGE_NUMERIC
};

enum sk_shift {
    SK_SHIFT_OFF = 0,
    SK_SHIFT_ONCE,
    SK_SHIFT_CAPS
};

enum sk_result {
    SK_RESULT_ACTIVE = 0,
    SK_RESULT_ACCEPTED,
    SK_RESULT_CANCELLED
};

enum sk_focus_zone {
    SK_ZONE_KEYS = 0,
    SK_ZONE_SUGGESTIONS
};

enum sk_key_kind {
    SK_KEY_CHAR = 0,
    SK_KEY_ESC,
    SK_KEY_BACK,
    SK_KEY_TAB,
    SK_KEY_CAPS,
    SK_KEY_ENTER,
    SK_KEY_SHIFT,
    SK_KEY_CTRL,
    SK_KEY_WIN,
    SK_KEY_ALT,
    SK_KEY_ALTGR,
    SK_KEY_SPACE,
    SK_KEY_UP,
    SK_KEY_DOWN,
    SK_KEY_LEFT,
    SK_KEY_RIGHT
};

struct sk_key_def {
    const char *lo;
    const char *hi;
    const char *sub;  /* secondary top-left legend (e.g. '!' on '1') */
    float weight;     /* proportional width; each row sums to 15.0 */
    int kind;
};

/* Gamepad snapshot passed into sk_step() (decoupled from XInput/Limelight). */
struct sk_pad {
    bool up, down, left, right;
    bool a, b, x, y;
    bool lb, rb, lt, rt;
    bool start, back, ls_click, rs_click;
    int lx, ly, rx, ry;
};

struct sk_state {
    enum sk_mode mode;
    enum sk_page page;
    enum sk_shift shift;
    enum sk_focus_zone zone;
    int lang;
    bool predictions_enabled;
    bool dual_stick;
    bool full_pc; /* false = 3x10 compact console layout (in-app); true = 5-row full PC layout (Desktop Mode) */

    /* Interactive modifier toggles (used directly in Desktop Mode chords) */
    bool ctrl_on, alt_on, win_on;
    /* Last special action emitted (consumed by remote adapter if active) */
    int pending_special;
    int pending_mods;

    char title[64];
    char subtitle[96];
    char text[SK_TEXT_MAX];
    char original[SK_TEXT_MAX];
    int cursor;       /* UTF-8 byte offset inside text */
    int max_bytes;    /* caller buffer capacity including NUL */

    int row, col;
    int right_row, right_col; /* experimental Dual-Stick right-half cursor */
    int sug_index;
    struct sk_suggestions suggestions;
    struct sk_user_dict *user_dict;

    /* Input edge, hysteresis, and controlled repeat state */
    struct sk_pad prev;
    int stick_x_latch, stick_y_latch, rstick_x_latch;
    bool dual_dpad_override;
    uint32_t x_held_since, x_last_repeat;
    uint32_t nav_held_since, nav_last_repeat;
    uint32_t cursor_held_since, cursor_last_repeat;
    int last_nav_dx, last_nav_dy;

    /* Validation feedback */
    char error[96];
    /* Telemetry counters for UX benchmarking */
    unsigned moves, insertions, backspaces, suggestions_accepted;
};

static const char *const sk_url_tokens[SK_MAX_SUGGESTIONS] = { "https://", ".com", "/" };

/* 5-row full PC Virtual Keyboard layout (each row totals 15.0 width units). */
static const int sk_full_row_counts[SK_FULL_ROWS] = { 15, 14, 13, 13, 10 };

static const struct sk_key_def sk_full_qwerty[SK_FULL_ROWS][SK_FULL_MAX_COLS] = {
    /* Row 0 (15 keys, sum = 15.0) */
    {
        {"Esc","Esc",NULL,1.0f,SK_KEY_ESC},
        {"`","~","~",0.95f,SK_KEY_CHAR},
        {"1","!","!",0.95f,SK_KEY_CHAR},
        {"2","@","@",0.95f,SK_KEY_CHAR},
        {"3","#","#",0.95f,SK_KEY_CHAR},
        {"4","$","$",0.95f,SK_KEY_CHAR},
        {"5","%","%",0.95f,SK_KEY_CHAR},
        {"6","^","^",0.95f,SK_KEY_CHAR},
        {"7","&","&",0.95f,SK_KEY_CHAR},
        {"8","*","*",0.95f,SK_KEY_CHAR},
        {"9","(","(",0.95f,SK_KEY_CHAR},
        {"0",")",")",0.95f,SK_KEY_CHAR},
        {"-","_","_",0.95f,SK_KEY_CHAR},
        {"=","+","+",0.95f,SK_KEY_CHAR},
        {"Back","Back",NULL,1.65f,SK_KEY_BACK}
    },
    /* Row 1 (14 keys, sum = 15.0) */
    {
        {"Tab","Tab",NULL,1.45f,SK_KEY_TAB},
        {"q","Q",NULL,1.0f,SK_KEY_CHAR},
        {"w","W",NULL,1.0f,SK_KEY_CHAR},
        {"e","E",NULL,1.0f,SK_KEY_CHAR},
        {"r","R",NULL,1.0f,SK_KEY_CHAR},
        {"t","T",NULL,1.0f,SK_KEY_CHAR},
        {"y","Y",NULL,1.0f,SK_KEY_CHAR},
        {"u","U",NULL,1.0f,SK_KEY_CHAR},
        {"i","I",NULL,1.0f,SK_KEY_CHAR},
        {"o","O",NULL,1.0f,SK_KEY_CHAR},
        {"p","P",NULL,1.0f,SK_KEY_CHAR},
        {"[","{","{",1.0f,SK_KEY_CHAR},
        {"]","}","}",1.0f,SK_KEY_CHAR},
        {"\\","|","|",1.55f,SK_KEY_CHAR}
    },
    /* Row 2 (13 keys, sum = 15.0) */
    {
        {"Caps","Caps",NULL,1.75f,SK_KEY_CAPS},
        {"a","A",NULL,1.0f,SK_KEY_CHAR},
        {"s","S",NULL,1.0f,SK_KEY_CHAR},
        {"d","D",NULL,1.0f,SK_KEY_CHAR},
        {"f","F",NULL,1.0f,SK_KEY_CHAR},
        {"g","G",NULL,1.0f,SK_KEY_CHAR},
        {"h","H",NULL,1.0f,SK_KEY_CHAR},
        {"j","J",NULL,1.0f,SK_KEY_CHAR},
        {"k","K",NULL,1.0f,SK_KEY_CHAR},
        {"l","L",NULL,1.0f,SK_KEY_CHAR},
        {";",":",":",1.0f,SK_KEY_CHAR},
        {"'","\"","\"",1.0f,SK_KEY_CHAR},
        {"Enter","Enter",NULL,2.25f,SK_KEY_ENTER}
    },
    /* Row 3 (13 keys, sum = 15.0) */
    {
        {"Shift","Shift",NULL,1.75f,SK_KEY_SHIFT},
        {"z","Z",NULL,1.0f,SK_KEY_CHAR},
        {"x","X",NULL,1.0f,SK_KEY_CHAR},
        {"c","C",NULL,1.0f,SK_KEY_CHAR},
        {"v","V",NULL,1.0f,SK_KEY_CHAR},
        {"b","B",NULL,1.0f,SK_KEY_CHAR},
        {"n","N",NULL,1.0f,SK_KEY_CHAR},
        {"m","M",NULL,1.0f,SK_KEY_CHAR},
        {",","<","<",1.0f,SK_KEY_CHAR},
        {".",">",">",1.0f,SK_KEY_CHAR},
        {"/","?","?",1.0f,SK_KEY_CHAR},
        {"^","^",NULL,1.0f,SK_KEY_UP},
        {"Shift","Shift",NULL,2.25f,SK_KEY_SHIFT}
    },
    /* Row 4 (10 keys, sum = 15.0) */
    {
        {"Ctrl","Ctrl",NULL,1.75f,SK_KEY_CTRL},
        {"Win","Win",NULL,1.15f,SK_KEY_WIN},
        {"Alt","Alt",NULL,1.15f,SK_KEY_ALT},
        {"Space","Space",NULL,4.75f,SK_KEY_SPACE},
        {"AltGr","AltGr",NULL,1.15f,SK_KEY_ALTGR},
        {"Win","Win",NULL,1.05f,SK_KEY_WIN},
        {"<","<",NULL,0.95f,SK_KEY_LEFT},
        {"v","v",NULL,0.95f,SK_KEY_DOWN},
        {">",">",NULL,0.95f,SK_KEY_RIGHT},
        {"Ctrl","Ctrl",NULL,1.15f,SK_KEY_CTRL}
    }
};

/* Accents page (PT-BR / ES) in the same 5-row structure (toggled via AltGr or RT). */
static const struct sk_key_def sk_full_accents[SK_FULL_ROWS][SK_FULL_MAX_COLS] = {
    {
        {"Esc","Esc",NULL,1.0f,SK_KEY_ESC},
        {"\xc3\xa1","\xc3\x81",NULL,0.95f,SK_KEY_CHAR},
        {"\xc3\xa0","\xc3\x80",NULL,0.95f,SK_KEY_CHAR},
        {"\xc3\xa2","\xc3\x82",NULL,0.95f,SK_KEY_CHAR},
        {"\xc3\xa3","\xc3\x83",NULL,0.95f,SK_KEY_CHAR},
        {"\xc3\xa9","\xc3\x89",NULL,0.95f,SK_KEY_CHAR},
        {"\xc3\xaa","\xc3\x8a",NULL,0.95f,SK_KEY_CHAR},
        {"\xc3\xad","\xc3\x8d",NULL,0.95f,SK_KEY_CHAR},
        {"\xc3\xb3","\xc3\x93",NULL,0.95f,SK_KEY_CHAR},
        {"\xc3\xb4","\xc3\x94",NULL,0.95f,SK_KEY_CHAR},
        {"\xc3\xb5","\xc3\x95",NULL,0.95f,SK_KEY_CHAR},
        {"\xc3\xba","\xc3\x9a",NULL,0.95f,SK_KEY_CHAR},
        {"\xc3\xbc","\xc3\x9c",NULL,0.95f,SK_KEY_CHAR},
        {"\xc3\xa7","\xc3\x87",NULL,0.95f,SK_KEY_CHAR},
        {"Back","Back",NULL,1.65f,SK_KEY_BACK}
    },
    {
        {"Tab","Tab",NULL,1.45f,SK_KEY_TAB},
        {"\xc3\xb1","\xc3\x91",NULL,1.0f,SK_KEY_CHAR},
        {"\xc2\xbf","\xc2\xbf",NULL,1.0f,SK_KEY_CHAR},
        {"\xc2\xa1","\xc2\xa1",NULL,1.0f,SK_KEY_CHAR},
        {"\xc2\xba","\xc2\xaa",NULL,1.0f,SK_KEY_CHAR},
        {"@","@",NULL,1.0f,SK_KEY_CHAR},
        {"#","#",NULL,1.0f,SK_KEY_CHAR},
        {"$","$",NULL,1.0f,SK_KEY_CHAR},
        {"%","%",NULL,1.0f,SK_KEY_CHAR},
        {"&","&",NULL,1.0f,SK_KEY_CHAR},
        {"*","*",NULL,1.0f,SK_KEY_CHAR},
        {"(","(",NULL,1.0f,SK_KEY_CHAR},
        {")",")",NULL,1.0f,SK_KEY_CHAR},
        {"/","\\",NULL,1.55f,SK_KEY_CHAR}
    },
    {
        {"Caps","Caps",NULL,1.75f,SK_KEY_CAPS},
        {"a","A",NULL,1.0f,SK_KEY_CHAR},
        {"s","S",NULL,1.0f,SK_KEY_CHAR},
        {"d","D",NULL,1.0f,SK_KEY_CHAR},
        {"f","F",NULL,1.0f,SK_KEY_CHAR},
        {"g","G",NULL,1.0f,SK_KEY_CHAR},
        {"h","H",NULL,1.0f,SK_KEY_CHAR},
        {"j","J",NULL,1.0f,SK_KEY_CHAR},
        {"k","K",NULL,1.0f,SK_KEY_CHAR},
        {"l","L",NULL,1.0f,SK_KEY_CHAR},
        {"\xc3\xa7","\xc3\x87",NULL,1.0f,SK_KEY_CHAR},
        {"~","^",NULL,1.0f,SK_KEY_CHAR},
        {"Enter","Enter",NULL,2.25f,SK_KEY_ENTER}
    },
    {
        {"Shift","Shift",NULL,1.75f,SK_KEY_SHIFT},
        {"z","Z",NULL,1.0f,SK_KEY_CHAR},
        {"x","X",NULL,1.0f,SK_KEY_CHAR},
        {"c","C",NULL,1.0f,SK_KEY_CHAR},
        {"v","V",NULL,1.0f,SK_KEY_CHAR},
        {"b","B",NULL,1.0f,SK_KEY_CHAR},
        {"n","N",NULL,1.0f,SK_KEY_CHAR},
        {"m","M",NULL,1.0f,SK_KEY_CHAR},
        {",",";",NULL,1.0f,SK_KEY_CHAR},
        {".",":",NULL,1.0f,SK_KEY_CHAR},
        {"?","!",NULL,1.0f,SK_KEY_CHAR},
        {"^","^",NULL,1.0f,SK_KEY_UP},
        {"Shift","Shift",NULL,2.25f,SK_KEY_SHIFT}
    },
    {
        {"Ctrl","Ctrl",NULL,1.75f,SK_KEY_CTRL},
        {"Win","Win",NULL,1.15f,SK_KEY_WIN},
        {"Alt","Alt",NULL,1.15f,SK_KEY_ALT},
        {"Space","Space",NULL,4.75f,SK_KEY_SPACE},
        {"ABC","ABC",NULL,1.15f,SK_KEY_ALTGR},
        {"Win","Win",NULL,1.05f,SK_KEY_WIN},
        {"<","<",NULL,0.95f,SK_KEY_LEFT},
        {"v","v",NULL,0.95f,SK_KEY_DOWN},
        {">",">",NULL,0.95f,SK_KEY_RIGHT},
        {"Ctrl","Ctrl",NULL,1.15f,SK_KEY_CTRL}
    }
};

static int sk_compact_cols(enum sk_page page) { return page == SK_PAGE_NUMERIC ? SK_NUM_COLS : 10; }
static int sk_compact_rows(enum sk_page page) { return page == SK_PAGE_NUMERIC ? SK_NUM_ROWS : 3; }

static const char *sk_compact_key_label(enum sk_page page, enum sk_shift shift, int row, int col)
{
    static const char *const qwerty_lo[3][10] = {
        {"q","w","e","r","t","y","u","i","o","p"},
        {"a","s","d","f","g","h","j","k","l","\xc3\xa7"},
        {"z","x","c","v","b","n","m",",",".","-"}
    };
    static const char *const qwerty_hi[3][10] = {
        {"Q","W","E","R","T","Y","U","I","O","P"},
        {"A","S","D","F","G","H","J","K","L","\xc3\x87"},
        {"Z","X","C","V","B","N","M",",",".","_"}
    };
    static const char *const symbols[3][10] = {
        {"1","2","3","4","5","6","7","8","9","0"},
        {"!","@","#","$","%","&","*","(",")","/"},
        {"+","=","-","_","\\",":",";","\"","'","?"}
    };
    static const char *const accents_lo[3][10] = {
        {"\xc3\xa1","\xc3\xa0","\xc3\xa2","\xc3\xa3","\xc3\xa9","\xc3\xaa","\xc3\xad","\xc3\xb3","\xc3\xb4","\xc3\xb5"},
        {"\xc3\xba","\xc3\xbc","\xc3\xa7","\xc3\xb1","\xc2\xbf","\xc2\xa1","@","/",":","-"},
        {"1","2","3","4","5","6","7","8","9","0"}
    };
    static const char *const accents_hi[3][10] = {
        {"\xc3\x81","\xc3\x80","\xc3\x82","\xc3\x83","\xc3\x89","\xc3\x8a","\xc3\x8d","\xc3\x93","\xc3\x94","\xc3\x95"},
        {"\xc3\x9a","\xc3\x9c","\xc3\x87","\xc3\x91","\xc2\xbf","\xc2\xa1","@","/",":","-"},
        {"1","2","3","4","5","6","7","8","9","0"}
    };
    static const char *const numeric[4][3] = {
        {"1","2","3"},{"4","5","6"},{"7","8","9"},{".","0","Bksp"}
    };
    if (page == SK_PAGE_NUMERIC) {
        if (row < 0 || row >= SK_NUM_ROWS || col < 0 || col >= SK_NUM_COLS) return "";
        return numeric[row][col];
    }
    if (row < 0 || row >= 3 || col < 0 || col >= 10) return "";
    bool hi = shift != SK_SHIFT_OFF;
    if (page == SK_PAGE_SYMBOLS) return symbols[row][col];
    if (page == SK_PAGE_ACCENTS) return hi ? accents_hi[row][col] : accents_lo[row][col];
    return hi ? qwerty_hi[row][col] : qwerty_lo[row][col];
}

static int sk_row_cols(enum sk_page page, int row)
{
    if (page == SK_PAGE_NUMERIC) return SK_NUM_COLS;
    if (row < 0 || row >= SK_FULL_ROWS) return 1;
    return sk_full_row_counts[row];
}

static int sk_total_rows(enum sk_page page)
{
    return page == SK_PAGE_NUMERIC ? SK_NUM_ROWS : SK_FULL_ROWS;
}

static const struct sk_key_def *sk_get_key(enum sk_page page, int row, int col)
{
    static const struct sk_key_def empty = {"", "", NULL, 1.0f, SK_KEY_CHAR};
    if (row < 0 || row >= SK_FULL_ROWS) return &empty;
    if (col < 0 || col >= sk_full_row_counts[row]) return &empty;
    return page == SK_PAGE_ACCENTS ? &sk_full_accents[row][col] : &sk_full_qwerty[row][col];
}

static const char *sk_key_label(enum sk_page page, enum sk_shift shift, int row, int col)
{
    static const char *const numeric[4][3] = {
        {"1","2","3"},
        {"4","5","6"},
        {"7","8","9"},
        {".","0","Bksp"}
    };
    if (page == SK_PAGE_NUMERIC) {
        if (row < 0 || row >= SK_NUM_ROWS || col < 0 || col >= SK_NUM_COLS) return "";
        return numeric[row][col];
    }
    const struct sk_key_def *k = sk_get_key(page, row, col);
    return (shift != SK_SHIFT_OFF) ? k->hi : k->lo;
}

/* Visual center X (in [0, 15]) of a key so Up/Down navigation lands naturally. */
static float sk_key_center_x(enum sk_page page, int row, int col)
{
    float x = 0.0f;
    int n = sk_row_cols(page, row);
    if (col >= n) col = n - 1;
    for (int i = 0; i < col; i++) x += sk_get_key(page, row, i)->weight;
    return x + sk_get_key(page, row, col)->weight * 0.5f;
}

static int sk_nearest_col(enum sk_page page, int target_row, float center_x)
{
    int n = sk_row_cols(page, target_row), best = 0;
    float best_d = 1e9f, x = 0.0f;
    for (int i = 0; i < n; i++) {
        float w = sk_get_key(page, target_row, i)->weight;
        float d = (x + w * 0.5f) - center_x;
        if (d < 0) d = -d;
        if (d < best_d) { best_d = d; best = i; }
        x += w;
    }
    return best;
}

static bool sk_is_utf8_cont(unsigned char c) { return (c & 0xC0u) == 0x80u; }

static void sk_cursor_left(struct sk_state *sk)
{
    if (sk->cursor <= 0) return;
    sk->cursor--;
    while (sk->cursor > 0 && sk_is_utf8_cont((unsigned char)sk->text[sk->cursor]))
        sk->cursor--;
}

static void sk_cursor_right(struct sk_state *sk)
{
    int len = (int)strlen(sk->text);
    if (sk->cursor >= len) return;
    sk->cursor++;
    while (sk->cursor < len && sk_is_utf8_cont((unsigned char)sk->text[sk->cursor]))
        sk->cursor++;
}

static bool sk_mode_allows_suggestions(enum sk_mode mode)
{
    return mode == SK_MODE_TEXT || mode == SK_MODE_SEARCH;
}

static void sk_extract_words(const struct sk_state *sk,
                             char *prefix, int pcap, int *word_start,
                             char *prev_word, int prev_cap)
{
    prefix[0] = '\0';
    prev_word[0] = '\0';
    int pos = sk->cursor;
    int start = pos;
    while (start > 0 && sk->text[start - 1] != ' ') start--;
    *word_start = start;
    int wlen = pos - start;
    if (wlen > 0) {
        if (wlen >= pcap) wlen = pcap - 1;
        memcpy(prefix, sk->text + start, (size_t)wlen);
        prefix[wlen] = '\0';
    }
    int p_end = start;
    while (p_end > 0 && sk->text[p_end - 1] == ' ') p_end--;
    int p_start = p_end;
    while (p_start > 0 && sk->text[p_start - 1] != ' ') p_start--;
    int plen = p_end - p_start;
    if (plen > 0) {
        if (plen >= prev_cap) plen = prev_cap - 1;
        memcpy(prev_word, sk->text + p_start, (size_t)plen);
        prev_word[plen] = '\0';
    }
}

static void sk_refresh_suggestions(struct sk_state *sk)
{
    memset(&sk->suggestions, 0, sizeof(sk->suggestions));
    if (sk->mode == SK_MODE_URL) {
        for (int i = 0; i < SK_MAX_SUGGESTIONS; i++)
            sk_suggestions_push(&sk->suggestions, sk_url_tokens[i]);
        if (sk->sug_index >= sk->suggestions.count) sk->sug_index = 0;
        return;
    }
    if (!sk->predictions_enabled || !sk_mode_allows_suggestions(sk->mode)) {
        sk->zone = SK_ZONE_KEYS;
        sk->sug_index = 0;
        return;
    }
    char prefix[SK_WORD_MAX_BYTES], prev_word[SK_WORD_MAX_BYTES];
    int word_start = 0;
    sk_extract_words(sk, prefix, (int)sizeof(prefix), &word_start, prev_word, (int)sizeof(prev_word));
    sk_suggest(sk->lang, sk->user_dict, prefix, prev_word, &sk->suggestions);
    if (sk->suggestions.count == 0) {
        sk->sug_index = 0;
        if (sk->zone == SK_ZONE_SUGGESTIONS) sk->zone = SK_ZONE_KEYS;
    } else if (sk->sug_index >= sk->suggestions.count) {
        sk->sug_index = sk->suggestions.count - 1;
    }
}

static bool sk_insert_utf8(struct sk_state *sk, const char *s)
{
    if (!s || !s[0]) return false;
    int len = (int)strlen(sk->text);
    int add = (int)strlen(s);
    if (len + add >= sk->max_bytes) return false;
    memmove(sk->text + sk->cursor + add, sk->text + sk->cursor, (size_t)(len - sk->cursor + 1));
    memcpy(sk->text + sk->cursor, s, (size_t)add);
    sk->cursor += add;
    sk->insertions++;
    sk->error[0] = '\0';
    if (sk->shift == SK_SHIFT_ONCE) sk->shift = SK_SHIFT_OFF;
    sk_refresh_suggestions(sk);
    return true;
}

static bool sk_backspace(struct sk_state *sk)
{
    if (sk->cursor <= 0) return false;
    int prev = sk->cursor - 1;
    while (prev > 0 && sk_is_utf8_cont((unsigned char)sk->text[prev])) prev--;
    int len = (int)strlen(sk->text);
    memmove(sk->text + prev, sk->text + sk->cursor, (size_t)(len - sk->cursor + 1));
    sk->cursor = prev;
    sk->backspaces++;
    sk->error[0] = '\0';
    sk_refresh_suggestions(sk);
    return true;
}

static bool sk_accept_suggestion(struct sk_state *sk, int index)
{
    if (index < 0 || index >= sk->suggestions.count) return false;
    const char *word = sk->suggestions.items[index];
    if (sk->mode == SK_MODE_URL) return sk_insert_utf8(sk, word);

    char prefix[SK_WORD_MAX_BYTES], prev_word[SK_WORD_MAX_BYTES];
    int word_start = 0;
    sk_extract_words(sk, prefix, (int)sizeof(prefix), &word_start, prev_word, (int)sizeof(prev_word));

    char replacement[SK_WORD_MAX_BYTES];
    snprintf(replacement, sizeof(replacement), "%s", word);
    if (prefix[0] >= 'A' && prefix[0] <= 'Z' && replacement[0] >= 'a' && replacement[0] <= 'z')
        replacement[0] = (char)(replacement[0] - ('a' - 'A'));

    int tail_len = (int)strlen(sk->text + sk->cursor);
    int rep_len = (int)strlen(replacement);
    bool add_space = (sk->text[sk->cursor] != ' ') && (word_start + rep_len + 1 + tail_len < sk->max_bytes);
    int total = word_start + rep_len + (add_space ? 1 : 0) + tail_len;
    if (total >= sk->max_bytes) return false;

    memmove(sk->text + word_start + rep_len + (add_space ? 1 : 0),
            sk->text + sk->cursor, (size_t)(tail_len + 1));
    memcpy(sk->text + word_start, replacement, (size_t)rep_len);
    if (add_space) sk->text[word_start + rep_len] = ' ';
    sk->cursor = word_start + rep_len + (add_space ? 1 : 0);
    sk->suggestions_accepted++;
    sk->zone = SK_ZONE_KEYS;
    sk->error[0] = '\0';
    sk_refresh_suggestions(sk);
    return true;
}

static bool sk_valid_ipv4(const char *s)
{
    if (!s || !s[0]) return false;
    int parts = 0, val = 0, digits = 0;
    for (const char *p = s; ; p++) {
        if (*p >= '0' && *p <= '9') {
            val = val * 10 + (*p - '0');
            if (++digits > 3 || val > 255) return false;
        } else if (*p == '.' || *p == '\0') {
            if (digits == 0) return false;
            parts++;
            val = 0; digits = 0;
            if (*p == '\0') break;
        } else {
            return false;
        }
    }
    return parts == 4;
}

static void sk_learn_sentence(struct sk_state *sk)
{
    if (!sk->user_dict || !sk->user_dict->enabled) return;
    if (!sk_mode_allows_suggestions(sk->mode)) return;
    const char *p = sk->text;
    while (*p) {
        while (*p == ' ') p++;
        char word[SK_WORD_MAX_BYTES];
        int n = 0;
        while (*p && *p != ' ' && n + 1 < SK_WORD_MAX_BYTES) word[n++] = *p++;
        while (*p && *p != ' ') p++;
        word[n] = '\0';
        if (n >= 2) sk_user_dict_learn(sk->user_dict, word);
    }
}

static void sk_init(struct sk_state *sk, enum sk_mode mode, int lang,
                    const char *title, const char *subtitle,
                    const char *initial, int max_bytes,
                    struct sk_user_dict *user_dict)
{
    memset(sk, 0, sizeof(*sk));
    sk->mode = mode;
    sk->lang = (lang >= 0 && lang < SK_LANG_COUNT) ? lang : SK_LANG_EN;
    sk->page = (mode == SK_MODE_IP) ? SK_PAGE_NUMERIC : SK_PAGE_QWERTY;
    sk->shift = SK_SHIFT_OFF;
    sk->zone = SK_ZONE_KEYS;
    sk->predictions_enabled = sk_mode_allows_suggestions(mode);
    sk->user_dict = user_dict;
    sk->max_bytes = (max_bytes > 1 && max_bytes <= SK_TEXT_MAX) ? max_bytes : SK_TEXT_MAX;
    if (title) snprintf(sk->title, sizeof(sk->title), "%s", title);
    if (subtitle) snprintf(sk->subtitle, sizeof(sk->subtitle), "%s", subtitle);
    if (initial) {
        int len = (int)strlen(initial);
        if (len >= sk->max_bytes) len = sk->max_bytes - 1;
        memcpy(sk->text, initial, (size_t)len);
        sk->text[len] = '\0';
        memcpy(sk->original, sk->text, (size_t)(len + 1));
        sk->cursor = len;
    }
    sk->row = (sk->page == SK_PAGE_NUMERIC) ? 1 : 1;
    sk->col = (sk->page == SK_PAGE_NUMERIC) ? 1 : 4;
    sk->right_row = 1;
    sk->right_col = 7;
    sk_refresh_suggestions(sk);
}

static int sk_stick_axis(int val, int prev_latch)
{
    if (prev_latch == 0) {
        if (val > SK_STICK_HIGH) return 1;
        if (val < -SK_STICK_HIGH) return -1;
        return 0;
    }
    if (prev_latch > 0) return val < SK_STICK_LOW ? 0 : 1;
    return val > -SK_STICK_LOW ? 0 : -1;
}

static float sk_ds_norm_axis(int val)
{
    if (val > SK_DS_DEADZONE) {
        float t = (float)(val - SK_DS_DEADZONE) / (float)(32767 - SK_DS_DEADZONE);
        return t > 1.0f ? 1.0f : t;
    }
    if (val < -SK_DS_DEADZONE) {
        float t = (float)(val + SK_DS_DEADZONE) / (float)(32768 - SK_DS_DEADZONE);
        return t < -1.0f ? -1.0f : t;
    }
    return 0.0f;
}

static int sk_ds_full_row(float ny)
{
    /* Full 5-row vertical mapping:
     * +1.0..+0.55 -> Row 0 (Numbers 1..0, Esc, Back)
     * +0.55..>0   -> Row 1 (Tab, q..p, [, ], \)
     *  0 (center) -> Row 2 (Caps, a..l, ;, ', Enter — D & K at deadzone center)
     *  <0..-0.55  -> Row 3 (Shift, z../, ^, Shift)
     * -0.55..-1.0 -> Row 4 (Ctrl, Win, Alt, Space, AltGr, Arrows) */
    if (ny > 0.55f)  return 0;
    if (ny > 0.0f)   return 1;
    if (ny < -0.55f) return 4;
    if (ny < 0.0f)   return 3;
    return 2;
}

static int sk_ds_compact_row(float ny)
{
    if (ny > 0.0f) return 0;
    if (ny < 0.0f) return 2;
    return 1;
}

static int sk_ds_compact_col(float nx, int base_col)
{
    /* Maps [-1, +1] onto 5 columns [base_col .. base_col+4] with 0.0 -> base_col+2 ('d' or 'k'). */
    if (nx == 0.0f) return base_col + 2;
    if (nx < 0.0f)  return nx < -0.5f ? base_col : base_col + 1;
    return nx > 0.5f ? base_col + 4 : base_col + 3;
}

static void sk_dual_stick_update(struct sk_state *sk, const struct sk_pad *pad)
{
    if (!sk->dual_stick || sk->page == SK_PAGE_NUMERIC) return;
    float lnx = sk_ds_norm_axis(pad->lx);
    float lny = sk_ds_norm_axis(pad->ly);
    float rnx = sk_ds_norm_axis(pad->rx);
    float rny = sk_ds_norm_axis(pad->ry);

    if (lnx != 0.0f || lny != 0.0f) sk->dual_dpad_override = false;

    if (!sk->dual_dpad_override) {
        int r, c;
        if (sk->full_pc) {
            r = sk_ds_full_row(lny);
            /* Center visual X = 4.25 ('d' on row 2); sweeps [0.0 .. 7.25] across all 5 rows. */
            float cx = lnx < 0.0f ? (4.25f + lnx * 4.25f) : (4.25f + lnx * 3.00f);
            c = sk_nearest_col(sk->page, r, cx);
        } else {
            r = sk_ds_compact_row(lny);
            c = sk_ds_compact_col(lnx, 0);
        }
        if (c != sk->col || r != sk->row) { sk->col = c; sk->row = r; sk->moves++; }
    }

    {
        int r, c;
        if (sk->full_pc) {
            r = sk_ds_full_row(rny);
            /* Center visual X = 9.25 ('k' on row 2); sweeps [6.75 .. 14.5] across all 5 rows. */
            float cx = rnx < 0.0f ? (9.25f + rnx * 2.50f) : (9.25f + rnx * 5.25f);
            c = sk_nearest_col(sk->page, r, cx);
        } else {
            r = sk_ds_compact_row(rny);
            c = sk_ds_compact_col(rnx, 5);
        }
        if (c != sk->right_col || r != sk->right_row) { sk->right_col = c; sk->right_row = r; sk->moves++; }
    }
}

static int sk_consume_mods(struct sk_state *sk)
{
    int mods = (sk->ctrl_on ? 0x02 : 0) | (sk->alt_on ? 0x04 : 0) | (sk->win_on ? 0x08 : 0);
    sk->ctrl_on = sk->alt_on = sk->win_on = false;
    return mods;
}

static enum sk_result sk_activate_key(struct sk_state *sk, int row, int col)
{
    if (!sk->full_pc || sk->page == SK_PAGE_NUMERIC) {
        const char *lbl = sk_compact_key_label(sk->page, sk->shift, row, col);
        if (sk->page == SK_PAGE_NUMERIC && !strcmp(lbl, "Bksp")) {
            sk_backspace(sk);
            sk->pending_special = SK_KEY_BACK;
            sk->pending_mods = sk_consume_mods(sk);
        } else {
            sk_insert_utf8(sk, lbl);
        }
        return SK_RESULT_ACTIVE;
    }
    const struct sk_key_def *k = sk_get_key(sk->page, row, col);
    switch (k->kind) {
    case SK_KEY_CHAR: {
        const char *ch = (sk->shift != SK_SHIFT_OFF) ? k->hi : k->lo;
        if ((sk->ctrl_on || sk->alt_on || sk->win_on) && (unsigned char)ch[0] < 0x80) {
            sk->pending_special = (unsigned char)ch[0];
            sk->pending_mods = sk_consume_mods(sk);
            if (sk->shift == SK_SHIFT_ONCE) sk->shift = SK_SHIFT_OFF;
        } else {
            sk_insert_utf8(sk, ch);
        }
        break;
    }
    case SK_KEY_SPACE:
        if (sk->ctrl_on || sk->alt_on || sk->win_on) {
            sk->pending_special = ' ';
            sk->pending_mods = sk_consume_mods(sk);
        } else {
            sk_insert_utf8(sk, " ");
        }
        break;
    case SK_KEY_BACK:
        sk_backspace(sk);
        sk->pending_special = SK_KEY_BACK;
        sk->pending_mods = sk_consume_mods(sk);
        break;
    case SK_KEY_CAPS:  sk->shift = (sk->shift == SK_SHIFT_CAPS) ? SK_SHIFT_OFF : SK_SHIFT_CAPS; break;
    case SK_KEY_SHIFT: sk->shift = (enum sk_shift)((sk->shift + 1) % 3); break;
    case SK_KEY_CTRL:  sk->ctrl_on = !sk->ctrl_on; break;
    case SK_KEY_ALT:   sk->alt_on = !sk->alt_on; break;
    case SK_KEY_WIN:   sk->win_on = !sk->win_on; sk->pending_special = SK_KEY_WIN; break;
    case SK_KEY_ALTGR: sk->page = (sk->page == SK_PAGE_ACCENTS) ? SK_PAGE_QWERTY : SK_PAGE_ACCENTS; break;
    case SK_KEY_ESC:
        sk->pending_special = SK_KEY_ESC;
        sk->pending_mods = sk_consume_mods(sk);
        strcpy(sk->text, sk->original);
        return SK_RESULT_CANCELLED;
    case SK_KEY_TAB:
        sk->pending_special = SK_KEY_TAB;
        sk->pending_mods = sk_consume_mods(sk);
        break;
    case SK_KEY_ENTER:
        sk->pending_special = SK_KEY_ENTER;
        sk->pending_mods = sk_consume_mods(sk);
        sk_learn_sentence(sk);
        return SK_RESULT_ACCEPTED;
    case SK_KEY_LEFT:
        sk_cursor_left(sk);
        sk_refresh_suggestions(sk);
        sk->pending_special = SK_KEY_LEFT;
        sk->pending_mods = sk_consume_mods(sk);
        break;
    case SK_KEY_RIGHT:
        sk_cursor_right(sk);
        sk_refresh_suggestions(sk);
        sk->pending_special = SK_KEY_RIGHT;
        sk->pending_mods = sk_consume_mods(sk);
        break;
    case SK_KEY_UP:
        sk->pending_special = SK_KEY_UP;
        sk->pending_mods = sk_consume_mods(sk);
        break;
    case SK_KEY_DOWN:
        sk->pending_special = SK_KEY_DOWN;
        sk->pending_mods = sk_consume_mods(sk);
        break;
    }
    return SK_RESULT_ACTIVE;
}

static enum sk_result sk_step(struct sk_state *sk, const struct sk_pad *pad, uint32_t now_ms)
{
    sk->pending_special = 0;
    sk->pending_mods = 0;

    bool p_a = pad->a && !sk->prev.a;
    bool p_b = pad->b && !sk->prev.b;
    bool p_x = pad->x && !sk->prev.x;
    bool p_y = pad->y && !sk->prev.y;
    bool p_lb = pad->lb && !sk->prev.lb;
    bool p_rb = pad->rb && !sk->prev.rb;
    bool p_lt = pad->lt && !sk->prev.lt;
    bool p_rt = pad->rt && !sk->prev.rt;
    bool p_start = pad->start && !sk->prev.start;
    bool p_rs_click = pad->rs_click && !sk->prev.rs_click;

    if (pad->back) {
        sk->prev = *pad;
        return SK_RESULT_ACTIVE;
    }

    if (p_b) {
        strcpy(sk->text, sk->original);
        sk->cursor = (int)strlen(sk->text);
        sk->ctrl_on = sk->alt_on = sk->win_on = false;
        sk->prev = *pad;
        return SK_RESULT_CANCELLED;
    }

    if (p_start) {
        if (sk->mode == SK_MODE_IP && !sk_valid_ipv4(sk->text)) {
            snprintf(sk->error, sizeof(sk->error), "Invalid IPv4 address (e.g. 192.168.1.10)");
            sk->prev = *pad;
            return SK_RESULT_ACTIVE;
        }
        sk_learn_sentence(sk);
        sk->prev = *pad;
        return SK_RESULT_ACCEPTED;
    }

    if (p_rs_click && sk->page != SK_PAGE_NUMERIC) {
        sk->dual_stick = !sk->dual_stick;
        sk->dual_dpad_override = false;
        if (sk->dual_stick) {
            sk->zone = SK_ZONE_KEYS;
            sk->row = sk->full_pc ? 2 : 1;
            sk->col = sk->full_pc ? 3 : 2;
            sk->right_row = sk->full_pc ? 2 : 1;
            sk->right_col = sk->full_pc ? 8 : 7;
        }
    }

    if (p_x) {
        sk_backspace(sk);
        sk->pending_special = SK_KEY_BACK;
        sk->pending_mods = sk_consume_mods(sk);
        sk->x_held_since = now_ms;
        sk->x_last_repeat = now_ms;
    } else if (pad->x) {
        if ((uint32_t)(now_ms - sk->x_held_since) >= SK_REPEAT_DELAY_MS &&
            (uint32_t)(now_ms - sk->x_last_repeat) >= SK_REPEAT_RATE_MS) {
            sk_backspace(sk);
            sk->pending_special = SK_KEY_BACK;
            sk->x_last_repeat = now_ms;
        }
    }

    if (p_y && sk->mode != SK_MODE_IP) sk_insert_utf8(sk, " ");

    if (sk->dual_stick && sk->page != SK_PAGE_NUMERIC) {
        /* Allow D-Pad precision override in Dual-Stick mode to reach any key effortlessly. */
        int ddx = (pad->right ? 1 : 0) - (pad->left ? 1 : 0);
        int ddy = (pad->down ? 1 : 0) - (pad->up ? 1 : 0);
        bool dpad_step = false;
        if (ddx != 0 || ddy != 0) {
            if (ddx != sk->last_nav_dx || ddy != sk->last_nav_dy) {
                dpad_step = true;
                sk->nav_held_since = now_ms;
                sk->nav_last_repeat = now_ms;
            } else if ((uint32_t)(now_ms - sk->nav_held_since) >= SK_REPEAT_DELAY_MS &&
                       (uint32_t)(now_ms - sk->nav_last_repeat) >= SK_NAV_REPEAT_MS) {
                dpad_step = true;
                sk->nav_last_repeat = now_ms;
            }
        }
        sk->last_nav_dx = ddx;
        sk->last_nav_dy = ddy;
        if (dpad_step) {
            sk->dual_dpad_override = true;
            int rows = sk->full_pc ? sk_total_rows(sk->page) : sk_compact_rows(sk->page);
            if (ddy != 0) {
                if (sk->full_pc) {
                    float cx = sk_key_center_x(sk->page, sk->row, sk->col);
                    sk->row = (sk->row + ddy + rows) % rows;
                    sk->col = sk_nearest_col(sk->page, sk->row, cx);
                } else {
                    sk->row = (sk->row + ddy + rows) % rows;
                }
            } else if (ddx != 0) {
                int cols = sk->full_pc ? sk_row_cols(sk->page, sk->row) : sk_compact_cols(sk->page);
                sk->col = (sk->col + ddx + cols) % cols;
            }
            sk->moves++;
        }

        sk_dual_stick_update(sk, pad);
        if (p_lt) {
            enum sk_result lres = sk_activate_key(sk, sk->row, sk->col);
            if (lres != SK_RESULT_ACTIVE) { sk->prev = *pad; return lres; }
        }
        if (p_rt || p_a) {
            enum sk_result rres = sk_activate_key(sk, sk->right_row, sk->right_col);
            if (rres != SK_RESULT_ACTIVE) { sk->prev = *pad; return rres; }
        }
        if (p_lb && sk->suggestions.count > 0) {
            sk->sug_index = (sk->sug_index + sk->suggestions.count - 1) % sk->suggestions.count;
            sk_accept_suggestion(sk, sk->sug_index);
        }
        if (p_rb && sk->suggestions.count > 0) {
            sk->sug_index = (sk->sug_index + 1) % sk->suggestions.count;
            sk_accept_suggestion(sk, sk->sug_index);
        }
        sk->prev = *pad;
        return SK_RESULT_ACTIVE;
    }

    if (p_lt && sk->page != SK_PAGE_NUMERIC) sk->shift = (enum sk_shift)((sk->shift + 1) % 3);
    if (p_rt && sk->page != SK_PAGE_NUMERIC) {
        if (sk->full_pc) sk->page = (sk->page == SK_PAGE_ACCENTS) ? SK_PAGE_QWERTY : SK_PAGE_ACCENTS;
        else sk->page = (enum sk_page)((sk->page + 1) % 3);
    }

    if ((p_lb || p_rb) && sk->suggestions.count > 0) {
        if (sk->zone != SK_ZONE_SUGGESTIONS) {
            sk->zone = SK_ZONE_SUGGESTIONS;
            sk->sug_index = p_lb ? 0 : (sk->suggestions.count > 1 ? 1 : 0);
        } else {
            sk->sug_index = (sk->sug_index + (p_rb ? 1 : sk->suggestions.count - 1)) % sk->suggestions.count;
        }
        sk->moves++;
    }

    int rx_dir = sk_stick_axis(pad->rx, sk->rstick_x_latch);
    if (rx_dir != 0 && rx_dir != sk->rstick_x_latch) {
        if (rx_dir < 0) sk_cursor_left(sk); else sk_cursor_right(sk);
        sk_refresh_suggestions(sk);
        sk->cursor_held_since = now_ms;
        sk->cursor_last_repeat = now_ms;
    } else if (rx_dir != 0 &&
               (uint32_t)(now_ms - sk->cursor_held_since) >= SK_REPEAT_DELAY_MS &&
               (uint32_t)(now_ms - sk->cursor_last_repeat) >= SK_NAV_REPEAT_MS) {
        if (rx_dir < 0) sk_cursor_left(sk); else sk_cursor_right(sk);
        sk_refresh_suggestions(sk);
        sk->cursor_last_repeat = now_ms;
    }
    sk->rstick_x_latch = rx_dir;

    int sx = sk_stick_axis(pad->lx, sk->stick_x_latch);
    int sy = sk_stick_axis(-pad->ly, sk->stick_y_latch);
    sk->stick_x_latch = sx;
    sk->stick_y_latch = sy;

    int dx = (pad->right ? 1 : 0) - (pad->left ? 1 : 0);
    int dy = (pad->down ? 1 : 0) - (pad->up ? 1 : 0);
    if (!dx) dx = sx;
    if (!dy) dy = sy;

    bool do_nav = false;
    if (dx != 0 || dy != 0) {
        if (dx != sk->last_nav_dx || dy != sk->last_nav_dy) {
            do_nav = true;
            sk->nav_held_since = now_ms;
            sk->nav_last_repeat = now_ms;
        } else if ((uint32_t)(now_ms - sk->nav_held_since) >= SK_REPEAT_DELAY_MS &&
                   (uint32_t)(now_ms - sk->nav_last_repeat) >= SK_NAV_REPEAT_MS) {
            do_nav = true;
            sk->nav_last_repeat = now_ms;
        }
    }
    sk->last_nav_dx = dx;
    sk->last_nav_dy = dy;

    if (do_nav) {
        int rows = sk->full_pc ? sk_total_rows(sk->page) : sk_compact_rows(sk->page);
        if (sk->zone == SK_ZONE_SUGGESTIONS) {
            if (dy > 0) { sk->zone = SK_ZONE_KEYS; sk->row = 0; }
            else if (dx != 0 && sk->suggestions.count > 0)
                sk->sug_index = (sk->sug_index + (dx > 0 ? 1 : sk->suggestions.count - 1)) % sk->suggestions.count;
        } else {
            if (dy < 0 && sk->row == 0 && sk->suggestions.count > 0) {
                sk->zone = SK_ZONE_SUGGESTIONS;
            } else if (dy != 0) {
                if (sk->full_pc && sk->page != SK_PAGE_NUMERIC) {
                    float cx = sk_key_center_x(sk->page, sk->row, sk->col);
                    sk->row = (sk->row + dy + rows) % rows;
                    sk->col = sk_nearest_col(sk->page, sk->row, cx);
                } else {
                    sk->row = (sk->row + dy + rows) % rows;
                }
            } else if (dx != 0) {
                int cols = sk->full_pc ? sk_row_cols(sk->page, sk->row) : sk_compact_cols(sk->page);
                sk->col = (sk->col + dx + cols) % cols;
            }
        }
        sk->moves++;
    }

    if (p_a) {
        if (sk->zone == SK_ZONE_SUGGESTIONS) {
            sk_accept_suggestion(sk, sk->sug_index);
        } else {
            enum sk_result ares = sk_activate_key(sk, sk->row, sk->col);
            if (ares != SK_RESULT_ACTIVE) { sk->prev = *pad; return ares; }
        }
    }

    sk->prev = *pad;
    return SK_RESULT_ACTIVE;
}
