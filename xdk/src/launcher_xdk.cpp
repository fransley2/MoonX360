/*
 * MoonX360 permanent Xbox 360 XEX launcher (default.xex).
 * Independent of FFmpeg, Opus, Moonlight, and D3D9: prepares verified updates,
 * performs automatic/manual rollback on unconfirmed boots, preserves Aurora
 * Direct Launch data, and launches game:\moonx360.xex.
 */
#include <xtl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/version_info.h"
#include "../../src/update/update_public_key.h"
#include "../../src/update/update_crypto.h"
#include "../../src/update/update_journal.h"

static FILE *g_log;
static DWORD g_start_ms;

static void launcher_log_line(const char *msg, void *ctx)
{
    char line[384];
    (void)ctx;
    if (!msg)
        return;
    _snprintf(line, sizeof(line) - 1, "[%lu ms] %s\n",
              (unsigned long)(GetTickCount() - g_start_ms), msg);
    line[sizeof(line) - 1] = '\0';
    OutputDebugStringA(line);
    if (g_log) {
        fputs(line, g_log);
        fflush(g_log);
    }
}

static void launcher_log_open(void)
{
    CreateDirectoryA("game:\\updates", NULL);
    g_log = fopen("game:\\updates\\launcher.log", "ab");
    g_start_ms = GetTickCount();
    launcher_log_line("========================================", NULL);
    {
        char banner[160];
        _snprintf(banner, sizeof(banner) - 1,
                  "MoonX360 launcher v%d | built %s %s",
                  MOONX_LAUNCHER_VERSION, __DATE__, __TIME__);
        banner[sizeof(banner) - 1] = '\0';
        launcher_log_line(banner, NULL);
    }
}

static void launcher_log_close(void)
{
    if (g_log) {
        fflush(g_log);
        fclose(g_log);
        g_log = NULL;
    }
}

static bool recovery_combo_held(void)
{
    const WORD mask = XINPUT_GAMEPAD_LEFT_SHOULDER |
                      XINPUT_GAMEPAD_RIGHT_SHOULDER |
                      XINPUT_GAMEPAD_BACK;
    for (DWORD i = 0; i < 4; i++) {
        XINPUT_STATE st;
        if (XInputGetState(i, &st) == ERROR_SUCCESS &&
            (st.Gamepad.wButtons & mask) == mask) {
            return true;
        }
    }
    return false;
}

static void show_fatal_and_exit(const char *detail)
{
    char msg[448];
    WCHAR wide[448];
    const WCHAR *buttons[] = { L"Return to Dashboard" };
    MESSAGEBOX_RESULT result;
    XOVERLAPPED pending;

    _snprintf(msg, sizeof(msg) - 1,
              "Could not start MoonX360 (%s).\n\n"
              "Manual recovery:\n"
              "1. Hold LB + RB + Back on startup to restore from embedded/previous backup.\n"
              "2. Or recopy default.xex (your moonlight\\ data is safe).\n"
              "Log: game:\\updates\\launcher.log",
              detail ? detail : "unknown error");
    msg[sizeof(msg) - 1] = '\0';

    if (!MultiByteToWideChar(CP_UTF8, 0, msg, -1, wide, 448)) {
        static const WCHAR fallback[] = L"Could not start MoonX360. Reinstall package or restore moonx360.previous.xex.";
        memcpy(wide, fallback, sizeof(fallback));
    }

    ZeroMemory(&result, sizeof(result));
    ZeroMemory(&pending, sizeof(pending));
    DWORD rc = XShowMessageBoxUI(0, L"MoonX360 Launcher Error", wide, 1, buttons, 0, 0, &result, &pending);
    if (rc == ERROR_IO_PENDING) {
        DWORD started = GetTickCount();
        while (!XHasOverlappedIoCompleted(&pending) && GetTickCount() - started < 15000)
            Sleep(16);
    }
    XLaunchNewImage(XLAUNCH_KEYWORD_DEFAULT_APP, 0);
}

#ifdef GetModuleHandleA
#undef GetModuleHandleA
#endif

static void build_embedded_payload(struct update_embedded_payload *emb)
{
    memset(emb, 0, sizeof(*emb));
#if defined(MOONX_EMBEDDED_PAYLOAD_SIZE) && defined(MOONX_EMBEDDED_PAYLOAD_SHA256)
    HMODULE hmod = GetModuleHandleA(NULL);
    PVOID sec_data = NULL;
    ULONG sec_size = 0;
    BOOL ok = (hmod != NULL) ? XGetModuleSection(hmod, "MOONXAPP", &sec_data, &sec_size) : FALSE;
    if (ok && sec_data && sec_size > 0) {
        emb->build_id = MOONX_BUILD_ID;
        emb->version = MOONX_VERSION;
        emb->size = (uint64_t)MOONX_EMBEDDED_PAYLOAD_SIZE;
        emb->sha256_hex = MOONX_EMBEDDED_PAYLOAD_SHA256;
        emb->data = (const uint8_t *)sec_data;
        emb->data_len = (size_t)sec_size;
    }
#endif
}

struct launcher_kstr { USHORT Length, MaximumLength; CHAR *Buffer; };
struct launcher_obj_attr { HANDLE RootDirectory; struct launcher_kstr *ObjectName; ULONG Attributes; };
extern "C" LONG NtOpenSymbolicLinkObject(HANDLE *Handle, struct launcher_obj_attr *ObjAttr);
extern "C" LONG NtQuerySymbolicLinkObject(HANDLE Handle, struct launcher_kstr *LinkTarget, ULONG *ReturnedLength);
extern "C" LONG NtClose(HANDLE Handle);

static void record_install_root_path(void)
{
    char link_buf[] = "\\??\\game:";
    struct launcher_kstr link = { (USHORT)strlen(link_buf), (USHORT)(strlen(link_buf) + 1), link_buf };
    struct launcher_obj_attr oa = { NULL, &link, 0x40u };
    HANDLE h = NULL;
    char dev_buf[320];
    if (NtOpenSymbolicLinkObject(&h, &oa) != 0 || !h)
        return;

    struct launcher_kstr target = { 0, (USHORT)(sizeof(dev_buf) - 1), dev_buf };
    ULONG ret_len = 0;
    LONG st = NtQuerySymbolicLinkObject(h, &target, &ret_len);
    NtClose(h);
    if (st != 0 || target.Length == 0 || target.Length >= sizeof(dev_buf))
        return;
    dev_buf[target.Length] = '\0';
    size_t dlen = strlen(dev_buf);
    while (dlen > 0 && dev_buf[dlen - 1] == '\\')
        dev_buf[--dlen] = '\0';
    if (_strnicmp(dev_buf, "\\Device\\", 8) != 0)
        return;

    char existing[320];
    existing[0] = '\0';
    FILE *rf = fopen("game:\\" UPD_REL_ROOT_PATH, "rb");
    if (rf) {
        size_t nr = fread(existing, 1, sizeof(existing) - 1, rf);
        existing[nr] = '\0';
        fclose(rf);
        if (strcmp(existing, dev_buf) == 0)
            return;
    }
    CreateDirectoryA("game:\\updates", NULL);
    CreateDirectoryA("game:\\" UPD_REL_RUNTIME_DIR, NULL);
    FILE *wf = fopen("game:\\" UPD_REL_ROOT_PATH, "wb");
    if (wf) {
        fwrite(dev_buf, 1, strlen(dev_buf), wf);
        fflush(wf);
        fclose(wf);
    }
}

void __cdecl main()
{
    XSetThreadProcessor(GetCurrentThread(), 0);

    BYTE launch_buf[4096];
    DWORD launch_bytes = 0;
    bool has_launch_data = false;
    if (XGetLaunchDataSize(&launch_bytes) == ERROR_SUCCESS &&
        launch_bytes > 0 && launch_bytes <= sizeof(launch_buf)) {
        if (XGetLaunchData(launch_buf, launch_bytes) == ERROR_SUCCESS)
            has_launch_data = true;
    }

    launcher_log_open();
    if (has_launch_data) {
        char dmsg[96];
        _snprintf(dmsg, sizeof(dmsg) - 1, "preserved launch data (%lu bytes)", (unsigned long)launch_bytes);
        dmsg[sizeof(dmsg) - 1] = '\0';
        launcher_log_line(dmsg, NULL);
    }

    struct update_embedded_payload emb;
    build_embedded_payload(&emb);

    bool force_embedded_recover = false;
    if (recovery_combo_held()) {
        if (update_journal_has_previous("game:\\")) {
            FILE *rf = fopen("game:\\updates\\recover.flag", "wb");
            if (rf) {
                fputs("manual-combo\n", rf);
                fclose(rf);
                launcher_log_line("manual recovery combo (LB+RB+Back) detected; scheduling previous rollback", NULL);
            }
        } else if (emb.data != NULL) {
            force_embedded_recover = true;
            launcher_log_line("manual recovery combo (LB+RB+Back) detected; scheduling embedded payload recovery", NULL);
        }
    }

    char target_path[320];
    char err[64];
    bool rolled_back = false;
    bool extracted = false;
    target_path[0] = '\0';
    err[0] = '\0';

    if (emb.data != NULL) {
        bool emb_ok = update_journal_ensure_embedded_runtime("game:\\", MOONX_UPDATE_PUBLIC_KEY_HEX,
                                                             &emb, force_embedded_recover, &extracted,
                                                             err, sizeof(err), launcher_log_line, NULL);
        if (!emb_ok) {
            if (!update_check_xex2_header("game:\\" UPD_REL_APP_XEX, NULL)) {
                launcher_log_close();
                show_fatal_and_exit(err[0] ? err : "embedded_extract");
                return;
            }
        }
    }
    record_install_root_path();

    if (!update_journal_prepare_launch("game:\\", MOONX_UPDATE_PUBLIC_KEY_HEX,
                                       MOONX_LAUNCHER_VERSION,
                                       target_path, sizeof(target_path),
                                       &rolled_back, err, sizeof(err),
                                       launcher_log_line, NULL)) {
        launcher_log_line(err[0] ? err : "prepare_launch failed", NULL);
        if (emb.data != NULL &&
            update_journal_ensure_embedded_runtime("game:\\", MOONX_UPDATE_PUBLIC_KEY_HEX,
                                                   &emb, true, &extracted,
                                                   err, sizeof(err), launcher_log_line, NULL)) {
            update_join_path("game:\\", UPD_REL_APP_XEX, target_path, sizeof(target_path));
            rolled_back = true;
        } else {
            launcher_log_close();
            show_fatal_and_exit(err[0] ? err : "prepare_launch");
            return;
        }
    }

    if (rolled_back)
        launcher_log_line("rollback active: previous version restored", NULL);

    if (!update_check_xex2_header(target_path, NULL)) {
        launcher_log_line("target XEX2 header check failed", NULL);
        if (emb.data != NULL &&
            update_journal_ensure_embedded_runtime("game:\\", MOONX_UPDATE_PUBLIC_KEY_HEX,
                                                   &emb, true, &extracted,
                                                   err, sizeof(err), launcher_log_line, NULL) &&
            update_check_xex2_header("game:\\" UPD_REL_APP_XEX, NULL)) {
            update_join_path("game:\\", UPD_REL_APP_XEX, target_path, sizeof(target_path));
        } else {
            launcher_log_close();
            show_fatal_and_exit("invalid moonx360.xex");
            return;
        }
    }

    {
        char lmsg[360];
        _snprintf(lmsg, sizeof(lmsg) - 1, "launching %s", target_path);
        lmsg[sizeof(lmsg) - 1] = '\0';
        launcher_log_line(lmsg, NULL);
    }

    if (has_launch_data) {
        DWORD set_rc = XSetLaunchData(launch_buf, launch_bytes);
        if (set_rc != ERROR_SUCCESS)
            launcher_log_line("warning: XSetLaunchData failed before handoff", NULL);
    }

    launcher_log_close();
    XLaunchNewImage(target_path, 0);

    /* Only reached if XLaunchNewImage unexpectedly returns. */
    launcher_log_open();
    launcher_log_line("XLaunchNewImage returned; attempting rollback", NULL);
    char fallback_path[320];
    fallback_path[0] = '\0';
    if (update_journal_launch_failed("game:\\", MOONX_UPDATE_PUBLIC_KEY_HEX,
                                     fallback_path, sizeof(fallback_path),
                                     err, sizeof(err), launcher_log_line, NULL) &&
        update_check_xex2_header(fallback_path, NULL)) {
        if (has_launch_data)
            XSetLaunchData(launch_buf, launch_bytes);
        launcher_log_line("launching restored fallback", NULL);
        launcher_log_close();
        XLaunchNewImage(fallback_path, 0);
    }

    if (emb.data != NULL &&
        update_journal_ensure_embedded_runtime("game:\\", MOONX_UPDATE_PUBLIC_KEY_HEX,
                                               &emb, true, &extracted,
                                               err, sizeof(err), launcher_log_line, NULL) &&
        update_check_xex2_header("game:\\" UPD_REL_APP_XEX, NULL)) {
        if (has_launch_data)
            XSetLaunchData(launch_buf, launch_bytes);
        launcher_log_line("launching emergency embedded payload", NULL);
        launcher_log_close();
        XLaunchNewImage("game:\\" UPD_REL_APP_XEX, 0);
    }

    launcher_log_line("fallback launch failed; returning to dashboard", NULL);
    launcher_log_close();
    show_fatal_and_exit(err[0] ? err : "launch failed");
}
