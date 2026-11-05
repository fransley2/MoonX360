#pragma once
/* Path operations shared with the PC fixture. The MX36 wire format is unchanged. */
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include "aurora_launch.h"

static int aurora_path_normalize(char *out,size_t cap,const char *input,int file)
{
    size_t n=0;
    if(!out || !cap || !input)return 0;
    while(input[n] && n<cap){out[n]=input[n]=='/'?'\\':input[n];n++;}
    if(input[n] || n>=cap)return 0;
    out[n]=0;
    if(file)return moonx_launch_path(out,cap);
    return moonx_launch_folder(out,cap);
}
static int aurora_path_join(char *out,size_t cap,const char *base,const char *name)
{
    size_t n;
    if(!out || !base || !name || !moonx_launch_folder(base,240))return 0;
    if(!name[0] || !strcmp(name,".") || !strcmp(name,"..") || strchr(name,'\\') || strchr(name,'/') || strchr(name,':'))return 0;
    for(n=0;name[n];n++)if((unsigned char)name[n]<32 || strchr("<>\"|?*",name[n]))return 0;
    int written=snprintf(out,cap,"%s%s%s",base,base[strlen(base)-1]=='\\'?"":"\\",name);
    return written>0 && (size_t)written<cap && moonx_launch_folder(out,cap);
}
static int aurora_path_parent(char *path)
{
    if(!path || !moonx_launch_folder(path,240))return 0;
    char *colon=strchr(path,':'),*end=path+strlen(path);
    while(end>colon+2 && end[-1]=='\\')--end;
    if(end==colon+2)return 0;
    while(end>colon+2 && end[-1]!='\\')--end;
    *end=0;
    return 1;
}
static int aurora_path_xex_name(const char *name)
{
    size_t n=name?strlen(name):0;
    if(n<4)return 0;
    return name[n-4]=='.' && (name[n-3]=='x'||name[n-3]=='X') &&
           (name[n-2]=='e'||name[n-2]=='E') && (name[n-1]=='x'||name[n-1]=='X');
}
static int aurora_xex_magic_valid(const char bytes[4])
{return bytes && !memcmp(bytes,"XEX2",4);}
static int aurora_xex_stream_header(FILE *file)
{
    char bytes[4];return file && fread(bytes,1,sizeof(bytes),file)==sizeof(bytes) && aurora_xex_magic_valid(bytes);
}
typedef int (*aurora_path_probe)(const char *path,void *context);
static int aurora_path_detect(char *out,size_t cap,const char *saved,const char *shortcuts,
                              aurora_path_probe probe,void *context)
{
    char normalized[160],parent[160],candidate[160];
    if(!out || cap<16 || !probe)return 0;
    if(saved && aurora_path_normalize(normalized,sizeof(normalized),saved,1) &&
       probe(normalized,context) && strlen(normalized)<cap){snprintf(out,cap,"%s",normalized);return 1;}
    if(shortcuts && aurora_path_normalize(parent,sizeof(parent),shortcuts,0) &&
       aurora_path_parent(parent) && aurora_path_join(candidate,sizeof(candidate),parent,"default.xex") &&
       probe(candidate,context) && strlen(candidate)<cap){snprintf(out,cap,"%s",candidate);return 1;}
    static const char *roots[]={"Hdd1:","Usb0:","Usb1:","Usb2:","Usb3:","Usb4:","Usb5:","Usb6:","Usb7:","Usb8:","Usb9:","UsbMu0:","UsbMu1:","UsbMu2:","UsbMu3:","UsbMu4:","IntMu:"};
    static const char *suffixes[]={"\\Apps\\MoonX360\\default.xex","\\MoonX360\\default.xex","\\default.xex"};
    for(unsigned i=0;i<sizeof(roots)/sizeof(roots[0]);i++)for(unsigned j=0;j<sizeof(suffixes)/sizeof(suffixes[0]);j++){
        int n=snprintf(candidate,sizeof(candidate),"%s%s",roots[i],suffixes[j]);
        if(n>0 && n<(int)sizeof(candidate) && probe(candidate,context) && strlen(candidate)<cap){
            snprintf(out,cap,"%s",candidate);return 1;
        }
    }
    return 0;
}
