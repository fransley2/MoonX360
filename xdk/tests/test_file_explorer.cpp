#define FE_HOST_BACKEND
#define FE_WITH_IMAGE_PREVIEW
#define STB_IMAGE_WRITE_IMPLEMENTATION
#ifndef NOGDI
#define NOGDI
#endif

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <deque>
#include <string>
#include <vector>

#include "../../third_party/stb/stb_image_write.h"
#include "../../src/config.h"
#include "../../src/file_explorer_app.h"
#include "../../src/ui/draw.h"

/* Host test stubs required by config.c / ui.c */
static std::string g_ini;
char *storage_read(const char *name, size_t *len)
{
    (void)name;
    if (g_ini.empty()) { if (len) *len = 0; return NULL; }
    char *p = (char *)malloc(g_ini.size() + 1);
    memcpy(p, g_ini.c_str(), g_ini.size() + 1);
    if (len) *len = g_ini.size();
    return p;
}
bool storage_write(const char *name, const void *data, size_t len)
{
    (void)name;
    g_ini.assign((const char *)data, len);
    return true;
}

static unsigned g_presents = 0;
void ui_present(void) { g_presents++; }

static std::deque<int> g_keys;
static std::deque<bool> g_confirms;
static std::deque<std::string> g_edits;
static std::vector<std::string> g_messages;
static int g_raw_buttons = 0;
static uint32_t g_now_ms = 1000;
static struct fe_settings g_saved_fe;
static int g_save_count = 0;

static int test_wait_button(void)
{
    if (g_keys.empty()) {
        fprintf(stderr, "g_keys EMPTY! msgs=%zu\n", g_messages.size());
        for (size_t i = 0; i < g_messages.size(); i++) fprintf(stderr, "  msg[%zu]=%s\n", i, g_messages[i].c_str());
    }
    assert(!g_keys.empty());
    int k = g_keys.front();
    g_keys.pop_front();
    return k;
}
static uint32_t test_now_ms(void) { g_now_ms += 16; return g_now_ms; }
static void test_sleep_ms(unsigned) {}
static void test_message(int icon, const char *title, const char *text)
{
    (void)icon;
    g_messages.push_back(std::string(title ? title : "") + ": " + (text ? text : ""));
}
static bool test_confirm(const char *title, const char *text, const char *confirm_label)
{
    (void)title; (void)text; (void)confirm_label;
    if (g_confirms.empty()) return true;
    bool v = g_confirms.front();
    g_confirms.pop_front();
    return v;
}
static bool test_edit_name(const char *title, const char *subtitle, char *buf, int cap)
{
    (void)title; (void)subtitle;
    if (g_edits.empty()) return false;
    std::string s = g_edits.front();
    g_edits.pop_front();
    snprintf(buf, (size_t)cap, "%s", s.c_str());
    return true;
}
static void test_save_settings(const struct fe_settings *s)
{
    g_saved_fe = *s;
    g_save_count++;
}
static int test_poll_raw_buttons(void) { return g_raw_buttons; }
static bool test_valid_xex_contract(const char *path)
{
    struct fe_xex_info info;
    return fe_fs_probe_xex(path, &info) == FE_OK && info.is_xex2;
}
static bool test_current_xex_contract(const char *path)
{
    return test_valid_xex_contract(path);
}
static bool test_valid_aurora_contract(const char *folder)
{
    char xex[FE_PATH_MAX];
    if (!fe_path_join(xex, sizeof(xex), folder, "Aurora.xex")) return false;
    return test_valid_xex_contract(xex);
}

static const struct fe_ui_ops g_ops = {
    test_wait_button, test_now_ms, test_sleep_ms, ui_present,
    test_message, test_confirm, test_edit_name, test_save_settings,
    test_poll_raw_buttons, test_valid_xex_contract, test_current_xex_contract, test_valid_aurora_contract
};

static void rm_tree(const std::string &dir)
{
    std::string pat = dir + "\\*";
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat.c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
            std::string child = dir + "\\" + fd.cFileName;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) rm_tree(child);
            else { SetFileAttributesA(child.c_str(), FILE_ATTRIBUTE_NORMAL); DeleteFileA(child.c_str()); }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryA(dir.c_str());
}

static void write_bytes(const std::string &path, const void *data, size_t len)
{
    FILE *f = fopen(path.c_str(), "wb");
    assert(f);
    if (len) assert(fwrite(data, 1, len, f) == len);
    fclose(f);
}

static bool cancel_after_first_chunk(const struct fe_op_progress *, void *user)
{
    int *calls = (int *)user;
    (*calls)++;
    return *calls < 2;
}

static void assert_render_ok(const char *title, const struct fe_model *m)
{
    fe_view_draw(title, m);
    int n = 0;
    const struct ui_draw_command *cmds = ui_draw_commands(&n);
    assert(cmds && n > 10 && n < UI_DRAW_CAPACITY);
    canvas_t *cv = ui_canvas();
    for (int i = 0; i < n; i++) {
        if (cmds[i].kind == UI_GLYPH) {
            assert(cmds[i].x >= -2.0f && cmds[i].x + cmds[i].w <= (float)cv->w + 2.0f);
            assert(cmds[i].y >= -2.0f && cmds[i].y + cmds[i].h <= (float)cv->h + 2.0f);
        }
    }
    ui_draw_software();
}

int main(void)
{
    assert(ui_init(1280, 720));
    ui_draw_enable(true);

    /* 1. Classification, formatting, filename & protected path tests. */
    {
        int icon = -1;
        char tbuf[FE_TYPE_MAX], sbuf[32], dbuf[32], nbuf[FE_NAME_MAX];
        fe_classify_entry("Games", true, &icon, tbuf, sizeof(tbuf));
        assert(icon == FE_ICON_FOLDER && !strcmp(tbuf, "Folder"));
        fe_classify_entry("default.xex", false, &icon, tbuf, sizeof(tbuf));
        assert(icon == FE_ICON_XEX);
        fe_classify_entry("cover.PNG", false, &icon, tbuf, sizeof(tbuf));
        assert(icon == FE_ICON_IMAGE);
        fe_classify_entry("notes.txt", false, &icon, tbuf, sizeof(tbuf));
        assert(icon == FE_ICON_TEXT);
        fe_classify_entry("moonlight.ini", false, &icon, tbuf, sizeof(tbuf));
        assert(icon == FE_ICON_CONFIG);
        fe_classify_entry("pack.7z", false, &icon, tbuf, sizeof(tbuf));
        assert(icon == FE_ICON_ARCHIVE);
        fe_classify_entry("bgm.mp3", false, &icon, tbuf, sizeof(tbuf));
        assert(icon == FE_ICON_AUDIO);
        fe_classify_entry("clip.mp4", false, &icon, tbuf, sizeof(tbuf));
        assert(icon == FE_ICON_VIDEO);
        fe_classify_entry("README_NO_EXT", false, &icon, tbuf, sizeof(tbuf));
        assert(icon == FE_ICON_UNKNOWN && !strcmp(tbuf, "File"));
        fe_classify_entry("weird.xyz", false, &icon, tbuf, sizeof(tbuf));
        assert(icon == FE_ICON_UNKNOWN && !strcmp(tbuf, "XYZ file"));

        fe_format_size(512, sbuf, sizeof(sbuf)); assert(!strcmp(sbuf, "512 B"));
        fe_format_size(2048, sbuf, sizeof(sbuf)); assert(!strcmp(sbuf, "2.0 KB"));
        fe_format_size(5ULL * 1024ULL * 1024ULL, sbuf, sizeof(sbuf)); assert(!strcmp(sbuf, "5.0 MB"));
        fe_format_size(2ULL * 1024ULL * 1024ULL * 1024ULL, sbuf, sizeof(sbuf)); assert(!strcmp(sbuf, "2.00 GB"));
        fe_format_date(0, dbuf, sizeof(dbuf)); assert(!strcmp(dbuf, "--"));
        fe_format_date(1700000000LL, dbuf, sizeof(dbuf)); assert(strlen(dbuf) == 16 && dbuf[4] == '-' && dbuf[13] == ':');

        struct fe_entry e = {};
        snprintf(e.name, sizeof(e.name), "demo.ini");
        fe_format_display_name(&e, true, nbuf, sizeof(nbuf)); assert(!strcmp(nbuf, "demo.ini"));
        fe_format_display_name(&e, false, nbuf, sizeof(nbuf)); assert(!strcmp(nbuf, "demo"));

        assert(!fe_valid_filename("") && !fe_valid_filename(".") && !fe_valid_filename("..") &&
               !fe_valid_filename("bad/name") && !fe_valid_filename("bad*name") && !fe_valid_filename("trail."));
        assert(fe_valid_filename("My Game (BR) [v1.2].xex"));
        assert(fe_path_is_protected("Hdd1:\\") && fe_path_is_protected("Flash:\\bootanim.xex") &&
               fe_path_is_protected("game:\\default.xex") && fe_path_is_protected("Hdd1:\\$SystemUpdate") &&
               fe_path_is_protected("Hdd1:\\Apps\\MoonX360\\client.pem") &&
               fe_path_is_protected("Hdd1:\\Apps\\MoonX360\\default.xex"));
        assert(!fe_path_is_protected("Hdd1:\\Apps\\MoonX360\\Shortcuts\\game.xex"));
    }

    /* 2. Build isolated filesystem sandbox outside workspace sync locks. */
    char tmp[FE_PATH_MAX];
    DWORD tlen = GetTempPathA(sizeof(tmp), tmp);
    assert(tlen > 0 && tlen < sizeof(tmp));
    char base_buf[FE_PATH_MAX];
    snprintf(base_buf, sizeof(base_buf), "%smoonx-fe-%lu", tmp, (unsigned long)GetCurrentProcessId());
    std::string base = base_buf;
    rm_tree(base);
    std::string drvA = base + "\\DriveA";
    std::string drvB = base + "\\DriveB";
    std::string emptyDir = drvA + "\\EmptyFolder";
    std::string bigDir = drvA + "\\ManyFiles";
    std::string appDir = drvA + "\\AuroraApp";
    std::string workDir = drvA + "\\Work";
    CreateDirectoryA(base.c_str(), NULL);
    CreateDirectoryA(drvA.c_str(), NULL);
    CreateDirectoryA(drvB.c_str(), NULL);
    CreateDirectoryA(emptyDir.c_str(), NULL);
    CreateDirectoryA(bigDir.c_str(), NULL);
    CreateDirectoryA(appDir.c_str(), NULL);
    CreateDirectoryA(workDir.c_str(), NULL);

    /* Create 1500 entries (proving the old 192 cap is completely gone). */
    for (int i = 0; i < 1500; i++) {
        char p[FE_PATH_MAX];
        snprintf(p, sizeof(p), "%s\\item_%04d.txt", bigDir.c_str(), i);
        write_bytes(p, "hello", 5);
    }

    /* Create XEX2 files (valid & invalid), hidden file, large file (> 2 chunks), text > 8 KB, binary, PNG. */
    unsigned char xex_valid[64] = {'X','E','X','2', 0,0,0,1, 0,0,1,0, 0,0,0,0, 0,0,0,64};
    unsigned char xex_bad[16] = {'M','Z',0,0};
    write_bytes(appDir + "\\default.xex", xex_valid, sizeof(xex_valid));
    write_bytes(appDir + "\\Aurora.xex", xex_valid, sizeof(xex_valid));
    write_bytes(workDir + "\\broken.xex", xex_bad, sizeof(xex_bad));

    std::vector<unsigned char> big_blob(200 * 1024, 0x5Au);
    write_bytes(workDir + "\\payload.bin", big_blob.data(), big_blob.size());
    std::string long_txt(10000, 'A');
    write_bytes(workDir + "\\long.log", long_txt.data(), long_txt.size());
    write_bytes(workDir + "\\alpha.ini", "width=1280\nheight=720\n", 22);
    write_bytes(workDir + "\\secret.txt", "hidden", 6);
    SetFileAttributesA((workDir + "\\secret.txt").c_str(), FILE_ATTRIBUTE_HIDDEN);

    {
        std::vector<unsigned char> rgb(640 * 480 * 3, 64);
        for (int y = 0; y < 480; y++)
            for (int x = 0; x < 640; x++) {
                size_t idx = ((size_t)y * 640 + (size_t)x) * 3;
                rgb[idx] = (unsigned char)((x * 255) / 640);
                rgb[idx + 1] = (unsigned char)((y * 255) / 480);
                rgb[idx + 2] = 200;
            }
        assert(stbi_write_png((workDir + "\\wall.png").c_str(), 640, 480, 3, rgb.data(), 640 * 3));
        write_bytes(workDir + "\\corrupt.png", "not-a-png", 9);
    }

    struct fe_root roots[3] = {};
    snprintf(roots[0].name, sizeof(roots[0].name), "DriveA");
    snprintf(roots[0].path, sizeof(roots[0].path), "%s", drvA.c_str());
    snprintf(roots[0].label, sizeof(roots[0].label), "Test Drive A");
    roots[0].icon_kind = FE_ICON_DRIVE_HDD; roots[0].total_bytes = 10000000; roots[0].free_bytes = 4000000; roots[0].accessible = true;
    snprintf(roots[1].name, sizeof(roots[1].name), "DriveB");
    snprintf(roots[1].path, sizeof(roots[1].path), "%s", drvB.c_str());
    snprintf(roots[1].label, sizeof(roots[1].label), "Test USB B");
    roots[1].icon_kind = FE_ICON_DRIVE_USB; roots[1].total_bytes = 5000000; roots[1].free_bytes = 2500000; roots[1].accessible = true;
    snprintf(roots[2].name, sizeof(roots[2].name), "MissingUsb");
    snprintf(roots[2].path, sizeof(roots[2].path), "%s\\NoSuchUsb", base.c_str());
    snprintf(roots[2].label, sizeof(roots[2].label), "Removed USB");
    roots[2].icon_kind = FE_ICON_DRIVE_USB; roots[2].accessible = false;
    fe_fs_set_host_roots(roots, 3);

    /* 3. List empty, 1500+ entries, hidden toggle, sort modes & history restore. */
    {
        struct fe_model m;
        fe_model_init(&m, FE_MODE_MANAGER, NULL);
        assert(fe_model_open_dir(&m, emptyDir.c_str(), NULL, false) && m.count == 0);
        assert(fe_model_open_dir(&m, bigDir.c_str(), NULL, false) && m.count == 1500 && !m.truncated);

        m.settings.show_hidden = false;
        assert(fe_model_open_dir(&m, workDir.c_str(), NULL, false));
        int count_no_hidden = m.count;
        assert(fe_model_find_by_name(&m, "secret.txt") == -1);

        m.settings.show_hidden = true;
        assert(fe_model_open_dir(&m, workDir.c_str(), NULL, false) && m.count == count_no_hidden + 1);
        assert(fe_model_find_by_name(&m, "secret.txt") >= 0);

        /* Sorting checks: Size Desc puts payload.bin (200 KB) first; Asc puts alpha.ini / secret.txt before payload.bin. */
        m.settings.sort_by = FE_SORT_SIZE; m.settings.sort_dir = FE_SORT_DESC;
        fe_model_sort(&m, NULL);
        assert(!strcmp(m.items[0].name, "payload.bin"));
        m.settings.sort_by = FE_SORT_NAME; m.settings.sort_dir = FE_SORT_ASC;
        fe_model_sort(&m, NULL);
        assert(!strcmp(m.items[0].name, "alpha.ini"));

        /* Navigation history + scroll/selection restore */
        assert(fe_model_load_roots(&m, NULL));
        assert(fe_model_open_dir(&m, drvA.c_str(), NULL, true));
        int many_idx = fe_model_find_by_name(&m, "ManyFiles");
        assert(many_idx >= 0);
        m.selected = many_idx;
        assert(fe_model_open_dir(&m, bigDir.c_str(), NULL, true));
        fe_model_page(&m, 1, 8, 1);
        assert(m.selected == 8 && m.scroll_top > 0);
        assert(fe_model_go_back(&m, 8, 1) && !m.in_roots);
        assert(m.selected == many_idx && !strcmp(m.items[m.selected].name, "ManyFiles"));
        assert(fe_model_go_back(&m, 8, 1) && m.in_roots);
        fe_model_free(&m);
    }

    /* 4. Previews (XEX probe passive, text bounded/binary, PNG downsample/corrupt rejection). */
    {
        struct fe_xex_info xi;
        assert(fe_fs_probe_xex((appDir + "\\default.xex").c_str(), &xi) == FE_OK && xi.is_xex2 && xi.module_flags == 1);
        assert(fe_fs_probe_xex((workDir + "\\broken.xex").c_str(), &xi) == FE_ERR_INVALID && !xi.is_xex2);

        char tbuf[FE_PREVIEW_TEXT_MAX + 1];
        size_t tlen = 0; bool trunc = false, bin = false;
        assert(fe_fs_read_text_preview((workDir + "\\long.log").c_str(), tbuf, sizeof(tbuf), &tlen, &trunc, &bin) == FE_OK);
        assert(tlen == FE_PREVIEW_TEXT_MAX && trunc && !bin);
        assert(fe_fs_read_text_preview((workDir + "\\broken.xex").c_str(), tbuf, sizeof(tbuf), &tlen, &trunc, &bin) == FE_OK || bin);

        uint32_t *px = NULL; int w = 0, h = 0, ow = 0, oh = 0;
        assert(fe_fs_decode_image_preview((workDir + "\\wall.png").c_str(), 320, 240, &px, &w, &h, &ow, &oh) == FE_OK);
        assert(px && ow == 640 && oh == 480 && w == 320 && h == 240);
        free(px);
        assert(fe_fs_decode_image_preview((workDir + "\\corrupt.png").c_str(), 320, 240, &px, &w, &h, &ow, &oh) != FE_OK && !px);
    }

    /* 5. Safe file ops: mkdir, rename, copy + mid-copy cancel + conflict, move, delete & non-empty folder guard. */
    {
        char outp[FE_PATH_MAX];
        assert(fe_fs_mkdir(drvB.c_str(), "SubTest", outp, sizeof(outp)) == FE_OK);
        assert(fe_fs_mkdir(drvB.c_str(), "SubTest", outp, sizeof(outp)) == FE_ERR_EXISTS);
        assert(fe_fs_rename(outp, "RenamedSub", outp, sizeof(outp)) == FE_OK);

        int calls = 0;
        std::string src = workDir + "\\payload.bin";
        std::string dst_cancel = std::string(outp) + "\\cancel.bin";
        assert(fe_fs_copy_file(src.c_str(), dst_cancel.c_str(), false, big_blob.size(), cancel_after_first_chunk, &calls) == FE_ERR_CANCELLED);
        assert(!fe_fs_exists(dst_cancel.c_str(), NULL)); /* partial target must be deleted */

        std::string dst_ok = std::string(outp) + "\\copied.bin";
        assert(fe_fs_copy_file(src.c_str(), dst_ok.c_str(), false, big_blob.size(), NULL, NULL) == FE_OK);
        assert(fe_fs_copy_file(src.c_str(), dst_ok.c_str(), false, big_blob.size(), NULL, NULL) == FE_ERR_EXISTS);
        assert(fe_fs_copy_file(src.c_str(), dst_ok.c_str(), true, big_blob.size(), NULL, NULL) == FE_OK);

        /* Deleting non-empty folder must fail safely with FE_ERR_DIR_NOT_EMPTY */
        assert(fe_fs_delete_item(outp) == FE_ERR_DIR_NOT_EMPTY);

        /* Move copied.bin -> moved.bin and verify delete works cleanly once empty */
        std::string dst_moved = drvB + "\\moved.bin";
        assert(fe_fs_move_file(dst_ok.c_str(), dst_moved.c_str(), false, big_blob.size(), NULL, NULL) == FE_OK);
        assert(!fe_fs_exists(dst_ok.c_str(), NULL) && fe_fs_exists(dst_moved.c_str(), NULL));
        assert(fe_fs_delete_item(outp) == FE_OK);
        assert(fe_fs_delete_item(dst_moved.c_str()) == FE_OK);
    }

    /* 6. End-to-end Controller flow (Manager + Copy/Paste + Rename + Delete + Missing Drive + Pickers + Settings). */
    {
        struct fe_settings s;
        fe_settings_defaults(&s);
        s.remember_last_dir = false;
        char picked[160] = "";

        /* Unavailable drive fails cleanly and returns to Drives Home */
        g_keys = { DOWN_FLAG, DOWN_FLAG, A_FLAG, B_FLAG };
        g_messages.clear();
        assert(!fe_app_run(&g_ops, &s, "File Explorer", picked, sizeof(picked), FE_MODE_MANAGER));
        assert(!g_messages.empty() && g_keys.empty());

        /* Picker: Image selection navigates DriveA -> Work -> wall.png */
        g_keys = {
            A_FLAG,                                  /* open DriveA */
            DOWN_FLAG, DOWN_FLAG, DOWN_FLAG, A_FLAG, /* AuroraApp(0)->EmptyFolder(1)->ManyFiles(2)->Work(3) */
            Y_FLAG, Y_FLAG, Y_FLAG,                  /* cycle Details -> Compact -> Icons -> Details */
            B_FLAG, A_FLAG,                          /* B restores selection on Work, re-enter Work */
            DOWN_FLAG, A_FLAG                        /* select second filtered image (wall.png after corrupt.png) */
        };
        picked[0] = 0;
        assert(fe_app_run(&g_ops, &s, "Choose wallpaper", picked, sizeof(picked), FE_MODE_PICK_IMAGE));
        assert(strstr(picked, "wall.png") != NULL && g_keys.empty());

        /* Picker: Aurora.xex explicit selection returns containing Aurora folder */
        picked[0] = 0;
        g_keys = { A_FLAG, A_FLAG, A_FLAG }; /* DriveA (0) -> AuroraApp (0) -> Aurora.xex (0) */
        assert(fe_app_run(&g_ops, &s, "Select Aurora.xex", picked, sizeof(picked), FE_MODE_PICK_AURORA_XEX));
        assert(strstr(picked, "AuroraApp") != NULL && g_keys.empty());

        /* Picker: Aurora Folder validates Aurora.xex on Start */
        picked[0] = 0;
        g_keys = { A_FLAG, A_FLAG, PLAY_FLAG }; /* DriveA (0) -> AuroraApp (0) -> Start */
        assert(fe_app_run(&g_ops, &s, "Select Aurora Folder", picked, sizeof(picked), FE_MODE_PICK_FOLDER));
        assert(strstr(picked, "AuroraApp") != NULL && g_keys.empty());

        /* Picker: MoonX360 XEX picks default.xex */
        picked[0] = 0;
        g_keys = { A_FLAG, A_FLAG, DOWN_FLAG, A_FLAG }; /* DriveA (0) -> AuroraApp (0) -> default.xex (1) */
        assert(fe_app_run(&g_ops, &s, "Select MoonX360 XEX", picked, sizeof(picked), FE_MODE_PICK_XEX));
        assert(strstr(picked, "default.xex") != NULL && g_keys.empty());

        /* Settings menu cycles view mode and saves */
        int prev_view = s.view_mode;
        g_keys = { RIGHT_FLAG, B_FLAG };
        fe_app_settings_menu(&g_ops, &s);
        assert(s.view_mode != prev_view && g_save_count > 0 && g_keys.empty());
        fe_settings_defaults(&s);

        /* Manager interactive flow: Context Copy -> RB switch to DriveB -> Start Paste -> Rename -> Delete -> B exit */
        std::string mgrSrc = drvA + "\\MgrSrc";
        CreateDirectoryA(mgrSrc.c_str(), NULL);
        write_bytes(mgrSrc + "\\note.txt", "abc", 3);
        char mgr_buf[160];
        snprintf(mgr_buf, sizeof(mgr_buf), "%s", mgrSrc.c_str());
        g_edits = { "renamed.txt" };
        g_confirms = { true };
        g_keys = {
            X_FLAG, DOWN_FLAG, DOWN_FLAG, DOWN_FLAG, A_FLAG, /* Preview(0), Props(1), Mark(2), Copy(3) */
            RB_FLAG,                                          /* switch directly from DriveA\MgrSrc to DriveB */
            PLAY_FLAG, DOWN_FLAG, A_FLAG,                     /* Start -> New Folder(0), Paste(1) */
            X_FLAG, DOWN_FLAG, DOWN_FLAG, DOWN_FLAG, DOWN_FLAG, DOWN_FLAG, DOWN_FLAG, A_FLAG, /* Rename(6) */
            X_FLAG, DOWN_FLAG, DOWN_FLAG, DOWN_FLAG, DOWN_FLAG, DOWN_FLAG, DOWN_FLAG, DOWN_FLAG, A_FLAG, /* Delete(7) */
            B_FLAG, B_FLAG, B_FLAG, B_FLAG                    /* DriveB->MgrSrc->DriveA->Roots->Exit */
        };
        assert(!fe_app_run(&g_ops, &s, "File Explorer", mgr_buf, sizeof(mgr_buf), FE_MODE_MANAGER));
        assert(g_keys.empty() && g_edits.empty() && g_confirms.empty());
        assert(!fe_fs_exists((drvB + "\\note.txt").c_str(), NULL));
        assert(!fe_fs_exists((drvB + "\\renamed.txt").c_str(), NULL));
        assert(fe_fs_exists((mgrSrc + "\\note.txt").c_str(), NULL));
    }

    /* 7. Software UI Rendering at both 1280x720 (16:9) and 960x720 (4:3) across all views & modals. */
    {
        struct fe_model m;
        fe_model_init(&m, FE_MODE_MANAGER, NULL);
        fe_model_load_roots(&m, NULL);
        const int cfgs[][3] = {{1280, 720, 0}, {960, 720, 1}};
        for (int r = 0; r < 2; r++) {
            assert(ui_init_layout(cfgs[r][0], cfgs[r][1], cfgs[r][2]));
            assert(fe_model_load_roots(&m, NULL));
            assert_render_ok("File Explorer", &m);
            assert(fe_model_open_dir(&m, workDir.c_str(), NULL, false));
            m.selected = fe_model_find_by_name(&m, "wall.png");
            fe_model_refresh_preview(&m, true);
            for (int v = 0; v < FE_VIEW_COUNT; v++) {
                m.settings.view_mode = v;
                fe_model_clamp_scroll(&m, 8, v == FE_VIEW_ICONS ? 3 : 1);
                assert_render_ok("File Explorer", &m);
            }
            fe_view_draw_preview_modal(&m, 0);
            fe_view_draw_properties_modal(&m, &m.items[m.selected], (workDir + "\\wall.png").c_str());
            struct fe_op_progress pr = {1, 2, 1024, 2048, 1024, 2048, false, "wall.png"};
            fe_view_draw_progress_modal("Copying files", "wall.png", &pr);
        }
        fe_model_free(&m);
    }

    rm_tree(base);
    puts("PASS: File Explorer 2.0 (1500+ items, classification, sorting, history restore, PNG/text/XEX previews, safe copy/cancel/conflict/move/delete/protected guards, pickers, settings, 16:9 & 4:3 rendering).");
    return 0;
}
