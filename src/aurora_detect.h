#pragma once
/* Bounded Aurora.xex search, with injected filesystem functions for PC tests. */
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "aurora_paths.h"

#define MOONX_AURORA_SEARCH_LIMIT 320
#define MOONX_AURORA_SEARCH_DEPTH 4
#define MOONX_AURORA_DIR_ENTRIES 192

struct moonx_aurora_search_io {
    int (*valid_xex)(const char *path,void *context);
    int (*list_dirs)(const char *folder,char names[][64],int capacity,void *context);
    int (*cancelled)(void *context);
    void (*checked)(const char *path,int valid,void *context);
    void (*phase)(int stage,void *context);
    void *context;
};
static int moonx_aurora_case_eq(const char *a,const char *b)
{
    if(!a || !b)return 0;
    while(*a && *b){char x=*a++,y=*b++;
        if(x>='A'&&x<='Z')x=(char)(x+32);
        if(y>='A'&&y<='Z')y=(char)(y+32);
        if(x!=y)return 0;}
    return !*a && !*b;
}
static int moonx_aurora_skip_dir(const char *name)
{
    static const char *skip[]={"Content","Games","Cache","Shortcuts","$SystemUpdate","System Volume Information"};
    if(!name || !name[0] || !strcmp(name,".") || !strcmp(name,".."))return 1;
    for(unsigned i=0;i<sizeof(skip)/sizeof(skip[0]);i++)if(moonx_aurora_case_eq(name,skip[i]))return 1;
    return 0;
}
static int moonx_aurora_check(const char *folder,const moonx_aurora_search_io *io)
{
    char xex[160];
    if(!aurora_path_join(xex,sizeof(xex),folder,"Aurora.xex"))return 0;
    int valid=io->valid_xex(xex,io->context);
    if(io->checked)io->checked(xex,valid,io->context);
    return valid;
}
static int moonx_aurora_find(char *out,size_t cap,const char *const *roots,int root_count,
                             const moonx_aurora_search_io *io)
{
    if(!out || cap<16 || !roots || root_count<0 || !io || !io->valid_xex || !io->list_dirs)return 0;
    static const char *known[]={"Aurora\\","Apps\\Aurora\\","Applications\\Aurora\\","Homebrew\\Aurora\\"};
    char folder[160];
    for(int r=0;r<root_count;r++)for(unsigned k=0;k<sizeof(known)/sizeof(known[0]);k++){
        if(io->cancelled && io->cancelled(io->context))return -1;
        if(!roots[r])continue;
        int n=snprintf(folder,sizeof(folder),"%s%s",roots[r],known[k]);
        if(n<=0 || n>=(int)sizeof(folder))continue;
        if(moonx_aurora_check(folder,io) && strlen(folder)<cap){snprintf(out,cap,"%s",folder);return 1;}
    }
    if(io->phase)io->phase(2,io->context);
    struct moonx_aurora_queue_entry { char folder[160];unsigned depth; };
    moonx_aurora_queue_entry queue[MOONX_AURORA_SEARCH_LIMIT];
    int head=0,tail=0;
    for(int r=0;r<root_count && tail<MOONX_AURORA_SEARCH_LIMIT;r++){
        if(!roots[r] || strlen(roots[r])>=sizeof(queue[0].folder))continue;
        snprintf(queue[tail].folder,sizeof(queue[tail].folder),"%s",roots[r]);queue[tail++].depth=0;
    }
    while(head<tail){
        if(io->cancelled && io->cancelled(io->context))return -1;
        moonx_aurora_queue_entry current=queue[head++];
        if(moonx_aurora_check(current.folder,io) && strlen(current.folder)<cap){
            snprintf(out,cap,"%s",current.folder);return 1;
        }
        if(current.depth>=MOONX_AURORA_SEARCH_DEPTH)continue;
        char names[MOONX_AURORA_DIR_ENTRIES][64];
        int count=io->list_dirs(current.folder,names,MOONX_AURORA_DIR_ENTRIES,io->context);
        if(count<0)continue;
        if(count>MOONX_AURORA_DIR_ENTRIES)count=MOONX_AURORA_DIR_ENTRIES;
        for(int i=0;i<count && tail<MOONX_AURORA_SEARCH_LIMIT;i++){
            if(moonx_aurora_skip_dir(names[i]))continue;
            if(!aurora_path_join(folder,sizeof(folder),current.folder,names[i]))continue;
            snprintf(queue[tail].folder,sizeof(queue[tail].folder),"%s",folder);
            queue[tail++].depth=current.depth+1;
        }
    }
    return 0;
}
