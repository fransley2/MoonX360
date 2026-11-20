#pragma once
/* MoonX360 Smart Keyboard — Remote Streaming & Desktop Mode Adapter.
 * Reuses the exact Smart Keyboard core (smart_keyboard.h + smart_keyboard_dict.h)
 * without duplicating layouts, autocomplete, or input handling.
 * Supports Compose Mode (default) and Direct Typing, Compact/Full/Floating layouts,
 * UTF-8 delivery with ASCII VK fallback, and paired Windows modifier chords. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "smart_keyboard.h"

/* Virtual-key constants matching Windows / Moonlight-common-c */
#define SK_VK_BACK    0x08
#define SK_VK_TAB     0x09
#define SK_VK_RETURN  0x0D
#define SK_VK_SHIFT   0x10
#define SK_VK_ESCAPE  0x1B
#define SK_VK_END     0x23
#define SK_VK_HOME    0x24
#define SK_VK_LEFT    0x25
#define SK_VK_UP      0x26
#define SK_VK_RIGHT   0x27
#define SK_VK_DOWN    0x28
#define SK_VK_DELETE  0x2E
#define SK_VK_LWIN    0x5B
#define SK_VK_LSHIFT  0xA0
#define SK_VK_LCTRL   0xA2
#define SK_VK_LALT    0xA4

#define SK_MOD_SHIFT  0x01
#define SK_MOD_CTRL   0x02
#define SK_MOD_ALT    0x04
#define SK_MOD_META   0x08
#define SK_MOD_EXT    0x10

enum sk_target {
    SK_TARGET_LOCAL = 0,
    SK_TARGET_REMOTE_DESKTOP,
    SK_TARGET_REMOTE_GAME
};

enum sk_overlay_layout {
    SK_LAYOUT_COMPACT = 0,
    SK_LAYOUT_FULL,
    SK_LAYOUT_FLOATING_TOP,
    SK_LAYOUT_FLOATING_BOTTOM,
    SK_LAYOUT_COUNT
};

enum sk_send_mode {
    SK_SEND_COMPOSE = 0,
    SK_SEND_DIRECT
};

enum sk_desktop_cmd {
    SK_CMD_ENTER = 0,
    SK_CMD_TAB,
    SK_CMD_ESC,
    SK_CMD_CTRL_A,
    SK_CMD_CTRL_C,
    SK_CMD_CTRL_V,
    SK_CMD_ALT_TAB,
    SK_CMD_MODE_TOGGLE,
    SK_CMD_COUNT
};

/* Transport callbacks injected by input_xdk.cpp (Limelight) or host tests. */
struct sk_remote_ops {
    int (*send_utf8)(const char *text, unsigned length);
    void (*send_vk)(short vk, char action, char modifiers);
};

struct sk_remote_session {
    bool active;
    bool neutral;
    bool back_held_clean;
    bool prefs_applied;
    enum sk_target target;
    enum sk_overlay_layout layout;
    enum sk_send_mode send_mode;
    bool utf8_fallback_used;
    bool send_failed;
    bool toolbar_focused;
    int toolbar_index;
    unsigned revision;
    struct sk_state sk;
    struct sk_user_dict user_dict;
    struct sk_remote_ops ops;
};

static const char *sk_desktop_cmd_label(int cmd, enum sk_send_mode mode)
{
    switch (cmd) {
    case SK_CMD_ENTER:       return "Enter";
    case SK_CMD_TAB:         return "Tab";
    case SK_CMD_ESC:         return "Esc";
    case SK_CMD_CTRL_A:      return "Ctrl+A";
    case SK_CMD_CTRL_C:      return "Ctrl+C";
    case SK_CMD_CTRL_V:      return "Ctrl+V";
    case SK_CMD_ALT_TAB:     return "Alt+Tab";
    case SK_CMD_MODE_TOGGLE: return mode == SK_SEND_DIRECT ? "Direct" : "Compose";
    default:                 return "";
    }
}

static const char *sk_overlay_layout_name(enum sk_overlay_layout layout)
{
    switch (layout) {
    case SK_LAYOUT_FULL:            return "Full";
    case SK_LAYOUT_FLOATING_TOP:    return "Float Top";
    case SK_LAYOUT_FLOATING_BOTTOM: return "Float Bottom";
    default:                        return "Compact";
    }
}

/* Always pairs every DOWN with UP and releases modifiers in reverse order. */
static void sk_remote_send_chord(const struct sk_remote_ops *ops, short vk, int mods)
{
    if (!ops || !ops->send_vk) return;
    if (mods & SK_MOD_META)  ops->send_vk(SK_VK_LWIN, 0x03, (char)(SK_MOD_META | SK_MOD_EXT));
    if (mods & SK_MOD_CTRL)  ops->send_vk(SK_VK_LCTRL, 0x03, (char)(mods & (SK_MOD_META | SK_MOD_CTRL)));
    if (mods & SK_MOD_ALT)   ops->send_vk(SK_VK_LALT, 0x03, (char)(mods & (SK_MOD_META | SK_MOD_CTRL | SK_MOD_ALT)));
    if (mods & SK_MOD_SHIFT) ops->send_vk(SK_VK_LSHIFT, 0x03, (char)mods);

    bool ext = (vk >= 0x21 && vk <= 0x2E) || vk == SK_VK_LWIN;
    char key_mods = (char)((mods & 0x0F) | (ext ? SK_MOD_EXT : 0));
    ops->send_vk(vk, 0x03, key_mods); /* KEY_ACTION_DOWN */
    ops->send_vk(vk, 0x04, key_mods); /* KEY_ACTION_UP */

    if (mods & SK_MOD_SHIFT) ops->send_vk(SK_VK_LSHIFT, 0x04, (char)(mods & ~SK_MOD_SHIFT));
    if (mods & SK_MOD_ALT)   ops->send_vk(SK_VK_LALT, 0x04, (char)(mods & ~(SK_MOD_SHIFT | SK_MOD_ALT)));
    if (mods & SK_MOD_CTRL)  ops->send_vk(SK_VK_LCTRL, 0x04, (char)(mods & SK_MOD_META));
    if (mods & SK_MOD_META)  ops->send_vk(SK_VK_LWIN, 0x04, (char)SK_MOD_EXT);
}

/* Fallback ASCII virtual-key delivery when a host/app rejects UTF-8 text events. */
static bool sk_remote_send_ascii_fallback(const struct sk_remote_ops *ops, const char *text)
{
    if (!ops || !ops->send_vk || !text) return false;
    bool sent_any = false;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        unsigned char c = *p;
        if (c >= 'a' && c <= 'z') { sk_remote_send_chord(ops, (short)('A' + (c - 'a')), 0); sent_any = true; }
        else if (c >= 'A' && c <= 'Z') { sk_remote_send_chord(ops, (short)c, SK_MOD_SHIFT); sent_any = true; }
        else if (c >= '0' && c <= '9') { sk_remote_send_chord(ops, (short)c, 0); sent_any = true; }
        else if (c == ' ') { sk_remote_send_chord(ops, 0x20, 0); sent_any = true; }
        else if (c == '.') { sk_remote_send_chord(ops, 0xBE, 0); sent_any = true; }
        else if (c == ',') { sk_remote_send_chord(ops, 0xBC, 0); sent_any = true; }
        else if (c == '-') { sk_remote_send_chord(ops, 0xBD, 0); sent_any = true; }
        else if (c == '_') { sk_remote_send_chord(ops, 0xBD, SK_MOD_SHIFT); sent_any = true; }
        else if (c == '/') { sk_remote_send_chord(ops, 0xBF, 0); sent_any = true; }
        else if (c == ':') { sk_remote_send_chord(ops, 0xBA, SK_MOD_SHIFT); sent_any = true; }
        else if (c == ';') { sk_remote_send_chord(ops, 0xBA, 0); sent_any = true; }
        else if (c == '\n') { sk_remote_send_chord(ops, SK_VK_RETURN, 0); sent_any = true; }
    }
    return sent_any;
}

/* Prefer LiSendUtf8TextEvent (full Unicode/PT-BR/ES); fallback to ASCII VK if rejected. */
static bool sk_remote_send_text(struct sk_remote_session *rs, const char *text)
{
    if (!rs || !text || !text[0]) return true;
    unsigned len = (unsigned)strlen(text);
    if (rs->ops.send_utf8 && rs->ops.send_utf8(text, len) == 0) {
        rs->send_failed = false;
        return true;
    }
    rs->utf8_fallback_used = true;
    if (sk_remote_send_ascii_fallback(&rs->ops, text)) {
        rs->send_failed = false;
        return true;
    }
    rs->send_failed = true;
    return false;
}

static void sk_remote_run_cmd(struct sk_remote_session *rs, int cmd)
{
    switch (cmd) {
    case SK_CMD_ENTER:       sk_remote_send_chord(&rs->ops, SK_VK_RETURN, 0); break;
    case SK_CMD_TAB:         sk_remote_send_chord(&rs->ops, SK_VK_TAB, 0); break;
    case SK_CMD_ESC:         sk_remote_send_chord(&rs->ops, SK_VK_ESCAPE, 0); break;
    case SK_CMD_CTRL_A:      sk_remote_send_chord(&rs->ops, 'A', SK_MOD_CTRL); break;
    case SK_CMD_CTRL_C:      sk_remote_send_chord(&rs->ops, 'C', SK_MOD_CTRL); break;
    case SK_CMD_CTRL_V:      sk_remote_send_chord(&rs->ops, 'V', SK_MOD_CTRL); break;
    case SK_CMD_ALT_TAB:     sk_remote_send_chord(&rs->ops, SK_VK_TAB, SK_MOD_ALT); break;
    case SK_CMD_MODE_TOGGLE:
        rs->send_mode = (rs->send_mode == SK_SEND_COMPOSE) ? SK_SEND_DIRECT : SK_SEND_COMPOSE;
        rs->sk.predictions_enabled = (rs->send_mode == SK_SEND_COMPOSE);
        sk_refresh_suggestions(&rs->sk);
        break;
    }
    rs->revision++;
}

static bool sk_remote_char_to_vk(unsigned char c, short *vk, int *extra_mods)
{
    int em = 0;
    short code = 0;
    if (c >= 'a' && c <= 'z') code = (short)('A' + (c - 'a'));
    else if (c >= 'A' && c <= 'Z') { code = (short)c; em = SK_MOD_SHIFT; }
    else if (c >= '0' && c <= '9') code = (short)c;
    else if (c == ' ') code = 0x20;
    else if (c == '-') code = 0xBD;
    else if (c == '_') { code = 0xBD; em = SK_MOD_SHIFT; }
    else if (c == '=') code = 0xBB;
    else if (c == '+') { code = 0xBB; em = SK_MOD_SHIFT; }
    else if (c == '[') code = 0xDB;
    else if (c == '{') { code = 0xDB; em = SK_MOD_SHIFT; }
    else if (c == ']') code = 0xDD;
    else if (c == '}') { code = 0xDD; em = SK_MOD_SHIFT; }
    else if (c == '\\') code = 0xDC;
    else if (c == '|') { code = 0xDC; em = SK_MOD_SHIFT; }
    else if (c == ';') code = 0xBA;
    else if (c == ':') { code = 0xBA; em = SK_MOD_SHIFT; }
    else if (c == '\'') code = 0xDE;
    else if (c == '"') { code = 0xDE; em = SK_MOD_SHIFT; }
    else if (c == ',') code = 0xBC;
    else if (c == '<') { code = 0xBC; em = SK_MOD_SHIFT; }
    else if (c == '.') code = 0xBE;
    else if (c == '>') { code = 0xBE; em = SK_MOD_SHIFT; }
    else if (c == '/') code = 0xBF;
    else if (c == '?') { code = 0xBF; em = SK_MOD_SHIFT; }
    else if (c == '`') code = 0xC0;
    else if (c == '~') { code = 0xC0; em = SK_MOD_SHIFT; }
    else return false;
    *vk = code;
    if (extra_mods) *extra_mods = em;
    return true;
}

static void sk_remote_apply_layout_grid(struct sk_remote_session *rs, bool full_pc)
{
    if (!rs) return;
    rs->sk.full_pc = full_pc;
    if (full_pc && rs->sk.page == SK_PAGE_SYMBOLS) rs->sk.page = SK_PAGE_QWERTY;
    int rows = full_pc ? sk_total_rows(rs->sk.page) : sk_compact_rows(rs->sk.page);
    if (rs->sk.row < 0 || rs->sk.row >= rows) rs->sk.row = full_pc ? 2 : 1;
    int cols = full_pc ? sk_row_cols(rs->sk.page, rs->sk.row) : sk_compact_cols(rs->sk.page);
    if (rs->sk.col < 0 || rs->sk.col >= cols) rs->sk.col = full_pc ? 3 : 2;
    if (rs->sk.right_row < 0 || rs->sk.right_row >= rows) rs->sk.right_row = full_pc ? 2 : 1;
    int rcols = full_pc ? sk_row_cols(rs->sk.page, rs->sk.right_row) : sk_compact_cols(rs->sk.page);
    if (rs->sk.right_col < 0 || rs->sk.right_col >= rcols) rs->sk.right_col = full_pc ? 8 : 7;
}

static void sk_remote_apply_prefs(struct sk_remote_session *rs, int kb_style, int kb_send_mode)
{
    if (!rs) return;
    bool want_full = (kb_style == 0);
    rs->layout = want_full ? SK_LAYOUT_FULL : SK_LAYOUT_COMPACT;
    rs->send_mode = (kb_send_mode == 0) ? SK_SEND_DIRECT : SK_SEND_COMPOSE;
    rs->toolbar_focused = false;
    rs->sk.predictions_enabled = (rs->send_mode == SK_SEND_COMPOSE);
    sk_remote_apply_layout_grid(rs, want_full);
    if (rs->send_mode == SK_SEND_DIRECT) {
        rs->sk.text[0] = '\0';
        rs->sk.cursor = 0;
    }
    sk_refresh_suggestions(&rs->sk);
    rs->revision++;
}

static void sk_remote_open(struct sk_remote_session *rs, enum sk_target target, int lang,
                           const struct sk_remote_ops *ops)
{
    /* Preserve user's last layout, send_mode, and learned dictionary across openings. */
    enum sk_overlay_layout saved_layout = rs->layout < SK_LAYOUT_COUNT ? rs->layout : SK_LAYOUT_COMPACT;
    enum sk_send_mode saved_mode = rs->send_mode;
    bool saved_full_pc = rs->sk.full_pc;
    bool saved_prefs = rs->prefs_applied;
    rs->active = true;
    rs->neutral = true;
    rs->back_held_clean = false;
    rs->prefs_applied = saved_prefs;
    rs->target = target;
    rs->layout = saved_layout;
    rs->send_mode = saved_mode;
    rs->send_failed = false;
    rs->utf8_fallback_used = false;
    rs->toolbar_focused = false;
    rs->toolbar_index = 0;
    if (ops) rs->ops = *ops;
    sk_init(&rs->sk, SK_MODE_TEXT, lang,
            target == SK_TARGET_REMOTE_GAME ? "Game Keyboard" : "Desktop Keyboard",
            saved_mode == SK_SEND_DIRECT ? "Direct Typing" : "Compose & Send",
            "", SK_TEXT_MAX, &rs->user_dict);
    bool full_pc = saved_layout == SK_LAYOUT_FULL ? true
                 : saved_layout == SK_LAYOUT_COMPACT ? false
                 : saved_full_pc;
    sk_remote_apply_layout_grid(rs, full_pc);
    rs->sk.row = full_pc ? 2 : 1;
    rs->sk.col = full_pc ? 5 : 4;
    rs->sk.right_row = full_pc ? 2 : 1;
    rs->sk.right_col = full_pc ? 8 : 7;
    rs->sk.predictions_enabled = (rs->send_mode == SK_SEND_COMPOSE);
    sk_refresh_suggestions(&rs->sk);
    rs->revision++;
}

static void sk_remote_close(struct sk_remote_session *rs)
{
    if (!rs) return;
    rs->active = false;
    rs->neutral = true;
    rs->back_held_clean = false;
    rs->toolbar_focused = false;
    rs->revision++;
}

static bool sk_remote_pad_neutral(const struct sk_pad *p)
{
    return !p->a && !p->b && !p->x && !p->y && !p->lb && !p->rb &&
           !p->lt && !p->rt && !p->start && !p->back &&
           !p->up && !p->down && !p->left && !p->right;
}

/* Single input-thread step when Smart Keyboard is open over streaming.
 * Returns true while keyboard remains active; false when closed/submitted. */
static bool sk_remote_step(struct sk_remote_session *rs, const struct sk_pad *pad, uint32_t now_ms)
{
    if (!rs || !rs->active) return false;
    if (rs->neutral) {
        if (sk_remote_pad_neutral(pad)) {
            rs->neutral = false;
            rs->sk.prev = *pad;
        }
        return true;
    }

    bool p_ls = pad->ls_click && !rs->sk.prev.ls_click;
    bool p_up = pad->up && !rs->sk.prev.up;
    bool p_down = pad->down && !rs->sk.prev.down;
    bool p_left = pad->left && !rs->sk.prev.left;
    bool p_right = pad->right && !rs->sk.prev.right;
    bool p_a = pad->a && !rs->sk.prev.a;
    bool p_b = pad->b && !rs->sk.prev.b;

    /* Clean Back tap-and-release toggles Direct <-> Compose without breaking Back chords. */
    if (pad->back) {
        bool any_other = pad->a || pad->b || pad->x || pad->y || pad->lb || pad->rb ||
                         pad->lt || pad->rt || pad->start || pad->ls_click || pad->rs_click ||
                         pad->up || pad->down || pad->left || pad->right;
        if (!rs->sk.prev.back) rs->back_held_clean = !any_other;
        else if (any_other) rs->back_held_clean = false;
    } else if (rs->sk.prev.back && rs->back_held_clean) {
        rs->back_held_clean = false;
        rs->send_mode = (rs->send_mode == SK_SEND_DIRECT) ? SK_SEND_COMPOSE : SK_SEND_DIRECT;
        rs->sk.predictions_enabled = (rs->send_mode == SK_SEND_COMPOSE);
        if (rs->send_mode == SK_SEND_DIRECT) {
            rs->sk.text[0] = '\0';
            rs->sk.cursor = 0;
        }
        sk_refresh_suggestions(&rs->sk);
        rs->revision++;
        rs->sk.prev = *pad;
        return true;
    }

    /* L3 cycles overlay layout (Compact -> Full -> Float Top -> Float Bottom) */
    if (p_ls && !pad->back) {
        rs->layout = (enum sk_overlay_layout)((rs->layout + 1) % SK_LAYOUT_COUNT);
        if (rs->layout == SK_LAYOUT_COMPACT) sk_remote_apply_layout_grid(rs, false);
        else if (rs->layout == SK_LAYOUT_FULL) sk_remote_apply_layout_grid(rs, true);
        rs->toolbar_focused = false;
        rs->revision++;
        rs->sk.prev = *pad;
        return true;
    }

    /* Full layout includes desktop shortcut toolbar below the bottom key row. */
    int max_row = (rs->sk.full_pc ? SK_FULL_ROWS : SK_COMPACT_ROWS) - 1;
    if (rs->layout == SK_LAYOUT_FULL && rs->toolbar_focused) {
        if (p_b) { rs->toolbar_focused = false; rs->revision++; rs->sk.prev = *pad; return true; }
        if (p_up) { rs->toolbar_focused = false; rs->sk.row = max_row; rs->revision++; rs->sk.prev = *pad; return true; }
        if (p_left) { rs->toolbar_index = (rs->toolbar_index + SK_CMD_COUNT - 1) % SK_CMD_COUNT; rs->revision++; }
        if (p_right) { rs->toolbar_index = (rs->toolbar_index + 1) % SK_CMD_COUNT; rs->revision++; }
        if (p_a) { sk_remote_run_cmd(rs, rs->toolbar_index); }
        rs->sk.prev = *pad;
        return true;
    }
    if (rs->layout == SK_LAYOUT_FULL && !rs->toolbar_focused &&
        rs->sk.zone == SK_ZONE_KEYS && rs->sk.row == max_row && p_down) {
        rs->toolbar_focused = true;
        rs->revision++;
        rs->sk.prev = *pad;
        return true;
    }

    int prev_len = (int)strlen(rs->sk.text);
    int prev_cursor = rs->sk.cursor;
    int prev_row = rs->sk.row, prev_col = rs->sk.col;
    int prev_rrow = rs->sk.right_row, prev_rcol = rs->sk.right_col;
    int prev_sug = rs->sk.sug_index, prev_zone = (int)rs->sk.zone;
    int prev_page = (int)rs->sk.page, prev_shift = (int)rs->sk.shift;
    bool prev_dual = rs->sk.dual_stick;
    bool prev_ctrl = rs->sk.ctrl_on, prev_alt = rs->sk.alt_on, prev_win = rs->sk.win_on;
    enum sk_send_mode prev_mode = rs->send_mode;
    bool prev_full_pc = rs->sk.full_pc;
    enum sk_overlay_layout prev_layout = rs->layout;

    enum sk_result res = sk_step(&rs->sk, pad, now_ms);
    int new_len = (int)strlen(rs->sk.text);

    /* Forward interactive 5-row special keys, standalone Win, & modifier chords (Ctrl+C, Win+D, etc.) */
    bool handled_back_vk = false;
    if (rs->sk.pending_special == SK_KEY_WIN && !rs->sk.win_on) {
        sk_remote_send_chord(&rs->ops, SK_VK_LWIN, 0);
    } else if (rs->sk.pending_mods && rs->sk.pending_special >= 0x20 && rs->sk.pending_special < 0x7F) {
        short vk = 0; int extra = 0;
        if (sk_remote_char_to_vk((unsigned char)rs->sk.pending_special, &vk, &extra))
            sk_remote_send_chord(&rs->ops, vk, rs->sk.pending_mods | extra);
    } else if (rs->sk.pending_special == SK_KEY_TAB) {
        sk_remote_send_chord(&rs->ops, SK_VK_TAB, rs->sk.pending_mods);
    } else if (rs->sk.pending_special == SK_KEY_LEFT) {
        sk_remote_send_chord(&rs->ops, SK_VK_LEFT, rs->sk.pending_mods);
    } else if (rs->sk.pending_special == SK_KEY_RIGHT) {
        sk_remote_send_chord(&rs->ops, SK_VK_RIGHT, rs->sk.pending_mods);
    } else if (rs->sk.pending_special == SK_KEY_UP) {
        sk_remote_send_chord(&rs->ops, SK_VK_UP, rs->sk.pending_mods);
    } else if (rs->sk.pending_special == SK_KEY_DOWN) {
        sk_remote_send_chord(&rs->ops, SK_VK_DOWN, rs->sk.pending_mods);
    } else if (rs->sk.pending_special == SK_KEY_BACK &&
               (rs->send_mode == SK_SEND_DIRECT || rs->sk.pending_mods)) {
        sk_remote_send_chord(&rs->ops, SK_VK_BACK, rs->sk.pending_mods);
        handled_back_vk = true;
    } else if (rs->sk.pending_special == SK_KEY_ENTER && rs->send_mode == SK_SEND_DIRECT) {
        sk_remote_send_chord(&rs->ops, SK_VK_RETURN, rs->sk.pending_mods);
        rs->sk.text[0] = '\0';
        rs->sk.cursor = 0;
        sk_refresh_suggestions(&rs->sk);
        rs->revision++;
        return true;
    } else if (rs->sk.pending_special == SK_KEY_ESC && rs->send_mode == SK_SEND_DIRECT) {
        sk_remote_send_chord(&rs->ops, SK_VK_ESCAPE, rs->sk.pending_mods);
        rs->sk.cursor = (int)strlen(rs->sk.text);
        rs->revision++;
        return true;
    }

    /* Direct Typing mode: forward each new insertion or backspace immediately. */
    if (res == SK_RESULT_ACTIVE && rs->send_mode == SK_SEND_DIRECT) {
        if (new_len > prev_len && rs->sk.cursor > prev_cursor) {
            char delta[16];
            int dlen = rs->sk.cursor - prev_cursor;
            if (dlen > 0 && dlen < (int)sizeof(delta)) {
                memcpy(delta, rs->sk.text + prev_cursor, (size_t)dlen);
                delta[dlen] = '\0';
                sk_remote_send_text(rs, delta);
            }
        } else if (new_len < prev_len && !handled_back_vk) {
            sk_remote_send_chord(&rs->ops, SK_VK_BACK, 0);
        }
    }

    if (res == SK_RESULT_ACCEPTED) {
        if (rs->send_mode == SK_SEND_COMPOSE && rs->sk.text[0])
            sk_remote_send_text(rs, rs->sk.text);
        sk_remote_close(rs);
        return false;
    }
    if (res == SK_RESULT_CANCELLED) {
        sk_remote_close(rs);
        return false;
    }

    if (new_len != prev_len || rs->sk.cursor != prev_cursor ||
        rs->sk.row != prev_row || rs->sk.col != prev_col ||
        rs->sk.right_row != prev_rrow || rs->sk.right_col != prev_rcol ||
        rs->sk.sug_index != prev_sug || (int)rs->sk.zone != prev_zone ||
        (int)rs->sk.page != prev_page || (int)rs->sk.shift != prev_shift ||
        rs->sk.dual_stick != prev_dual || rs->sk.ctrl_on != prev_ctrl ||
        rs->sk.alt_on != prev_alt || rs->sk.win_on != prev_win ||
        rs->send_mode != prev_mode || rs->sk.full_pc != prev_full_pc ||
        rs->layout != prev_layout) {
        rs->revision++;
    }
    return true;
}
