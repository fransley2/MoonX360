#pragma once
/* Known Xbox 360 storage device targets. No I/O or namespace mutation here. */
#include <stddef.h>
#include <string.h>

static int moonx_mount_target(const char *name,char *target,size_t capacity)
{
    static const char hdd[]="\\Device\\Harddisk0\\Partition1";
    static const char mass[]="\\Device\\Mass";
    static const char storage[]="PartitionFile\\Storage";
    if(!name || !target)return 0;
    size_t length=strlen(name);
    if(length==4 && (name[0]=='H'||name[0]=='h') &&
       (name[1]=='D'||name[1]=='d') && (name[2]=='D'||name[2]=='d') && name[3]=='1' && !name[4]){
        if(capacity<sizeof(hdd))return 0;
        memcpy(target,hdd,sizeof(hdd));return 1;
    }
    char digit=0;int memory_unit=0;
    if(length>=4 && (name[0]=='U'||name[0]=='u') && (name[1]=='S'||name[1]=='s') &&
       (name[2]=='B'||name[2]=='b')){
        if(length==6 && (name[3]=='M'||name[3]=='m') && (name[4]=='U'||name[4]=='u') &&
           name[5]>='0' && name[5]<='9' && !name[6]){digit=name[5];memory_unit=1;}
        else if(length==4 && name[3]>='0' && name[3]<='9' && !name[4])digit=name[3];
    }
    if(!digit)return 0;
    size_t prefix=sizeof(mass)-1,extra=memory_unit?sizeof(storage)-1:0;
    if(capacity<prefix+1+extra+1)return 0;
    memcpy(target,mass,prefix);target[prefix]=digit;
    if(memory_unit)memcpy(target+prefix+1,storage,extra);
    target[prefix+1+extra]=0;return 1;
}
