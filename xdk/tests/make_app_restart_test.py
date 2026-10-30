from pathlib import Path
import sys
r=Path(__file__).resolve().parents[2]
s=(r/'xdk/src/main_xdk.cpp').read_text(encoding='utf-8')
def function(signature):
 a=s.index(signature);b=s.index('{',a)+1;depth=1
 while depth:depth+=(s[b]=='{')-(s[b]=='}');b+=1
 return s[a:b]
fixture=r'''
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <string>
typedef unsigned DWORD;
#define MAX_PATH 260
#define INVALID_FILE_ATTRIBUTES 0xFFFFFFFFu
#define FILE_ATTRIBUTE_DIRECTORY 16
static DWORD attributes;
static DWORD GetFileAttributesA(const char*){return attributes;}
static bool streaming;static unsigned flushes,launches;
static std::string launched;
static bool video_xdk_is_streaming(){return streaming;}
static void plat_diagnostics_flush(){flushes++;}
static void XLaunchNewImage(const char *path,DWORD flags){assert(flags==0);launches++;launched=path;}
/* CODE */
int main(){
 streaming=true;assert(!plat_restart_available() && !plat_restart_app() && !launches);
 streaming=false;attributes=INVALID_FILE_ATTRIBUTES;assert(!plat_restart_available() && !plat_restart_app() && !launches);
 attributes=FILE_ATTRIBUTE_DIRECTORY;assert(!plat_restart_app() && !launches);
 attributes=0;assert(plat_restart_available());plat_restart_app();
 assert(launches==1 && flushes==1 && launched=="game:\\default.xex");
 puts("PASS: actual restart uses the package default.xex entrypoint, refuses streaming/missing/directories, flushes diagnostics and never requests a console reboot.");
}

'''
Path(sys.argv[1]).write_text(fixture.replace('/* CODE */',function('bool plat_restart_available(')+'\n'+function('bool plat_restart_app(')),encoding='utf-8')
