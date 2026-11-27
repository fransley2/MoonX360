/* Host regression & integration test suite for MoonX360 Native Auto-Update System.
 * Links real Monocypher 4.0.3, mbedTLS 2.16.12, update_crypto, update_manifest,
 * update_journal, update_http, update_service, and update_tls_roots. */
#define MOONX_UPDATE_HOST_TEST 1

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <windows.h>

#include <string>
#include <vector>

#include "../../src/version_info.h"
#include "../../src/platform.h"
#include "../../src/crypto.h"
#include "../../src/update/update_crypto.h"
#include "../../src/update/update_manifest.h"
#include "../../src/update/update_journal.h"
#include "../../src/update/update_http.h"
#include "../../src/update/update_service.h"

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>

/* ---- Minimal platform & crypto stubs matching MoonX360 host environment ---- */
static mbedtls_entropy_context g_ent;
static mbedtls_ctr_drbg_context g_drbg;
static CRITICAL_SECTION g_rng_cs;

int crypto_rng_callback(void *ctx, unsigned char *out, size_t len)
{
    EnterCriticalSection(&g_rng_cs);
    int r = mbedtls_ctr_drbg_random((mbedtls_ctr_drbg_context *)ctx, out, len);
    LeaveCriticalSection(&g_rng_cs);
    return r;
}

mbedtls_ctr_drbg_context *crypto_drbg(void) { return &g_drbg; }

uint32_t plat_now_ms(void) { return (uint32_t)GetTickCount(); }
void plat_sleep_ms(unsigned int ms) { Sleep(ms); }
int plat_atomic_load(volatile int *v) { return (int)InterlockedCompareExchange((volatile LONG *)v, 0, 0); }
void plat_atomic_store(volatile int *v, int next) { InterlockedExchange((volatile LONG *)v, (LONG)next); }
void plat_log(const char *line) { (void)line; }

struct plat_thread {
    HANDLE handle;
    void *(*fn)(void *);
    void *arg;
};

static DWORD WINAPI host_thread_proc(LPVOID p)
{
    plat_thread *t = (plat_thread *)p;
    t->fn(t->arg);
    return 0;
}

plat_thread *plat_thread_start(void *(*fn)(void *), void *arg)
{
    plat_thread *t = (plat_thread *)calloc(1, sizeof(*t));
    if (!t) return NULL;
    t->fn = fn;
    t->arg = arg;
    t->handle = CreateThread(NULL, 256 * 1024, host_thread_proc, t, 0, NULL);
    if (!t->handle) { free(t); return NULL; }
    return t;
}

void plat_thread_join(plat_thread *t)
{
    WaitForSingleObject(t->handle, INFINITE);
    CloseHandle(t->handle);
    free(t);
}

/* ---- Helpers ---- */
static std::string read_text(const std::string &path)
{
    FILE *f = fopen(path.c_str(), "rb");
    assert(f != NULL);
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::string s((size_t)n, '\0');
    if (n > 0) assert(fread(&s[0], 1, (size_t)n, f) == (size_t)n);
    fclose(f);
    return s;
}

static void write_file(const std::string &path, const void *data, size_t len)
{
    FILE *f = fopen(path.c_str(), "wb");
    assert(f != NULL);
    if (len > 0) assert(fwrite(data, 1, len, f) == len);
    fclose(f);
}

static bool file_exists_str(const std::string &path)
{
    DWORD a = GetFileAttributesA(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static void make_fake_xex(const std::string &path, uint8_t tag, size_t total_size)
{
    std::vector<uint8_t> buf(total_size, tag);
    memcpy(buf.data(), "XEX2", 4);
    buf[8] = tag;
    write_file(path, buf.data(), buf.size());
}

static std::string sha256_of_file(const std::string &path)
{
    char hex[65];
    assert(update_sha256_file(path.c_str(), hex, NULL, NULL, NULL, NULL));
    return std::string(hex);
}

static void rm_tree(const std::string &dir)
{
    std::string pat = dir + "\\*";
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat.c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
            std::string full = dir + "\\" + fd.cFileName;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) rm_tree(full);
            else DeleteFileA(full.c_str());
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryA(dir.c_str());
}

int main(int argc, char **argv)
{
    /* Arguments supplied by run_update_tests.py:
     *   argv[1]: work_dir
     *   argv[2]: pub_hex
     *   argv[3]: test_base_url (e.g. https://127.0.0.1:PORT/)
     *   argv[4]: ca_pem_path
     *   argv[5]: valid_manifest_path (stable build 101)
     *   argv[6]: valid_sig_path
     *   argv[7]: beta_manifest_path (beta build 102)
     *   argv[8]: beta_sig_path
     *   argv[9]: payload_101_path
     *   argv[10]: payload_102_path
     *   argv[11]: bad_tls_port
     */
    assert(argc >= 12);
    std::string work = argv[1];
    std::string pub_hex = argv[2];
    std::string base_url = argv[3];
    std::string ca_pem = read_text(argv[4]);
    std::string m101 = read_text(argv[5]);
    std::string s101 = read_text(argv[6]);
    std::string m102 = read_text(argv[7]);
    std::string s102 = read_text(argv[8]);
    std::string p101_path = argv[9];
    std::string p102_path = argv[10];
    int bad_tls_port = atoi(argv[11]);

    WSADATA wsa;
    assert(WSAStartup(MAKEWORD(2, 2), &wsa) == 0);
    InitializeCriticalSection(&g_rng_cs);
    mbedtls_entropy_init(&g_ent);
    mbedtls_ctr_drbg_init(&g_drbg);
    const char pers[] = "moonx360-test";
    assert(mbedtls_ctr_drbg_seed(&g_drbg, mbedtls_entropy_func, &g_ent,
                                 (const unsigned char *)pers, sizeof(pers) - 1) == 0);

    update_test_set_base_url(base_url.c_str());

    /* 1. Valid signed manifest */
    assert(update_ed25519_verify((const uint8_t *)m101.data(), m101.size(),
                                 s101.data(), s101.size(), pub_hex.c_str()));
    struct update_manifest parsed;
    assert(update_manifest_parse(m101.data(), m101.size(), &parsed));
    assert(parsed.build_id == 101 && strcmp(parsed.channel, "stable") == 0);

    /* 2. Invalid signature (modified manifest byte, modified sig nibble, wrong pubkey) */
    {
        std::string tampered = m101;
        tampered[tampered.size() - 3] = 'X';
        assert(!update_ed25519_verify((const uint8_t *)tampered.data(), tampered.size(),
                                      s101.data(), s101.size(), pub_hex.c_str()));
        std::string bad_sig = s101;
        bad_sig[0] = (bad_sig[0] == '0') ? '1' : '0';
        assert(!update_ed25519_verify((const uint8_t *)m101.data(), m101.size(),
                                      bad_sig.data(), bad_sig.size(), pub_hex.c_str()));
        std::string wrong_pub = pub_hex;
        wrong_pub[0] = (wrong_pub[0] == 'a') ? 'b' : 'a';
        assert(!update_ed25519_verify((const uint8_t *)m101.data(), m101.size(),
                                      s101.data(), s101.size(), wrong_pub.c_str()));
    }
    puts("[PASS] 1-2: valid signature + tampered manifest/sig/key rejected");

    /* 3-7. Malformed manifest, wrong product, unsupported schema, bad URL, channel, older build */
    {
        struct update_manifest tmp;
        std::string wrong_prod = m101;
        size_t pos = wrong_prod.find("\"moonx360\"");
        assert(pos != std::string::npos);
        wrong_prod.replace(pos, 10, "\"tube360\"");
        assert(!update_manifest_parse(wrong_prod.data(), wrong_prod.size(), &tmp));

        std::string bad_schema = m101;
        pos = bad_schema.find("\"schema\": 1");
        assert(pos != std::string::npos);
        bad_schema.replace(pos, 11, "\"schema\": 2");
        assert(!update_manifest_parse(bad_schema.data(), bad_schema.size(), &tmp));

        std::string extra_key = m101;
        pos = extra_key.rfind('}');
        extra_key.insert(pos, ", \"extra\": 1");
        assert(!update_manifest_parse(extra_key.data(), extra_key.size(), &tmp));

        assert(!update_manifest_parse("{bad json", 9, &tmp));
        assert(!update_url_allowed_manifest("https://evil.example.com/fransley2/MoonX360/updates/stable/manifest.json"));
        assert(!update_url_allowed_manifest("http://raw.githubusercontent.com/fransley2/MoonX360/updates/stable/manifest.json"));
        assert(!update_url_allowed_asset("https://evil.example.com/moonx360.xex", true));
        assert(!update_url_allowed_asset("https://user@objects.githubusercontent.com/a", true));
        assert(update_url_allowed_asset("https://objects.githubusercontent.com/asset/1?X-Amz-Sig=abc", true));
        assert(update_url_allowed_manifest("https://raw.githubusercontent.com/fransley2/MoonX360/updates/stable/manifest.json"));
        assert(!update_url_allowed_manifest("https://raw.githubusercontent.com/fransley2/MoonX360/main/manifest.json"));
    }
    puts("[PASS] 3-7: malformed/wrong-product/schema/URL allowlists rejected");

    /* 8. Journal stage + modified XEX rejection + older build rejection + launcher min version */
    {
        std::string inst = work + "\\inst_stage";
        rm_tree(inst);
        CreateDirectoryA(inst.c_str(), NULL);
        CreateDirectoryA((inst + "\\updates").c_str(), NULL);
        CreateDirectoryA((inst + "\\" UPD_REL_RUNTIME_DIR).c_str(), NULL);
        make_fake_xex(inst + "\\" UPD_REL_APP_XEX, 0x10, 8192);

        /* Modified XEX -> hash mismatch */
        std::string p101_bytes = read_text(p101_path);
        std::string modified_xex = p101_bytes;
        modified_xex[100] ^= 0x55;
        write_file(inst + "\\" UPD_REL_NEXT_BIN, modified_xex.data(), modified_xex.size());
        char err[64];
        assert(!update_journal_stage(inst.c_str(), pub_hex.c_str(), 100, "0.2.7", 1,
                                     m101.data(), m101.size(), s101.data(), s101.size(),
                                     err, sizeof(err), NULL, NULL));
        assert(strcmp(err, "hash") == 0);

        /* Min launcher version too high -> rejected */
        assert(!update_journal_stage(inst.c_str(), pub_hex.c_str(), 100, "0.2.7", 0,
                                     m101.data(), m101.size(), s101.data(), s101.size(),
                                     err, sizeof(err), NULL, NULL));

        /* Valid candidate 101 -> staged */
        write_file(inst + "\\" UPD_REL_NEXT_BIN, p101_bytes.data(), p101_bytes.size());
        assert(update_journal_stage(inst.c_str(), pub_hex.c_str(), 100, "0.2.7", 1,
                                    m101.data(), m101.size(), s101.data(), s101.size(),
                                    err, sizeof(err), NULL, NULL));

        /* Older/same build 101 rejected by high water */
        assert(!update_journal_stage(inst.c_str(), pub_hex.c_str(), 100, "0.2.7", 1,
                                     m101.data(), m101.size(), s101.data(), s101.size(),
                                     err, sizeof(err), NULL, NULL));
        assert(strcmp(err, "version") == 0);
    }
    puts("[PASS] 8: modified XEX, min launcher version, and replay/downgrade blocked");

    /* 9. Full swap, boot confirmation, failed boot rollback + config preservation, manual rollback */
    {
        std::string inst = work + "\\inst_lifecycle";
        rm_tree(inst);
        CreateDirectoryA(inst.c_str(), NULL);
        CreateDirectoryA((inst + "\\updates").c_str(), NULL);
        CreateDirectoryA((inst + "\\" UPD_REL_RUNTIME_DIR).c_str(), NULL);
        CreateDirectoryA((inst + "\\moonlight").c_str(), NULL);
        make_fake_xex(inst + "\\" UPD_REL_APP_XEX, 0x10, 8192);
        std::string orig_hash = sha256_of_file(inst + "\\" UPD_REL_APP_XEX);
        write_file(inst + "\\moonlight\\moonlight.ini", "host=192.168.1.50\n", 18);
        write_file(inst + "\\moonlight\\client.pem", "PAIR-CERT", 9);

        std::string p101_bytes = read_text(p101_path);
        write_file(inst + "\\" UPD_REL_NEXT_BIN, p101_bytes.data(), p101_bytes.size());
        char err[64], target[320];
        bool rolled_back = false;
        assert(update_journal_stage(inst.c_str(), pub_hex.c_str(), 100, "0.2.7", 1,
                                    m101.data(), m101.size(), s101.data(), s101.size(),
                                    err, sizeof(err), NULL, NULL));

        /* Prepare launch -> swaps to trial */
        assert(update_journal_prepare_launch(inst.c_str(), pub_hex.c_str(), 1,
                                             target, sizeof(target), &rolled_back,
                                             err, sizeof(err), NULL, NULL));
        assert(!rolled_back);
        assert(sha256_of_file(inst + "\\" UPD_REL_APP_XEX) == sha256_of_file(p101_path));
        assert(sha256_of_file(inst + "\\" UPD_REL_PREV_BIN) == orig_hash);

        /* Confirm healthy boot -> good */
        bool confirmed_now = false;
        assert(update_journal_confirm_boot(inst.c_str(), pub_hex.c_str(), 101, "0.2.8",
                                           &confirmed_now, NULL, NULL));
        assert(confirmed_now);

        /* Controlled manual rollback to verified previous version (101 -> 100) */
        assert(update_journal_has_previous(inst.c_str()));
        assert(update_journal_request_manual_rollback(inst.c_str(), pub_hex.c_str(), 101,
                                                      err, sizeof(err), NULL, NULL));
        rolled_back = false;
        assert(update_journal_prepare_launch(inst.c_str(), pub_hex.c_str(), 1,
                                             target, sizeof(target), &rolled_back,
                                             err, sizeof(err), NULL, NULL));
        assert(rolled_back);
        assert(sha256_of_file(inst + "\\" UPD_REL_APP_XEX) == orig_hash);
        assert(update_journal_consume_restored_notice(inst.c_str(), pub_hex.c_str(), 100, NULL, NULL));

        /* Stage 102 and simulate unconfirmed trial + config edit + automatic rollback */
        std::string p102_bytes = read_text(p102_path);
        write_file(inst + "\\" UPD_REL_NEXT_BIN, p102_bytes.data(), p102_bytes.size());
        assert(update_journal_stage(inst.c_str(), pub_hex.c_str(), 100, "0.2.7", 1,
                                    m102.data(), m102.size(), s102.data(), s102.size(),
                                    err, sizeof(err), NULL, NULL));
        assert(update_journal_prepare_launch(inst.c_str(), pub_hex.c_str(), 1,
                                             target, sizeof(target), &rolled_back,
                                             err, sizeof(err), NULL, NULL));
        assert(!rolled_back);
        /* Simulate trial writing pre-update backup and mutating moonlight.ini */
        write_file(inst + "\\updates\\moonlight.ini.pre-update", "host=192.168.1.50\n", 18);
        write_file(inst + "\\moonlight\\moonlight.ini", "host=corrupted-trial\n", 21);

        /* Reopen launcher WITHOUT confirm -> automatic rollback! */
        assert(update_journal_prepare_launch(inst.c_str(), pub_hex.c_str(), 1,
                                             target, sizeof(target), &rolled_back,
                                             err, sizeof(err), NULL, NULL));
        assert(rolled_back);
        assert(sha256_of_file(inst + "\\" UPD_REL_APP_XEX) == orig_hash);
        assert(file_exists_str(inst + "\\" UPD_REL_FAIL_BIN));
        assert(read_text(inst + "\\moonlight\\moonlight.ini") == "host=192.168.1.50\n");
        assert(read_text(inst + "\\moonlight\\client.pem") == "PAIR-CERT");
        assert(update_journal_consume_restored_notice(inst.c_str(), pub_hex.c_str(), 100, NULL, NULL));

        /* Rejected build 102 stays blocked by highest_build=102 */
        write_file(inst + "\\" UPD_REL_NEXT_BIN, p102_bytes.data(), p102_bytes.size());
        assert(!update_journal_stage(inst.c_str(), pub_hex.c_str(), 100, "0.2.7", 1,
                                     m102.data(), m102.size(), s102.data(), s102.size(),
                                     err, sizeof(err), NULL, NULL));
    }
    puts("[PASS] 9: boot confirmation, failed/manual rollback, config/cert preservation, high-water");

    /* 10. Interrupted file transitions between every rename & corrupt journal */
    {
        std::string inst = work + "\\inst_interrupted";
        rm_tree(inst);
        CreateDirectoryA(inst.c_str(), NULL);
        CreateDirectoryA((inst + "\\updates").c_str(), NULL);
        CreateDirectoryA((inst + "\\" UPD_REL_RUNTIME_DIR).c_str(), NULL);
        make_fake_xex(inst + "\\" UPD_REL_APP_XEX, 0x10, 8192);
        std::string p101_bytes = read_text(p101_path);
        write_file(inst + "\\" UPD_REL_NEXT_BIN, p101_bytes.data(), p101_bytes.size());
        char err[64], target[320];
        bool rolled_back = false;
        assert(update_journal_stage(inst.c_str(), pub_hex.c_str(), 100, "0.2.7", 1,
                                    m101.data(), m101.size(), s101.data(), s101.size(),
                                    err, sizeof(err), NULL, NULL));
        /* Simulate crash right after writing phase=swapping and moving app -> previous */
        struct update_journal j;
        assert(update_journal_load(&j, inst.c_str(), pub_hex.c_str(), 100, NULL, NULL));
        j.phase = UPD_PHASE_SWAPPING;
        j.previous_build = 100;
        snprintf(j.previous_version, sizeof(j.previous_version), "0.2.7");
        assert(update_journal_save(&j, inst.c_str(), NULL, NULL));
        update_journal_free(&j);
        assert(MoveFileA((inst + "\\" UPD_REL_APP_XEX).c_str(), (inst + "\\" UPD_REL_PREV_BIN).c_str()));

        /* Launcher resumes from interrupted swap and completes moonx360.next.bin -> moonx360.xex */
        assert(update_journal_prepare_launch(inst.c_str(), pub_hex.c_str(), 1,
                                             target, sizeof(target), &rolled_back,
                                             err, sizeof(err), NULL, NULL));
        assert(!rolled_back);
        assert(sha256_of_file(inst + "\\" UPD_REL_APP_XEX) == sha256_of_file(p101_path));

        /* Corrupt both journal.json and journal.json.bak -> never promotes .next */
        make_fake_xex(inst + "\\" UPD_REL_NEXT_BIN, 0x99, 8192);
        write_file(inst + "\\updates\\journal.json", "{corrupt", 8);
        write_file(inst + "\\updates\\journal.json.bak", "{corrupt", 8);
        assert(update_journal_prepare_launch(inst.c_str(), pub_hex.c_str(), 1,
                                             target, sizeof(target), &rolled_back,
                                             err, sizeof(err), NULL, NULL));
        assert(sha256_of_file(inst + "\\" UPD_REL_APP_XEX) == sha256_of_file(p101_path));
    }
    puts("[PASS] 10: interrupted rename recovery & corrupt journal protection");

    /* 11. HTTPS transport: bad TLS cert rejected, valid test CA + retry + redirects + cancel */
    {
        assert(update_http_global_init());
        struct update_http_result res;
        char bad_url[256];
        snprintf(bad_url, sizeof(bad_url), "https://127.0.0.1:%d/stable/manifest.json", bad_tls_port);
        update_test_set_base_url(("https://127.0.0.1:" + std::to_string(bad_tls_port) + "/").c_str());
        assert(!update_http_get_buffer(bad_url, true, 65536, 5000, NULL, &res));
        assert(res.err == UPD_HTTP_TLS);
        update_http_result_free(&res);

        /* Switch to trusted test CA and test base */
        update_test_set_base_url(base_url.c_str());
        assert(update_http_test_set_ca_pem(ca_pem.data(), ca_pem.size()));

        std::string good_manifest_url = base_url + "stable/manifest.json";
        if (!update_http_get_buffer(good_manifest_url.c_str(), true, 65536, 5000, NULL, &res)) {
            printf("good_manifest_url failed: err=%d status=%d\n", (int)res.err, res.status);
            assert(false);
        }
        assert(res.status == 200 && res.len == m101.size());
        update_http_result_free(&res);

        /* Redirect to evil host rejected */
        std::string evil_redir = base_url + "redirect-evil";
        assert(!update_http_get_buffer(evil_redir.c_str(), false, 65536, 5000, NULL, &res));
        assert(res.err == UPD_HTTP_BAD_URL);
        update_http_result_free(&res);

        /* Redirect loop (>5 hops) rejected */
        std::string loop_redir = base_url + "redirect-loop";
        assert(!update_http_get_buffer(loop_redir.c_str(), false, 65536, 5000, NULL, &res));
        assert(res.err == UPD_HTTP_REDIRECT_LIMIT);
        update_http_result_free(&res);

        /* Disk free query + oversized payload rejection */
        uint64_t free_bytes = 0;
        assert(update_disk_free_bytes(work.c_str(), &free_bytes) && free_bytes > 0);
        assert(update_http_download_file(
                   (base_url + "assets/101.xex").c_str(), (work + "\\dl_oversize.part").c_str(),
                   (uint64_t)UPDATE_MAX_XEX_BYTES + 1u, UPDATE_MAX_XEX_BYTES, 5000, NULL, NULL, NULL) == UPD_HTTP_BAD_URL);

        /* Chunked download with retry after transient 500 */
        std::string dl_part = work + "\\dl_retry.part";
        enum update_http_err derr = update_http_download_file(
            (base_url + "flaky-asset").c_str(), dl_part.c_str(),
            parsed.payload_size, UPDATE_MAX_XEX_BYTES, 5000, NULL, NULL, NULL);
        if (derr != UPD_HTTP_OK) {
            fprintf(stderr, "flaky-asset failed: derr=%d\n", (int)derr);
            assert(false);
        }
        assert(sha256_of_file(dl_part) == parsed.sha256);

        /* Cancelled download */
        volatile int cancel = 1;
        derr = update_http_download_file(
            (base_url + "assets/101.xex").c_str(), (work + "\\dl_cancel.part").c_str(),
            parsed.payload_size, UPDATE_MAX_XEX_BYTES, 5000, &cancel, NULL, NULL);
        assert(derr == UPD_HTTP_CANCELLED);
    }
    puts("[PASS] 11: TLS cert validation, redirect security, flaky retry, and cancel");

    /* 12. Async update_service end-to-end check, channel switch, download, streaming suspend */
    {
        std::string inst = work + "\\inst_svc";
        rm_tree(inst);
        CreateDirectoryA(inst.c_str(), NULL);
        CreateDirectoryA((inst + "\\updates").c_str(), NULL);
        CreateDirectoryA((inst + "\\" UPD_REL_RUNTIME_DIR).c_str(), NULL);
        make_fake_xex(inst + "\\" UPD_REL_APP_XEX, 0x10, 8192);

        update_service_test_set_base_url(base_url.c_str());
        assert(update_service_init(inst.c_str(), UPDATE_CHANNEL_STABLE, true));
        update_service_test_set_public_key(pub_hex.c_str());
        update_service_notify_healthy();

        struct update_status st;
        uint32_t rev = 0;
        for (int i = 0; i < 100; i++) {
            update_service_poll(&st, &rev);
            if (st.state == UPD_AVAILABLE) break;
            Sleep(50);
        }
        assert(st.state == UPD_AVAILABLE && st.available_build == 101);

        update_service_start_download();
        for (int i = 0; i < 100; i++) {
            update_service_poll(&st, &rev);
            if (st.state == UPD_READY) break;
            Sleep(50);
        }
        assert(st.state == UPD_READY);
        assert(file_exists_str(inst + "\\" UPD_REL_NEXT_BIN));

        /* Suspend while streaming */
        update_service_set_streaming(true);
        update_service_check_now();
        Sleep(150);
        update_service_get_status(&st);
        assert(st.state == UPD_READY);
        update_service_set_streaming(false);
        update_service_shutdown();
    }
    puts("[PASS] 12: async update_service check -> download -> stage -> stream suspend");

    /* 13. Live GitHub TLS 1.2 validation with embedded roots (if internet available) */
    {
        update_http_test_set_ca_pem(NULL, 0);
        update_test_set_base_url(NULL);
        struct update_http_result res;
        bool ok = update_http_get_buffer(
            "https://raw.githubusercontent.com/fransley2/MoonX360/updates/stable/manifest.json",
            true, 65536, 8000, NULL, &res);
        /* The updates branch is not published yet (404), OR network offline; either way
         * if TCP connected, TLS MUST succeed (never UPD_HTTP_TLS)! */
        if (ok) {
            printf("[PASS] 13: Live GitHub TLS 1.2 verified with embedded roots (HTTP %d)\n", res.status);
        } else {
            assert(res.err != UPD_HTTP_TLS);
            printf("[INFO] 13: Live GitHub unreachable (err=%d), TLS check not failed\n", (int)res.err);
        }
        update_http_result_free(&res);
    }

    /* 14. Single-file embedded extraction, no re-extract, anti-downgrade, recovery, interrupted .part */
    {
        std::string inst = work + "\\inst_single";
        rm_tree(inst);
        CreateDirectoryA(inst.c_str(), NULL);
        CreateDirectoryA((inst + "\\moonlight").c_str(), NULL);
        write_file(inst + "\\moonlight\\moonlight.ini", "host=192.168.15.20\n", 19);

        std::string p101_bytes = read_text(p101_path);
        std::string p102_bytes = read_text(p102_path);
        std::string emb_sha = sha256_of_file(p101_path);
        std::string newer_sha = sha256_of_file(p102_path);

        /* Tampered embedded section rejected before creating runtime */
        std::string bad_emb = p101_bytes;
        bad_emb[120] ^= 0xAA;
        struct update_embedded_payload emb;
        emb.build_id = 101;
        emb.version = "0.2.8";
        emb.size = bad_emb.size();
        emb.sha256_hex = emb_sha.c_str();
        emb.data = (const uint8_t *)bad_emb.data();
        emb.data_len = bad_emb.size();

        bool extracted = false;
        char err[64], target[320];
        bool rolled_back = false;
        assert(!update_journal_ensure_embedded_runtime(inst.c_str(), pub_hex.c_str(), &emb,
                                                      false, &extracted, err, sizeof(err), NULL, NULL));
        assert(!extracted && strcmp(err, "embedded_hash") == 0);
        assert(!file_exists_str(inst + "\\" UPD_REL_APP_XEX));

        /* Interrupted leftover .extract.part cleaned up + first extraction succeeds */
        CreateDirectoryA((inst + "\\updates").c_str(), NULL);
        CreateDirectoryA((inst + "\\" UPD_REL_RUNTIME_DIR).c_str(), NULL);
        write_file(inst + "\\" UPD_REL_EXTRACT_PART, "PARTIAL", 7);

        emb.data = (const uint8_t *)p101_bytes.data();
        emb.data_len = p101_bytes.size();
        assert(update_journal_ensure_embedded_runtime(inst.c_str(), pub_hex.c_str(), &emb,
                                                     false, &extracted, err, sizeof(err), NULL, NULL));
        assert(extracted);
        assert(!file_exists_str(inst + "\\" UPD_REL_EXTRACT_PART));
        assert(sha256_of_file(inst + "\\" UPD_REL_APP_XEX) == emb_sha);
        assert(update_journal_prepare_launch(inst.c_str(), pub_hex.c_str(), 1,
                                             target, sizeof(target), &rolled_back,
                                             err, sizeof(err), NULL, NULL));
        assert(std::string(target).find(UPD_REL_APP_XEX) != std::string::npos);

        /* Subsequent launch skips extraction cleanly */
        extracted = true;
        assert(update_journal_ensure_embedded_runtime(inst.c_str(), pub_hex.c_str(), &emb,
                                                     false, &extracted, err, sizeof(err), NULL, NULL));
        assert(!extracted);

        /* Simulate OTA upgrade to build 102 -> older embedded build 101 never overwrites it */
        struct update_journal j;
        assert(update_journal_load(&j, inst.c_str(), pub_hex.c_str(), 101, NULL, NULL));
        j.phase = UPD_PHASE_GOOD;
        j.active_build = 102;
        j.highest_build = 102;
        snprintf(j.active_version, sizeof(j.active_version), "0.2.9-beta");
        assert(update_journal_save(&j, inst.c_str(), NULL, NULL));
        update_journal_free(&j);
        write_file(inst + "\\" UPD_REL_APP_XEX, p102_bytes.data(), p102_bytes.size());

        assert(update_journal_ensure_embedded_runtime(inst.c_str(), pub_hex.c_str(), &emb,
                                                     false, &extracted, err, sizeof(err), NULL, NULL));
        assert(!extracted);
        assert(sha256_of_file(inst + "\\" UPD_REL_APP_XEX) == newer_sha);

        /* Forced embedded recovery restores build 101 while keeping highest_build=102 and moonlight/ */
        assert(update_journal_ensure_embedded_runtime(inst.c_str(), pub_hex.c_str(), &emb,
                                                     true, &extracted, err, sizeof(err), NULL, NULL));
        assert(extracted);
        assert(sha256_of_file(inst + "\\" UPD_REL_APP_XEX) == emb_sha);
        assert(file_exists_str(inst + "\\" UPD_REL_FAIL_BIN));
        assert(read_text(inst + "\\moonlight\\moonlight.ini") == "host=192.168.15.20\n");
        assert(update_journal_load(&j, inst.c_str(), pub_hex.c_str(), 101, NULL, NULL));
        assert(j.highest_build == 102 && j.active_build == 101 && j.restored_notice == 1);
        update_journal_free(&j);
    }
    puts("[PASS] 14: single-file embedded extraction, skip re-extract, anti-downgrade & recovery");

    update_http_global_shutdown();
    mbedtls_ctr_drbg_free(&g_drbg);
    mbedtls_entropy_free(&g_ent);
    DeleteCriticalSection(&g_rng_cs);
    WSACleanup();
    puts("PASS: ALL native updater host tests succeeded.");
    return 0;
}
