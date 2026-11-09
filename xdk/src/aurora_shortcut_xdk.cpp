/* Small Aurora entrypoint: file I/O, diagnostics, XAM UI and XDK launch only. */
#include <xtl.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include "../../src/aurora_launch.h"
#include "../../src/aurora_link_io.h"
#include "aurora_mount_xdk.h"

typedef char moonx_shortcut_abi_check[(sizeof(moonx_launch_data)==468 &&
    offsetof(moonx_launch_data,checksum)==464)?1:-1];
static FILE *trace_file;
static DWORD trace_start;

static void trace(const char *format,...)
{
    char detail[352],line[400];va_list args;
    va_start(args,format);_vsnprintf(detail,sizeof(detail)-1,format,args);va_end(args);
    detail[sizeof(detail)-1]=0;
    _snprintf(line,sizeof(line)-1,"[%lu ms] %s\n",(unsigned long)(GetTickCount()-trace_start),detail);
    line[sizeof(line)-1]=0;
    OutputDebugStringA(line);
    if(trace_file){fputs(line,trace_file);fflush(trace_file);}
}
static void trace_close(void)
{if(trace_file){fflush(trace_file);fclose(trace_file);trace_file=NULL;}}

static void fatal(const char *stage,DWORD error,const char *path)
{
    trace("[FATAL] stage=%s error=0x%08lX path=%s",stage,(unsigned long)error,path?path:"(none)");
    bool log_saved=trace_file!=NULL;trace_close();
    char message[400];WCHAR wide[400];
    _snprintf(message,sizeof(message)-1,"Could not launch MoonX360.\nStage: %s\nPath: %s\nError: 0x%08lX\nSee: %s",
              stage,path?path:"(none)",(unsigned long)error,
              log_saved?"moonx360-shortcut.log":"shortcut log unavailable");
    message[sizeof(message)-1]=0;
    if(!MultiByteToWideChar(CP_UTF8,0,message,-1,wide,400)){
        static const WCHAR fallback[]=L"Could not launch MoonX360. Return to Dashboard.";
        memcpy(wide,fallback,sizeof(fallback));
    }
    const WCHAR *buttons[]={L"Return to Dashboard"};
    MESSAGEBOX_RESULT result;XOVERLAPPED pending;
    ZeroMemory(&result,sizeof(result));ZeroMemory(&pending,sizeof(pending));
    DWORD rc=XShowMessageBoxUI(0,L"MoonX360 Shortcut Error",wide,1,buttons,0,0,&result,&pending);
    if(rc==ERROR_IO_PENDING){
        DWORD started=GetTickCount();
        while(!XHasOverlappedIoCompleted(&pending) && GetTickCount()-started<10000)Sleep(16);
    }
    XLaunchNewImage(XLAUNCH_KEYWORD_DEFAULT_APP,0);
}

void __cdecl main()
{
    trace_file=fopen("game:\\moonx360-shortcut.log","wb"); /* First action: truncate any stale run. */
    int log_errno=errno;DWORD log_os_error=GetLastError();
    trace_start=GetTickCount();
    trace("MoonX360 Shortcut Launcher | build %s %s | TitleID 4D583353 | mode %s",
          __DATE__,__TIME__,
#ifdef MOONX_SHORTCUT_LAUNCH_MAIN_ONLY
          "MAIN_ONLY"
#elif defined(MOONX_SHORTCUT_DIAGNOSTIC)
          "DIAGNOSTIC"
#else
          "NORMAL"
#endif
          );
    trace("[01] ENTRY log=%s errno=%d os_error=%lu",trace_file?"open":"unavailable",
          trace_file?0:log_errno,(unsigned long)(trace_file?0:log_os_error));
    DWORD game_attr=GetFileAttributesA("game:\\default.xex");
    DWORD game_error=game_attr==INVALID_FILE_ATTRIBUTES?GetLastError():0;
    trace("[02] game: default.xex attributes=0x%08lX error=%lu",(unsigned long)game_attr,(unsigned long)game_error);
    trace("[03] reading game:\\moonx360.link");
    FILE *file=fopen("game:\\moonx360.link","rb");
    int link_errno=errno;DWORD link_error=GetLastError();
    if(!file){trace("[03] fopen failed errno=%d os_error=%lu",link_errno,(unsigned long)link_error);
        fatal("open shortcut link",link_error?link_error:(DWORD)link_errno,"game:\\moonx360.link");}
    if(fseek(file,0,SEEK_END)!=0){int seek_error=errno;fclose(file);
        fatal("seek shortcut link",(DWORD)seek_error,"game:\\moonx360.link");}
    long length=ftell(file);
    trace("[04] link file size=%ld writer/reader ABI=%u",length,(unsigned)sizeof(moonx_launch_data));
    if(length!=(long)sizeof(moonx_launch_data)){fclose(file);fatal("link size",(DWORD)length,"game:\\moonx360.link");}
    rewind(file);
    moonx_launch_data link;
    size_t read=moonx_link_read_payload(file,&link);int read_error=ferror(file);fclose(file);
    if(read!=sizeof(link) || read_error)fatal("read shortcut link",(DWORD)read,"game:\\moonx360.link");
    trace("[05] raw magic=%02X%02X%02X%02X version=%u size=%u checksum=0x%08lX expected=0x%08lX",
          (unsigned char)link.magic[0],(unsigned char)link.magic[1],
          (unsigned char)link.magic[2],(unsigned char)link.magic[3],
          (unsigned)link.version,(unsigned)link.size,(unsigned long)link.checksum,
          (unsigned long)moonx_launch_checksum(&link));
    if(!moonx_launch_valid(&link,sizeof(link)))fatal("MX36 validation",ERROR_INVALID_DATA,"game:\\moonx360.link");
    trace("[06] MX36 validation OK host UUID=%s app ID=%ld profile=%s title=%s",
          link.host_id,(long)link.app_id,link.profile_id[0]?link.profile_id:"(current)",link.title);
    trace("[07] main XEX path=%s",link.main_xex);
    moonx_mount_report mount;
    trace("[08] preparing mount for %s",link.main_xex);
    bool mounted=moonx_mount_for_path_ex(link.main_xex,&mount);
    trace("[09] mount root=%s existing=%d attempted=%d status=0x%08lX accessible=%d target=%s",
          mount.root,mount.existed,mount.attempted,(unsigned long)mount.status,mount.accessible,mount.target);
    if(!mounted)fatal("mount main XEX device",(DWORD)mount.status,link.main_xex);
    const char *colon=strchr(link.main_xex,':');
    if(colon && mount.target[0]){
        char native_path[320];_snprintf(native_path,sizeof(native_path)-1,"%s%s",mount.target,colon+1);
        native_path[sizeof(native_path)-1]=0;trace("[10] diagnostic NT device path=%s",native_path);
    }
    DWORD attributes=GetFileAttributesA(link.main_xex);
    DWORD attribute_error=attributes==INVALID_FILE_ATTRIBUTES?GetLastError():0;
    trace("[11] main XEX attributes=0x%08lX error=%lu",(unsigned long)attributes,(unsigned long)attribute_error);
    if(attributes==INVALID_FILE_ATTRIBUTES || (attributes&FILE_ATTRIBUTE_DIRECTORY))
        fatal("main XEX attributes",attribute_error,link.main_xex);
    FILE *main_file=fopen(link.main_xex,"rb");
    int main_errno=errno;DWORD main_error=main_file?0:GetLastError();
    trace("[12] main XEX fopen=%s errno=%d os_error=%lu",main_file?"OK":"FAIL",
          main_file?0:main_errno,(unsigned long)main_error);
    if(!main_file)fatal("main XEX open",main_error?main_error:(DWORD)main_errno,link.main_xex);
    if(fseek(main_file,0,SEEK_END)!=0){int seek_error=errno;fclose(main_file);
        fatal("main XEX seek",(DWORD)seek_error,link.main_xex);}
    long main_size=ftell(main_file);rewind(main_file);
    char magic[4];size_t magic_bytes=fread(magic,1,sizeof(magic),main_file);
    int main_read_error=ferror(main_file);fclose(main_file);
    trace("[13] main XEX size=%ld header=%02X%02X%02X%02X XEX2=%s",
          main_size,magic_bytes>0?(unsigned char)magic[0]:0,magic_bytes>1?(unsigned char)magic[1]:0,
          magic_bytes>2?(unsigned char)magic[2]:0,magic_bytes>3?(unsigned char)magic[3]:0,
          magic_bytes==4 && !memcmp(magic,"XEX2",4)?"YES":"NO");
    if(main_size<=0 || magic_bytes!=4 || main_read_error || memcmp(magic,"XEX2",4))
        fatal("main XEX header",ERROR_INVALID_DATA,link.main_xex);
#ifdef MOONX_SHORTCUT_LAUNCH_MAIN_ONLY
    trace("[14] MAIN_ONLY: XSetLaunchData intentionally skipped");
#else
    trace("[14] preparing launch data size=%u magic=MX36 version=%u",(unsigned)sizeof(link),(unsigned)link.version);
    trace("[15] BEFORE XSetLaunchData");
    DWORD set_status=XSetLaunchData(&link,(DWORD)sizeof(link));
    trace("[15] AFTER XSetLaunchData status=0x%08lX",(unsigned long)set_status);
    if(set_status!=ERROR_SUCCESS)fatal("XSetLaunchData",set_status,link.main_xex);
#endif
    trace("[16] BEFORE XLaunchNewImage path=%s flags=0",link.main_xex);
    trace_close();
    XLaunchNewImage(link.main_xex,0); /* XDK declares VOID and DECLSPEC_NORETURN. */
}
