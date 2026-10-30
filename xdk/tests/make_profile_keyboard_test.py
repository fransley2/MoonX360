from pathlib import Path
import sys
r=Path(__file__).resolve().parents[2];s=(r/'xdk/src/main_xdk.cpp').read_text();a=s.index('int plat_edit_profile_name(');b=s.index('{',a)+1;d=1
while d:d+=(s[b]=='{')-(s[b]=='}');b+=1
code=s[a:b]
fixture=r"""
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include <stdio.h>
#include "../src/config.h"
typedef uint32_t DWORD;typedef wchar_t WCHAR;struct XOVERLAPPED{int unused;};struct XINPUT_STATE{};
#define ZeroMemory(p,n) memset(p,0,n)
#define CP_UTF8 65001
#define VKBD_DEFAULT 0
#define ERROR_SUCCESS 0
#define ERROR_IO_PENDING 997
#define ERROR_CANCELLED 1223
#define ERROR_FUNCTION_FAILED 1627
#define FALSE 0
static bool streaming,guide;static DWORD start_status=ERROR_IO_PENDING,completion,extended;static int calls,polls,presents,sleeps;
static bool video_xdk_is_streaming(){return streaming;}
static bool gfx_system_ui_active(){return guide;}
static void gfx_present_system_ui(){presents++;}
void ui_present(){presents++;}
const char *ui_translate(const char *s){return s;}
static void Sleep(int n){assert(n==16);sleeps++;}
static int MultiByteToWideChar(int,int,const char *s,int,WCHAR *out,int cap){int n=(int)strlen(s);if(n>=cap)return 0;for(int i=0;i<=n;i++)out[i]=(unsigned char)s[i];return n+1;}
static int WideCharToMultiByte(int,int,const WCHAR *s,int,char *out,int cap,void*,void*){int n=(int)wcslen(s);if(n>=cap)return 0;for(int i=0;i<=n;i++)out[i]=(char)s[i];return n+1;}
static DWORD XInputGetState(DWORD user,XINPUT_STATE*){return user==2?ERROR_SUCCESS:1;}
static DWORD XShowKeyboardUI(DWORD user,DWORD flags,const WCHAR*,const WCHAR*,const WCHAR*,WCHAR *out,DWORD cap,XOVERLAPPED*){assert(user==2 && flags==0 && cap==64);calls++;wcscpy(out,L"Lounge");return start_status;}
static bool XHasOverlappedIoCompleted(XOVERLAPPED*){return ++polls>2;}
static DWORD XGetOverlappedResult(XOVERLAPPED*,void*,int wait){assert(!wait);return completion;}
static DWORD XGetOverlappedExtendedError(XOVERLAPPED*){return extended;}
char *storage_read(const char*,size_t*){assert(false);return NULL;}bool storage_write(const char*,const void*,size_t){assert(false);return false;}
/* CODE */
int main(){
 char name[CONFIG_PROFILE_NAME_BYTES]="Before";
 assert(plat_edit_profile_name(name,sizeof(name))==1 && !strcmp(name,"Lounge"));assert(presents==2 && sleeps==2);
 strcpy(name,"Before");polls=0;guide=true;completion=ERROR_CANCELLED;
 assert(plat_edit_profile_name(name,sizeof(name))==0 && !strcmp(name,"Before"));
 polls=0;completion=ERROR_FUNCTION_FAILED;extended=ERROR_CANCELLED;
 assert(plat_edit_profile_name(name,sizeof(name))==0 && !strcmp(name,"Before"));
 start_status=5;assert(plat_edit_profile_name(name,sizeof(name))==-1 && !strcmp(name,"Before"));
 int before=calls;streaming=true;assert(plat_edit_profile_name(name,sizeof(name))==-1 && calls==before);
 puts("PASS: actual XDK keyboard wrapper uses connected controller, asynchronous Presents, accept/cancel/extended cancellation, unavailable fallback and no keyboard while streaming");
}
"""
Path(sys.argv[1]).write_text(fixture.replace('/* CODE */',code))
