#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <string>
#include "../src/diagnostic_ring.h"
typedef long LONG;
struct CRITICAL_SECTION { bool held; };
static void InitializeCriticalSection(CRITICAL_SECTION *c) { c->held = false; }
static void EnterCriticalSection(CRITICAL_SECTION *c) { assert(!c->held); c->held = true; }
static void LeaveCriticalSection(CRITICAL_SECTION *c) { assert(c->held); c->held = false; }
static LONG InterlockedExchange(volatile LONG *p, LONG value) { LONG old = *p; *p = value; return old; }
static uint32_t GetTickCount() { return 42; }
static void gfx_release_ui_textures(void) {}
static std::map<std::string,std::string> files;
static int writes;
static bool fail_write;
static char *storage_read(const char *name, size_t *)
{
    if (!files.count(name)) return NULL;
    const std::string &s = files[name];
    char *out = (char *)malloc(s.size()+1);
    memcpy(out,s.c_str(),s.size()+1); return out;
}
static bool storage_write(const char *name, const void *data, size_t bytes)
{
    writes++;
    if (fail_write) return false;
    files[name].assign((const char *)data,bytes); return true;
}
void plat_diagnostics_checkpoint(const char *);
void plat_diagnostics_flush(void);
/* ACTUAL_STATE */
/* ACTUAL_API */
int main(void)
{
    files["last-log.txt"] = "old saved log\n";
    files["shutdown-stage.txt"] = "old shutdown phase\n";
    files["diagnostic-stage.txt"] = "old running marker\n";
    diagnostic_add("before configure"); assert(writes == 0);
    plat_diagnostics_configure(false); assert(writes == 0 && !diagnostic_enabled);
    assert(plat_has_diagnostics());
    char text[DIAG_SNAPSHOT_BYTES];
    plat_diagnostics_read(text,sizeof(text),true);
    assert(strstr(text,"old saved log") && strstr(text,"old shutdown phase") && strstr(text,"old running marker"));
    plat_diagnostics_configure(true); assert(diagnostic_enabled && writes == 1);
    int before = writes;
    diagnostic_add("stream packet error\nsecond line");
    assert(writes == before); // normal capture never writes to disk
    plat_diagnostics_read(text,sizeof(text),false);
    assert(strstr(text,"stream packet error") && strstr(text,"second line"));
    plat_diagnostics_flush();
    assert(files["last-log.txt"].find("stream packet error") != std::string::npos);
    plat_diagnostics_read(text,sizeof(text),true); assert(strstr(text,"old saved log"));
    plat_shutdown_stage("new shutdown phase\n");
    assert(files["shutdown-stage.txt"] == "new shutdown phase\n");
    plat_diagnostics_configure(false); before = writes;
    diagnostic_add("disabled"); plat_diagnostics_checkpoint("disabled"); plat_diagnostics_flush();
    assert(writes == before);
    fail_write = true; plat_diagnostics_configure(true);
    plat_diagnostics_read(text,sizeof(text),false); assert(strstr(text,"Save failed"));
    char guard[] = "XXXX";
    plat_diagnostics_read(guard,0,false); assert(guard[0] == 'X');
    plat_diagnostics_read(guard,1,false); assert(guard[0] == 0 && guard[1] == 'X');
    puts("PASS: actual SDK diagnostics: disabled/no I/O, RAM capture/no disk writes, checkpoint/flush,");
    puts("previous-run snapshot preserved, write errors visible, bounded reads and independent shutdown breadcrumb.");
}
