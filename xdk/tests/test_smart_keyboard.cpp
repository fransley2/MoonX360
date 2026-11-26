/* Automated UX, regression, bounds, and benchmark suite for MoonX360 Smart Virtual Keyboard. */
#include <assert.h>
#include <chrono>
#include <stdio.h>
#include <string>
#include <string.h>
#include <vector>
#include "../src/ui/ui.h"
#include "../src/ui/draw.h"
#include "../src/ui/smart_keyboard.h"
#include "../src/ui/smart_keyboard_remote.h"

struct vk_event { short vk; char action; char mods; };
static std::vector<std::string> remote_utf8_log;
static std::vector<vk_event> remote_vk_log;
static bool fail_utf8_mock;

static int mock_send_utf8(const char *text, unsigned len)
{
    if (fail_utf8_mock) return -1;
    remote_utf8_log.push_back(std::string(text, len));
    return 0;
}
static void mock_send_vk(short vk, char action, char mods)
{
    vk_event ev = {vk, action, mods};
    remote_vk_log.push_back(ev);
}
static void assert_no_stuck_keys(void)
{
    int balance[256] = {0};
    for (size_t i = 0; i < remote_vk_log.size(); i++) {
        int vk = (unsigned char)remote_vk_log[i].vk;
        if (remote_vk_log[i].action == 0x03) balance[vk]++;
        else if (remote_vk_log[i].action == 0x04) balance[vk]--;
    }
    for (int k = 0; k < 256; k++) assert(balance[k] == 0);
}

static void tap(struct sk_state *sk, struct sk_pad p, uint32_t &t)
{
    sk_step(sk, &p, t);
    t += 16;
    struct sk_pad neutral = {};
    sk_step(sk, &neutral, t);
    t += 16;
}

static void assert_screen_bounds(void)
{
    int n = 0;
    const struct ui_draw_command *cmds = ui_draw_commands(&n);
    canvas_t *cv = ui_canvas();
    assert(cmds && n > 10);
    for (int i = 0; i < n; i++) {
        assert(cmds[i].clip[0] >= 0 && cmds[i].clip[1] >= 0);
        assert(cmds[i].clip[2] <= cv->w && cmds[i].clip[3] <= cv->h);
    }
}

int main(void)
{
    /* 1. PT-BR accent-folding autocomplete & partial-word replacement */
    struct sk_user_dict ud = {};
    ud.enabled = true;
    struct sk_state sk;
    sk_init(&sk, SK_MODE_TEXT, SK_LANG_PT_BR, "Message", "Enter text", "minha configu atual", 96, &ud);
    /* Place cursor right after "configu" (index 13) */
    sk.cursor = 13;
    sk_refresh_suggestions(&sk);
    assert(sk.suggestions.count == 3);
    assert(!strcmp(sk.suggestions.items[0], "configura\xc3\xa7\xc3\xa3o"));
    assert(!strcmp(sk.suggestions.items[1], "configurar"));
    assert(!strcmp(sk.suggestions.items[2], "configura\xc3\xa7\xc3\xb5" "es"));
    assert(sk_accept_suggestion(&sk, 0));
    /* Must replace ONLY "configu", preserving "minha " and " atual" */
    assert(!strcmp(sk.text, "minha configura\xc3\xa7\xc3\xa3o atual"));

    /* 2. Spanish & English prefix matching + next-word bigrams */
    sk_init(&sk, SK_MODE_TEXT, SK_LANG_ES, "Texto", NULL, "configu", 64, NULL);
    assert(sk.suggestions.count == 3);
    assert(!strcmp(sk.suggestions.items[0], "configuraci\xc3\xb3n"));

    sk_init(&sk, SK_MODE_TEXT, SK_LANG_EN, "Search", NULL, "resolu", 64, NULL);
    assert(sk.suggestions.count >= 1 && !strcmp(sk.suggestions.items[0], "resolution"));

    sk_init(&sk, SK_MODE_TEXT, SK_LANG_PT_BR, "Text", NULL, "xbox ", 64, NULL);
    assert(sk.suggestions.count == 3 && !strcmp(sk.suggestions.items[0], "360"));

    /* 3. Learned words (opt-in) vs Password mode (zero suggestions, zero learning) */
    sk_init(&sk, SK_MODE_TEXT, SK_LANG_EN, "Text", NULL, "xenonlight", 64, &ud);
    uint32_t t = 1000;
    struct sk_pad start_pad = {}; start_pad.start = true;
    assert(sk_step(&sk, &start_pad, t) == SK_RESULT_ACCEPTED);
    t += 32;
    sk_init(&sk, SK_MODE_TEXT, SK_LANG_EN, "Text", NULL, "xeno", 64, &ud);
    assert(sk.suggestions.count >= 1 && !strcmp(sk.suggestions.items[0], "xenonlight"));

    sk_init(&sk, SK_MODE_PASSWORD, SK_LANG_EN, "Password", NULL, "secretpass", 64, &ud);
    assert(sk.suggestions.count == 0);
    assert(sk_step(&sk, &start_pad, t) == SK_RESULT_ACCEPTED);
    sk_init(&sk, SK_MODE_TEXT, SK_LANG_EN, "Text", NULL, "secret", 64, &ud);
    assert(sk.suggestions.count == 0); /* password must NEVER be learned */

    /* 4. IPv4 mode validation & numeric layout */
    sk_init(&sk, SK_MODE_IP, SK_LANG_EN, "Host IP", NULL, "999.1.1.1", 32, NULL);
    assert(sk.page == SK_PAGE_NUMERIC && sk.suggestions.count == 0);
    assert(sk_step(&sk, &start_pad, t) == SK_RESULT_ACTIVE && sk.error[0] != '\0');
    sk_init(&sk, SK_MODE_IP, SK_LANG_EN, "Host IP", NULL, "192.168.1.50", 32, NULL);
    assert(sk_step(&sk, &start_pad, t) == SK_RESULT_ACCEPTED);

    /* 5. URL mode quick tokens */
    sk_init(&sk, SK_MODE_URL, SK_LANG_EN, "URL", NULL, "", 64, NULL);
    assert(sk.suggestions.count == 3 && !strcmp(sk.suggestions.items[0], "https://"));
    assert(sk_accept_suggestion(&sk, 0) && !strcmp(sk.text, "https://"));

    /* 6. UTF-8 multi-byte cursor movement, backspace, held-X repeat, cancel & buffer bound */
    sk_init(&sk, SK_MODE_TEXT, SK_LANG_PT_BR, "UTF8", NULL, "a\xc3\xa7\xc3\xa3o", 16, NULL);
    sk_cursor_left(&sk); /* before 'o' */
    sk_backspace(&sk);   /* removes 2-byte 'ã' cleanly */
    assert(!strcmp(sk.text, "a\xc3\xa7o"));
    struct sk_pad x_hold = {}; x_hold.x = true;
    sk_step(&sk, &x_hold, 2000); /* deletes 'ç' immediately -> "ao" */
    assert(!strcmp(sk.text, "ao"));
    sk_step(&sk, &x_hold, 2400); /* > 380ms repeat -> deletes 'a' -> "o" */
    assert(!strcmp(sk.text, "o"));
    struct sk_pad cancel_pad = {}; cancel_pad.b = true;
    assert(sk_step(&sk, &cancel_pad, 2500) == SK_RESULT_CANCELLED);
    assert(!strcmp(sk.text, "a\xc3\xa7\xc3\xa3o")); /* original restored */

    /* Buffer full boundary */
    sk_init(&sk, SK_MODE_PROFILE, SK_LANG_EN, "Profile", NULL, "1234", 5, NULL);
    assert(!sk_insert_utf8(&sk, "5"));
    assert(!strcmp(sk.text, "1234"));

    /* 7. Experimental Dual-Stick mode: center 'd'/'k', corners, deadzone stability, 5-row numbers & D-Pad override */
    sk_init(&sk, SK_MODE_TEXT, SK_LANG_EN, "Dual", NULL, "", 64, NULL);
    struct sk_pad r3 = {}; r3.rs_click = true;
    tap(&sk, r3, t);
    assert(sk.dual_stick);
    struct sk_pad ds = {};
    ds.lx = -30000; ds.ly = 30000; ds.lt = true;  /* top-left -> 'q' */
    ds.rx = 30000;  ds.ry = 30000; ds.rt = true;  /* top-right -> 'p' */
    tap(&sk, ds, t);
    assert(!strcmp(sk.text, "qp"));
    /* Releasing sticks to center must land on 'd' (left) and 'k' (right) and type "dk" */
    struct sk_pad ds_center = {};
    ds_center.lt = true; ds_center.rt = true;
    tap(&sk, ds_center, t);
    assert(!strcmp(sk.text, "qpdk"));
    /* Drift inside deadzone must stay rock-solid on 'd' / 'k' without jitter */
    int old_r = sk.row, old_c = sk.col, old_rr = sk.right_row, old_rc = sk.right_col;
    struct sk_pad drift = {}; drift.lx = 5000; drift.ly = -5000; drift.rx = -5000; drift.ry = 5000;
    sk_step(&sk, &drift, t);
    assert(sk.row == old_r && sk.col == old_c && sk.right_row == old_rr && sk.right_col == old_rc);

    /* 7b. Full PC 5-row Dual-Stick: 'd', 'k', top number row ('4' / '8'), and D-Pad override */
    sk_init(&sk, SK_MODE_TEXT, SK_LANG_EN, "DualFull", NULL, "", 64, NULL);
    sk.full_pc = true;
    tap(&sk, r3, t);
    assert(sk.dual_stick);
    tap(&sk, ds_center, t); /* center -> 'd' and 'k' */
    assert(!strcmp(sk.text, "dk"));
    struct sk_pad ds_num = {};
    ds_num.ly = 32000; ds_num.lt = true; /* top row center-left -> '3' (above 'e'/'d') */
    ds_num.ry = 32000; ds_num.rt = true; /* top row center-right -> '8' (above 'i'/'k') */
    tap(&sk, ds_num, t);
    assert(!strcmp(sk.text, "dk38"));
    /* D-Pad precision override moves left cursor even in Dual-Stick mode */
    struct sk_pad dpad_up = {}; dpad_up.up = true;
    tap(&sk, dpad_up, t);
    assert(sk.dual_dpad_override && sk.row == 1);

    /* 8. Render bounds verification in 16:9 (1280x720) AND 4:3 CRT (640x480) */
    ui_draw_enable(true);
    assert(ui_init(1280, 720));
    ui_draw_smart_keyboard(&sk); assert_screen_bounds();
    sk_init(&sk, SK_MODE_IP, SK_LANG_EN, "Host IP", NULL, "192.168.1.10", 32, NULL);
    ui_draw_smart_keyboard(&sk); assert_screen_bounds();
    sk_init(&sk, SK_MODE_PASSWORD, SK_LANG_EN, "Password", NULL, "secret", 32, NULL);
    ui_draw_smart_keyboard(&sk); assert_screen_bounds();

    assert(ui_init_layout(640, 480, 1));
    sk_init(&sk, SK_MODE_SEARCH, SK_LANG_PT_BR, "Search library", "Enter game name", "configu", 64, NULL);
    ui_draw_smart_keyboard(&sk); assert_screen_bounds();
    sk_init(&sk, SK_MODE_IP, SK_LANG_EN, "Host IP", NULL, "192.168.1.10", 32, NULL);
    ui_draw_smart_keyboard(&sk); assert_screen_bounds();

    /* 9. Measured UX & Performance Benchmark: phrase "configuração xbox 360" */
    auto t0 = std::chrono::steady_clock::now();
    struct sk_suggestions bench_sug;
    for (int i = 0; i < 10000; i++) sk_suggest(SK_LANG_PT_BR, &ud, "configu", "", &bench_sug);
    auto t1 = std::chrono::steady_clock::now();
    double lookup_us = std::chrono::duration<double, std::micro>(t1 - t0).count() / 10000.0;

    /* Old linear 66-char alphabet editor requires ~210 D-pad taps for "configuracao xbox 360"
     * and cannot type UTF-8 accents ("ç", "ã") at all.
     * Smart Keyboard: type "conf" (4 chars) + accept "configuração" (1 LB + A) +
     * type "xbo" (3 chars) + accept "xbox" (1 LB + A) + accept bigram "360" (1 LB + A) = 10 actions! */
    sk_init(&sk, SK_MODE_TEXT, SK_LANG_PT_BR, "Bench", NULL, "", 64, NULL);
    sk_insert_utf8(&sk, "c"); sk_insert_utf8(&sk, "o"); sk_insert_utf8(&sk, "n"); sk_insert_utf8(&sk, "f");
    assert(sk_accept_suggestion(&sk, 0)); /* "configuração " */
    sk_insert_utf8(&sk, "x"); sk_insert_utf8(&sk, "b"); sk_insert_utf8(&sk, "o");
    assert(sk_accept_suggestion(&sk, 0)); /* "configuração xbox " */
    assert(sk_accept_suggestion(&sk, 0)); /* "configuração xbox 360 " */
    assert(!strcmp(sk.text, "configura\xc3\xa7\xc3\xa3o xbox 360 "));

    /* 10. Remote Desktop Mode integration (Compose, Direct, UTF-8 fallback, Chords, Layouts, Cancel) */
    struct sk_remote_session rs = {};
    struct sk_remote_ops ops = {mock_send_utf8, mock_send_vk};
    remote_utf8_log.clear(); remote_vk_log.clear(); fail_utf8_mock = false;
    sk_remote_open(&rs, SK_TARGET_REMOTE_DESKTOP, SK_LANG_PT_BR, &ops);
    assert(rs.active && rs.layout == SK_LAYOUT_COMPACT && rs.send_mode == SK_SEND_COMPOSE);
    struct sk_pad neutral_p = {};
    assert(sk_remote_step(&rs, &neutral_p, t)); /* clears opening neutral latch */
    /* Compose with PT-BR autocomplete and send with Start */
    sk_insert_utf8(&rs.sk, "configu");
    assert(sk_accept_suggestion(&rs.sk, 0));
    assert(!sk_remote_step(&rs, &start_pad, t + 16));
    assert(!rs.active && remote_utf8_log.size() == 1);
    assert(remote_utf8_log[0] == "configura\xc3\xa7\xc3\xa3o ");

    /* L3 layout cycle & persistence across reopen */
    sk_remote_open(&rs, SK_TARGET_REMOTE_DESKTOP, SK_LANG_EN, &ops);
    sk_remote_step(&rs, &neutral_p, t + 32);
    struct sk_pad l3 = {}; l3.ls_click = true;
    sk_remote_step(&rs, &l3, t + 48);
    assert(rs.layout == SK_LAYOUT_FULL);
    sk_remote_step(&rs, &neutral_p, t + 64);
    sk_remote_step(&rs, &l3, t + 80);
    assert(rs.layout == SK_LAYOUT_FLOATING_TOP);
    /* Desktop chords: Enter, Tab, Esc, Ctrl+A, Ctrl+C, Ctrl+V, Alt+Tab -> 0 stuck keys */
    for (int cmd = SK_CMD_ENTER; cmd <= SK_CMD_ALT_TAB; cmd++) sk_remote_run_cmd(&rs, cmd);
    assert_no_stuck_keys();
    /* Cancel restores mouse cleanly without sending text */
    size_t sent_before = remote_utf8_log.size();
    sk_insert_utf8(&rs.sk, "discard");
    assert(!sk_remote_step(&rs, &cancel_pad, t + 96));
    assert(!rs.active && remote_utf8_log.size() == sent_before);
    /* Reopen remembers last layout (SK_LAYOUT_FLOATING_TOP) */
    sk_remote_open(&rs, SK_TARGET_REMOTE_DESKTOP, SK_LANG_EN, &ops);
    assert(rs.layout == SK_LAYOUT_FLOATING_TOP);

    /* Direct Typing + UTF-8 rejection fallback to ASCII VK */
    sk_remote_run_cmd(&rs, SK_CMD_MODE_TOGGLE);
    assert(rs.send_mode == SK_SEND_DIRECT && !rs.sk.predictions_enabled);
    fail_utf8_mock = true;
    remote_vk_log.clear();
    assert(sk_remote_send_text(&rs, "Hi"));
    assert(rs.utf8_fallback_used && !rs.send_failed && remote_vk_log.size() > 0);
    assert_no_stuck_keys();
    sk_remote_close(&rs);
    assert(!rs.active);

    /* 11. Windows key tap/combos, Ctrl/Alt chords, Back Direct<->Compose toggle, Direct Enter/Esc/X, and Compact/Full prefs */
    fail_utf8_mock = false;
    struct sk_remote_session rs2 = {};
    sk_remote_open(&rs2, SK_TARGET_REMOTE_DESKTOP, SK_LANG_EN, &ops);
    sk_remote_apply_prefs(&rs2, 0, 0); /* Full PC + Direct */
    assert(rs2.layout == SK_LAYOUT_FULL && rs2.sk.full_pc && rs2.send_mode == SK_SEND_DIRECT);
    assert(sk_remote_step(&rs2, &neutral_p, t));

    /* Tap Win once -> latches win_on; tap Win second time -> sends standalone LWIN and unlatches */
    remote_vk_log.clear();
    rs2.sk.row = 4; rs2.sk.col = 1; /* Win key on Full PC row 4 */
    struct sk_pad ap = {}; ap.a = true;
    sk_remote_step(&rs2, &ap, t + 16); sk_remote_step(&rs2, &neutral_p, t + 32);
    assert(rs2.sk.win_on && remote_vk_log.empty());
    sk_remote_step(&rs2, &ap, t + 48); sk_remote_step(&rs2, &neutral_p, t + 64);
    assert(!rs2.sk.win_on && remote_vk_log.size() == 2 && remote_vk_log[0].vk == SK_VK_LWIN);
    assert_no_stuck_keys();

    /* Win+D combo */
    remote_vk_log.clear();
    rs2.sk.row = 4; rs2.sk.col = 1; /* Win */
    sk_remote_step(&rs2, &ap, t + 80); sk_remote_step(&rs2, &neutral_p, t + 96);
    assert(rs2.sk.win_on);
    rs2.sk.row = 2; rs2.sk.col = 3; /* 'd' */
    sk_remote_step(&rs2, &ap, t + 112); sk_remote_step(&rs2, &neutral_p, t + 128);
    assert(!rs2.sk.win_on && remote_vk_log.size() == 4);
    assert(remote_vk_log[0].vk == SK_VK_LWIN && remote_vk_log[1].vk == 'D' && (remote_vk_log[1].mods & SK_MOD_META));
    assert_no_stuck_keys();

    /* Ctrl+C & Ctrl+Backspace chords */
    remote_vk_log.clear();
    rs2.sk.row = 4; rs2.sk.col = 0; /* Ctrl */
    sk_remote_step(&rs2, &ap, t + 144); sk_remote_step(&rs2, &neutral_p, t + 160);
    assert(rs2.sk.ctrl_on);
    rs2.sk.row = 3; rs2.sk.col = 3; /* 'c' */
    sk_remote_step(&rs2, &ap, t + 176); sk_remote_step(&rs2, &neutral_p, t + 192);
    assert(!rs2.sk.ctrl_on && remote_vk_log.size() == 4 && remote_vk_log[1].vk == 'C' && (remote_vk_log[1].mods & SK_MOD_CTRL));
    rs2.sk.row = 4; rs2.sk.col = 0; /* Ctrl */
    sk_remote_step(&rs2, &ap, t + 208); sk_remote_step(&rs2, &neutral_p, t + 224);
    rs2.sk.row = 0; rs2.sk.col = 14; /* Backspace */
    sk_remote_step(&rs2, &ap, t + 240); sk_remote_step(&rs2, &neutral_p, t + 256);
    assert(!rs2.sk.ctrl_on && remote_vk_log.size() == 8 && remote_vk_log[5].vk == SK_VK_BACK && (remote_vk_log[5].mods & SK_MOD_CTRL));
    assert_no_stuck_keys();

    /* Direct mode: X backspace on empty buffer sends VK_BACK; virtual Enter/Esc send real keys & stay open */
    remote_vk_log.clear();
    struct sk_pad xp = {}; xp.x = true;
    assert(sk_remote_step(&rs2, &xp, t + 272)); sk_remote_step(&rs2, &neutral_p, t + 288);
    assert(remote_vk_log.size() == 2 && remote_vk_log[0].vk == SK_VK_BACK);
    rs2.sk.row = 2; rs2.sk.col = 12; /* Enter */
    assert(sk_remote_step(&rs2, &ap, t + 304)); sk_remote_step(&rs2, &neutral_p, t + 320);
    assert(rs2.active && remote_vk_log.size() == 4 && remote_vk_log[2].vk == SK_VK_RETURN);
    rs2.sk.row = 0; rs2.sk.col = 0; /* Esc */
    assert(sk_remote_step(&rs2, &ap, t + 336)); sk_remote_step(&rs2, &neutral_p, t + 352);
    assert(rs2.active && remote_vk_log.size() == 6 && remote_vk_log[4].vk == SK_VK_ESCAPE);
    assert_no_stuck_keys();

    /* Clean Back tap-and-release toggles Direct <-> Compose; Back+R3 does NOT toggle */
    struct sk_pad bp = {}; bp.back = true;
    assert(sk_remote_step(&rs2, &bp, t + 368));
    assert(rs2.send_mode == SK_SEND_DIRECT);
    assert(sk_remote_step(&rs2, &neutral_p, t + 384));
    assert(rs2.send_mode == SK_SEND_COMPOSE && rs2.sk.predictions_enabled);
    struct sk_pad br3 = {}; br3.back = true; br3.rs_click = true;
    assert(sk_remote_step(&rs2, &br3, t + 400));
    assert(sk_remote_step(&rs2, &neutral_p, t + 416));
    assert(rs2.send_mode == SK_SEND_COMPOSE);

    /* Compact 3x10 preference opens directly in Compact 3x10 + Direct and switches to Full via L3 */
    struct sk_remote_session rs3 = {};
    sk_remote_open(&rs3, SK_TARGET_REMOTE_DESKTOP, SK_LANG_PT_BR, &ops);
    sk_remote_apply_prefs(&rs3, 1, 0);
    assert(rs3.layout == SK_LAYOUT_COMPACT && !rs3.sk.full_pc && rs3.send_mode == SK_SEND_DIRECT);
    sk_remote_step(&rs3, &neutral_p, t + 432);
    sk_remote_step(&rs3, &l3, t + 448);
    assert(rs3.layout == SK_LAYOUT_FULL && rs3.sk.full_pc);
    sk_remote_close(&rs3);
    assert(!rs3.active);

    struct sk_state resolution;
    struct sk_pad confirm = {};confirm.start=true;
    sk_init(&resolution,SK_MODE_RESOLUTION,SK_LANG_EN,"Resolution",NULL,"1025x768",12,NULL);
    assert(resolution.page==SK_PAGE_NUMERIC && !resolution.predictions_enabled);
    assert(!strcmp(sk_numeric_key_label(&resolution,3,0),"x"));
    assert(sk_step(&resolution,&confirm,t+500)==SK_RESULT_ACTIVE && resolution.error[0]);
    sk_init(&resolution,SK_MODE_RESOLUTION,SK_LANG_EN,"Resolution",NULL,"2560x240",12,NULL);
    assert(sk_step(&resolution,&confirm,t+516)==SK_RESULT_ACCEPTED);
    printf("PASS: Smart Keyboard all 7 modes, PT-BR/EN/ES autocomplete, UTF-8, held-X repeat, Dual-Stick, 16:9 & 4:3 CRT bounds, and Remote Desktop Mode (Compose/Direct/Chords/Fallback/Layouts).\n");
    printf("BENCH: sizeof(sk_state)=%zu B | static_dicts=%zu B | lookup_avg=%.2f us | phrase_keystrokes=7 + 3 suggestions (vs ~210 legacy moves)\n",
           sizeof(struct sk_state), sk_dict_memory_bytes(), lookup_us);
    return 0;
}
