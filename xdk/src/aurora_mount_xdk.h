#pragma once
/* Xbox 360 title-local DOS aliases. Never replace an alias owned by another
 * title/dashboard. The shortcut XEX must establish its own mappings too. */
#include <xtl.h>
#include <string.h>
#include "../../src/aurora_mount_map.h"

struct moonx_kernel_string { USHORT Length,MaximumLength; CHAR *Buffer; };
extern "C" LONG ObCreateSymbolicLink(moonx_kernel_string *link,moonx_kernel_string *device);
struct moonx_mount_report { int existed,attempted,accessible; LONG status; char root[20],target[64]; };

static bool moonx_mount_root_accessible(const char *root)
{
    DWORD attr=GetFileAttributesA(root);
    if(attr!=INVALID_FILE_ATTRIBUTES && (attr&FILE_ATTRIBUTE_DIRECTORY))return true;
    char pattern[32];size_t n=strlen(root);
    if(n+2>sizeof(pattern))return false;
    memcpy(pattern,root,n);pattern[n]='*';pattern[n+1]=0;
    WIN32_FIND_DATAA data;HANDLE search=FindFirstFileA(pattern,&data);
    if(search==INVALID_HANDLE_VALUE)return false;
    FindClose(search);return true;
}
static bool moonx_mount_for_path_ex(const char *path,moonx_mount_report *report)
{
    if(report)memset(report,0,sizeof(*report));
    if(!path)return false;
    const char *colon=strchr(path,':');size_t length=colon?(size_t)(colon-path):0;
    if(!colon || !length || length>15)return false;
    char name[16],root[20],target[64],link_name[32];
    memcpy(name,path,length);name[length]=0;
    if(!_stricmp(name,"game")){if(report)report->accessible=1;return true;}
    if(length+3>sizeof(root))return false;
    memcpy(root,name,length);root[length]=':';root[length+1]='\\';root[length+2]=0;
    if(report)memcpy(report->root,root,length+3);
    bool known=moonx_mount_target(name,target,sizeof(target))!=0;
    if(known && report)memcpy(report->target,target,strlen(target)+1);
    if(moonx_mount_root_accessible(root)){if(report){report->existed=1;report->accessible=1;}return true;}
    if(!known)return false;
    static const char prefix[]="\\??\\";size_t prefix_len=sizeof(prefix)-1;
    if(prefix_len+length+2>sizeof(link_name))return false;
    memcpy(link_name,prefix,prefix_len);memcpy(link_name+prefix_len,name,length);
    link_name[prefix_len+length]=':';link_name[prefix_len+length+1]=0;
    moonx_kernel_string link={(USHORT)strlen(link_name),(USHORT)(strlen(link_name)+1),link_name};
    moonx_kernel_string device={(USHORT)strlen(target),(USHORT)(strlen(target)+1),target};
    /* A collision is left untouched. It may belong to the dashboard. */
    LONG status=ObCreateSymbolicLink(&link,&device);
    bool accessible=moonx_mount_root_accessible(root);
    if(report){report->attempted=1;report->status=status;report->accessible=accessible?1:0;}
    return accessible;
}
static bool moonx_mount_for_path(const char *path)
{return moonx_mount_for_path_ex(path,NULL);}
