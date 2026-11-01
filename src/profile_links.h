#pragma once
#include "product_features.h"
#ifndef PROFILE_LINK_LOG
#define PROFILE_LINK_LOG(text) ((void)0)
#endif
#define PROFILE_TXN_BYTES 34816
static const char *profile_link_stage="idle";
static bool profile_link_fail(const char *stage)
{
    profile_link_stage=stage;char text[160];
    snprintf(text,sizeof(text),"[PROFILE] failed phase=%s errno=%d\n",stage,errno);PROFILE_LINK_LOG(text);return false;
}
static bool profile_link_write(const char *path,const void *data,size_t bytes)
{
    if(!storage_write(path,data,bytes))return false;
    size_t n=0;char *read=storage_read(path,&n);
    bool ok=read && n==bytes && (!bytes || !memcmp(read,data,bytes));free(read);
    if(!ok)errno=EIO;return ok;
}
static const char *profile_txn_files[]={"profiles-a.tsv","profiles-b.tsv","game-bindings.tsv","moonlight.ini"};
/* Undo journal: failures restore the previous durable configuration. A pending
 * v2 journal is rolled back before startup publishes any profile state. */
static bool profile_txn_restore(const char *text,size_t bytes)
{
    static const char header[]="MOONX_PROFILE_TXN 2\n";size_t sizes[4];
    const char *p=text+sizeof(header)-1,*end=text+bytes;
    if(bytes<sizeof(header)+18 || memcmp(text,header,sizeof(header)-1))return profile_link_fail("undo-header");
    for(int i=0;i<4;i++) {
        unsigned n=0;if(p>=end || *p<'0' || *p>'9')return profile_link_fail("undo-length");
        while(p<end && *p>='0' && *p<='9'){n=n*10+(*p++-'0');if(n>8192)return profile_link_fail("undo-bound");}
        if(p>=end || *p++!=(i==3?'\n':' '))return profile_link_fail("undo-delimiter");sizes[i]=n;
    }
    size_t data_bytes=0;for(int i=0;i<4;i++)data_bytes+=sizes[i];
    unsigned hash;
    if((size_t)(end-p)!=data_bytes+18 || memcmp(p+data_bytes,"CHECKSUM ",9) ||
       !feature_checksum_read(p+data_bytes+9,9,&hash) || hash!=feature_checksum(text,(size_t)(p-text)+data_bytes))return profile_link_fail("undo-checksum");
    for(int i=0;i<4;i++){if(!profile_link_write(profile_txn_files[i],p,sizes[i]))return profile_link_fail(profile_txn_files[i]);p+=sizes[i];}
    if(!profile_link_write("profile-link-pending.tsv","",0))return profile_link_fail("undo-clear");
    PROFILE_LINK_LOG("[PROFILE] previous durable configuration restored\n");return true;
}
static char *profile_txn_snapshot(const struct config *c,size_t *bytes)
{
    char *journal=NULL,*parts[4]={0};size_t sizes[4]={0};
    bool ok=true;
    for(int i=0;i<4;i++) {
        errno=0;parts[i]=storage_read(profile_txn_files[i],sizes+i);
        if(!parts[i] && errno!=ENOENT){ok=false;break;}
        if(!parts[i]) {
            parts[i]=(char*)malloc(8192);if(!parts[i]){ok=false;errno=ENOMEM;break;}
            if(i<2)sizes[i]=0;
            else if(i==2){struct game_bindings empty;memset(&empty,0,sizeof(empty));sizes[i]=bindings_encode(&empty,parts[i],8192);}
            else sizes[i]=config_serialize(c,parts[i],8192);
        }
        if(sizes[i]>8192){ok=false;errno=ERANGE;break;}
    }
    size_t used=0,capacity=96;for(int i=0;i<4;i++)capacity+=sizes[i];
    if(ok){journal=(char*)malloc(capacity);if(!journal){ok=false;errno=ENOMEM;}}
    if(ok) {
        used=(size_t)snprintf(journal,capacity,"MOONX_PROFILE_TXN 2\n%u %u %u %u\n",(unsigned)sizes[0],(unsigned)sizes[1],(unsigned)sizes[2],(unsigned)sizes[3]);
        for(int i=0;i<4;i++){memcpy(journal+used,parts[i],sizes[i]);used+=sizes[i];}
        used+=(size_t)snprintf(journal+used,capacity-used,"CHECKSUM %08x\n",feature_checksum(journal,used));
        ok=used<capacity && used<PROFILE_TXN_BYTES;
    }
    for(int i=0;i<4;i++)free(parts[i]);
    if(!ok){free(journal);profile_link_fail("undo-snapshot");return NULL;}
    *bytes=used;return journal;
}

/* Small write-ahead intent for multi-file rename/delete. The existing A/B
 * checksummed profile snapshots remain authoritative. Replay is idempotent;
 * partial writes never publish successful state in memory. */
static bool profile_links_finish(struct user_profiles *p,struct game_bindings *b,
                                 struct config *c,const char *old,const char *name)
{
    if(b->read_only || p->read_only)return false;
    struct user_profiles next=*p;struct game_bindings links=*b;struct config settings=*c;
    int index=-1,renamed=-1;
    for(int i=0;i<next.count;i++) {
        if(!strcmp(next.items[i].name,old))index=i;
        if(!strcmp(next.items[i].name,name))renamed=i;
    }
    if(index>=0) {
        if(*name) {if(renamed>=0 && renamed!=index)return false;strcpy(next.items[index].name,name);}
        else {memmove(next.items+index,next.items+index+1,(next.count-index-1)*sizeof(next.items[0]));next.count--;}
    } else if(*name && renamed<0)return false;
    char token[80];snprintf(token,sizeof(token),"user:%s",old);
    for(int i=0;i<links.count;i++)if(!strcmp(links.items[i].profile,token)) {
        if(*name)snprintf(links.items[i].profile,sizeof(links.items[i].profile),"user:%s",name);
        else {memmove(links.items+i,links.items+i+1,(links.count-i-1)*sizeof(links.items[0]));links.count--;i--;}
    }
    if(!strcmp(settings.user_profile,old)){if(*name)strcpy(settings.user_profile,name);else config_select_factory(&settings,CONFIG_PROFILE_SMOOTH);}
    char text[8192];size_t n=bindings_encode(&links,text,sizeof(text));
    if(!n || !config_profiles_save(p,&next))return false;
    /* p is a caller-owned staging copy until the whole operation succeeds. */
    if(!storage_write("game-bindings.tsv",text,n) || !config_save(&settings) ||
       !storage_write("profile-link-pending.tsv","",0))return false;
    *b=links;*c=settings;return true;
}
static bool profile_links_recover(struct user_profiles *p,struct game_bindings *b,struct config *c)
{
    size_t bytes=0;errno=0;char *text=storage_read("profile-link-pending.tsv",&bytes);
    if(!text)return errno==ENOENT;
    if(!bytes){free(text);return true;}
    if(bytes>=19 && !memcmp(text,"MOONX_PROFILE_TXN 2\n",19)) {
        bool restored=profile_txn_restore(text,bytes);free(text);if(!restored)return false;
        struct user_profiles profiles;memset(&profiles,0,sizeof(profiles));profiles.slot=-1;
        struct game_bindings links;char *raw=storage_read("game-bindings.tsv",NULL);
        bool loaded=config_profiles_load(&profiles) && bindings_decode(&links,raw);free(raw);
        if(!loaded)return profile_link_fail("undo-reload");
        struct config settings;config_load(&settings);*p=profiles;*b=links;*c=settings;return true;
    }
    char old[64],name[64];unsigned hash;bool ok=false;
    /* Empty second line denotes deletion; names forbid LF. */
    char *line=strchr(text,'\n'),*end=line?strchr(line+1,'\n'):NULL;
    if(line && end && line-text>0 && line-text<64 && end-line>0 && end-line<65 &&
       end+1<=text+bytes && feature_checksum_read(end+1,(size_t)(text+bytes-end-1),&hash) &&
       hash==feature_checksum(text,end+1-text)) {
        memcpy(old,text,line-text);old[line-text]=0;
        memcpy(name,line+1,end-line-1);name[end-line-1]=0;
        if(config_profile_name_valid(old) && (!*name || config_profile_name_valid(name))) {
            struct user_profiles next=*p;struct game_bindings links=*b;struct config settings=*c;
            ok=profile_links_finish(&next,&links,&settings,old,name);
            if(ok){*p=next;*b=links;*c=settings;}
        }
    }
    free(text);return ok;
}
static bool profile_links_pending(void)
{
    size_t n=0;errno=0;char *text=storage_read("profile-link-pending.tsv",&n);
    bool pending=text?n!=0:errno!=ENOENT;free(text);return pending;
}
static bool profile_links_change(struct user_profiles *p,struct game_bindings *b,
                                struct config *c,int index,const char *name)
{
    if(profile_links_pending() || index<0 || index>=p->count || p->read_only || b->read_only || p->generation==0xffffffffu ||
       (*name && (!config_profile_name_valid(name))))return profile_link_fail("validate-user-profile");
    for(int i=0;i<p->count;i++)if(i!=index && !strcmp(p->items[i].name,name))return false;
    if(!strcmp(p->items[index].name,name))return true;
    struct user_profiles next=*p;struct game_bindings links=*b;struct config settings=*c;
    bool active=config_profiles_selected(p,c)==index;
    char old[64];strcpy(old,p->items[index].name);
    if(*name)strcpy(next.items[index].name,name);
    else {memmove(next.items+index,next.items+index+1,(next.count-index-1)*sizeof(next.items[0]));next.count--;}
    char token[80];snprintf(token,sizeof(token),"user:%s",old);
    for(int i=0;i<links.count;i++)if(!strcmp(links.items[i].profile,token)) {
        if(*name)snprintf(links.items[i].profile,sizeof(links.items[i].profile),"user:%s",name);
        else {memmove(links.items+i,links.items+i+1,(links.count-i-1)*sizeof(links.items[0]));links.count--;i--;}
    }
    if(active || !strcmp(settings.user_profile,old)) {
        if(*name)strcpy(settings.user_profile,name);
        else config_select_factory(&settings,CONFIG_PROFILE_SMOOTH);
    }
    char binding_text[8192],ini[2048],profile_text[CONFIG_PROFILES_BYTES];
    size_t binding_bytes=bindings_encode(&links,binding_text,sizeof(binding_text));
    size_t ini_bytes=config_serialize(&settings,ini,sizeof(ini));next.generation=p->generation+1;
    if(!binding_bytes || !ini_bytes || !config_profiles_serialize(&next,profile_text,sizeof(profile_text)))return profile_link_fail("serialize");
    size_t journal_bytes=0;char *journal=profile_txn_snapshot(c,&journal_bytes);if(!journal)return false;
    bool ok=profile_link_write("profile-link-pending.tsv",journal,journal_bytes);const char *stage="write-undo";
    struct user_profiles staged=*p;
    if(ok){stage="write-profiles";ok=config_profiles_save(&staged,&next);}
    if(ok){stage="write-bindings";ok=profile_link_write("game-bindings.tsv",binding_text,binding_bytes);}
    if(ok){stage="write-configuration";ok=profile_link_write("moonlight.ini",ini,ini_bytes);}
    if(ok){stage="commit-clear";ok=profile_link_write("profile-link-pending.tsv","",0);}
    if(!ok) {
        int error=errno;profile_link_fail(stage);
        /* Even a reported close/verification failure may have published bytes. */
        bool restored=profile_txn_restore(journal,journal_bytes);free(journal);
        if(!restored)return false;errno=error;return profile_link_fail(stage);
    }
    free(journal);*p=staged;*b=links;*c=settings;profile_link_stage="committed";
    PROFILE_LINK_LOG("[PROFILE] committed collection, bindings and active selection\n");return true;
}
