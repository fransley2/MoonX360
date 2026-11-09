#include "xdk.h"
#include "aurora_mount_xdk.h"
#include "../../src/aurora_shortcuts.h"
#include "../../src/aurora_index.h"
#include "../../src/aurora_link_io.h"
#include "../../src/aurora_paths.h"
#include "../../src/aurora_detect.h"
#include "../../src/platform.h"
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static struct aurora_index_file index_file;
typedef char moonx_writer_abi_check[(sizeof(moonx_launch_data)==468 &&
    offsetof(moonx_launch_data,checksum)==464)?1:-1];
static bool loaded,read_only;
static bool regular(const char *path)
{if(!moonx_mount_for_path(path))return false;DWORD a=GetFileAttributesA(path);return a!=INVALID_FILE_ATTRIBUTES && !(a&FILE_ATTRIBUTE_DIRECTORY);}
static bool directory(const char *path)
{if(!moonx_mount_for_path(path))return false;DWORD a=GetFileAttributesA(path);return a!=INVALID_FILE_ATTRIBUTES && (a&FILE_ATTRIBUTE_DIRECTORY);}
static bool xex_header(const char *path)
{
    if(!path || (_stricmp(path,"game:\\default.xex") && !moonx_launch_path(path,160)) ||
       !moonx_mount_for_path(path))return false;
    FILE *f=fopen(path,"rb");if(!f)return false;
    bool ok=aurora_xex_stream_header(f)!=0;
    fclose(f);return ok;
}
bool aurora_shortcut_valid_xex(const char *path){return xex_header(path);}
bool aurora_shortcut_folder_ready(const char *path)
{return path && moonx_launch_folder(path,160) && directory(path);}
/* Compare the candidate against the currently running image, not another
 * default.xex with a plausible name. This is a cold settings operation. */
static int current_identity(const char *path)
{
    if(!xex_header(path) || !xex_header("game:\\default.xex"))return 0;
    HANDLE current=CreateFileA("game:\\default.xex",GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
    HANDLE candidate=CreateFileA(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
    int result=-1;
    if(current!=INVALID_HANDLE_VALUE && candidate!=INVALID_HANDLE_VALUE){
        BY_HANDLE_FILE_INFORMATION a,b;
        if(GetFileInformationByHandle(current,&a) && GetFileInformationByHandle(candidate,&b) &&
           (a.nFileIndexHigh || a.nFileIndexLow) && (b.nFileIndexHigh || b.nFileIndexLow)){
            result=a.dwVolumeSerialNumber==b.dwVolumeSerialNumber &&
                      a.nFileIndexHigh==b.nFileIndexHigh && a.nFileIndexLow==b.nFileIndexLow;
        }
    }
    if(current!=INVALID_HANDLE_VALUE)CloseHandle(current);
    if(candidate!=INVALID_HANDLE_VALUE)CloseHandle(candidate);
    return result;
}
static bool current_image(const char *path)
{
    int identity=current_identity(path);
    if(identity>=0)return identity!=0;
    /* Some FATX implementations omit file indices. A user-selected path can
     * still be checked against the loaded binary; autodetect does not trust it. */
    FILE *a=fopen(path,"rb"),*b=fopen("game:\\default.xex","rb");
    if(!a || !b){if(a)fclose(a);if(b)fclose(b);return false;}
    bool same=true;char left[4096],right[4096];size_t n,m;
    do {
        n=fread(left,1,sizeof(left),a);m=fread(right,1,sizeof(right),b);
        if(n!=m || memcmp(left,right,n)){same=false;break;}
    } while(n==sizeof(left));
    if(ferror(a) || ferror(b))same=false;
    fclose(a);fclose(b);return same;
}
bool aurora_shortcut_is_current_xex(const char *path){return current_image(path);}
static int current_probe(const char *path,void *){return regular(path) && current_identity(path)==1;}
bool aurora_shortcut_autodetect_main(char *out,size_t cap,const char *saved,const char *shortcuts)
{
    return aurora_path_detect(out,cap,saved,shortcuts,current_probe,NULL)!=0;
}
int aurora_browser_roots(struct aurora_browser_entry *items,int capacity)
{
    if(!items || capacity<1)return -1;
    static const char *names[]={"Hdd1","Usb0","Usb1","Usb2","Usb3","Usb4","Usb5","Usb6","Usb7","Usb8","Usb9","UsbMu0","UsbMu1","UsbMu2","UsbMu3","UsbMu4","IntMu","Mu0","Mu1"};
    int n=0;char path[32];
    for(unsigned i=0;i<sizeof(names)/sizeof(names[0]);i++){
        snprintf(path,sizeof(path),"%s:\\",names[i]);
        if(!directory(path))continue;
        if(n==capacity)return -2;
        snprintf(items[n].name,sizeof(items[n].name),"%s",names[i]);items[n++].folder=1;
    }
    return n;
}
int aurora_browser_list(const char *folder,int mode,struct aurora_browser_entry *items,int capacity)
{
    if(!items || capacity<1 || !aurora_shortcut_folder_ready(folder))return -1;
    char pattern[164];int length=snprintf(pattern,sizeof(pattern),"%s%s*",folder,folder[strlen(folder)-1]=='\\'?"":"\\");
    if(length<0 || length>=(int)sizeof(pattern))return -1;
    WIN32_FIND_DATAA data;HANDLE search=FindFirstFileA(pattern,&data);
    if(search==INVALID_HANDLE_VALUE){DWORD error=GetLastError();return error==ERROR_FILE_NOT_FOUND || error==ERROR_NO_MORE_FILES?0:-1;}
    int n=0,overflow=0;
    do {
        const char *name=data.cFileName;int is_folder=(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)?1:0;
        if(!strcmp(name,".") || !strcmp(name,"..") || (!is_folder && mode==AURORA_BROWSER_FOLDER) ||
           (!is_folder && !(mode==AURORA_BROWSER_IMAGE?aurora_path_image_name(name):aurora_path_xex_name(name))) || strlen(name)>=sizeof(items[0].name))continue;
        char path[160];if(!aurora_path_join(path,sizeof(path),folder,name))continue;
        if(n==capacity){overflow=1;continue;}
        int at=n++;
        while(at>0 && (items[at-1].folder<is_folder ||
              (items[at-1].folder==is_folder && _stricmp(items[at-1].name,name)>0))){items[at]=items[at-1];at--;}
        snprintf(items[at].name,sizeof(items[at].name),"%s",name);items[at].folder=is_folder?1:0;
    }while(FindNextFileA(search,&data));
    FindClose(search);return overflow?-2:n;
}
bool aurora_browser_create_folder(const char *parent,const char *name,char *out,size_t cap)
{
    char path[160];
    if(!aurora_shortcut_folder_ready(parent) || !aurora_path_join(path,sizeof(path),parent,name) ||
       strlen(path)>=cap || !CreateDirectoryA(path,NULL))return false;
    snprintf(out,cap,"%s",path);return true;
}
static void load_index(void)
{
    if(loaded)return;loaded=true;
    memset(&index_file,0,sizeof(index_file));memcpy(index_file.magic,"MX36IDX1",8);index_file.version=1;
    size_t n=0;char *raw=storage_read("aurora-shortcuts.dat",&n);
    if(!raw && errno==ENOENT)return;
    if(raw && moonx_index_valid((const struct aurora_index_file*)raw,n)) {
        memcpy(&index_file,raw,sizeof(index_file));free(raw);return;
    }
    free(raw);n=0;raw=storage_read("aurora-shortcuts.dat.bak",&n);
    if(raw && moonx_index_valid((const struct aurora_index_file*)raw,n)) {
        memcpy(&index_file,raw,sizeof(index_file));free(raw);
        plat_log("[Aurora] Shortcut index recovered from backup\n");return;
    }
    free(raw);read_only=true;
    plat_log("[Aurora] Shortcut index corrupt; management locked to preserve files\n");
}
static bool save_index(void)
{
    if(read_only)return false;
    index_file.checksum=moonx_index_hash(&index_file);
    return storage_write("aurora-shortcuts.dat",&index_file,sizeof(index_file));
}
int aurora_shortcut_count(void){load_index();return (int)index_file.count;}
const struct aurora_shortcut_info *aurora_shortcut_at(int i)
{load_index();return i>=0 && i<(int)index_file.count?index_file.items+i:NULL;}
int aurora_shortcut_find(const char *host,int app)
{
    load_index();return moonx_index_find(&index_file,host,app);
}
static bool child_path(char *out,size_t cap,const char *folder,const char *name)
{int n=snprintf(out,cap,"%s%s%s",folder,folder[strlen(folder)-1]=='\\'?"":"\\",name);return n>0 && (size_t)n<cap;}
static bool metadata_path(char out[260],const struct aurora_shortcut_info *r)
{return r && child_path(out,260,r->folder,"moonx360.meta");}
static void metadata_default(struct moonx_aurora_metadata *m,const struct moonx_launch_data *link)
{
    memset(m,0,sizeof(*m));snprintf(m->host_id,sizeof(m->host_id),"%s",link->host_id);
    m->app_id=link->app_id;snprintf(m->display_title,sizeof(m->display_title),"%s",link->title);
    snprintf(m->description,sizeof(m->description),"PC game streamed through MoonX360.");
    snprintf(m->source,sizeof(m->source),"none");moonx_aurora_metadata_seal(m);
}
bool aurora_shortcut_metadata(int i,struct moonx_aurora_metadata *out)
{
    const struct aurora_shortcut_info *r=aurora_shortcut_at(i);
    if(!r || !out)return false;
    metadata_default(out,&r->link);
    char path[260];if(!metadata_path(path,r))return false;
    for(int attempt=0;attempt<2;attempt++){
        char candidate[272];snprintf(candidate,sizeof(candidate),"%s%s",path,attempt?".bak":"");
        FILE *f=fopen(candidate,"rb");if(!f)continue; /* v1 shortcut migration */
        struct moonx_aurora_metadata read;size_t n=fread(&read,1,sizeof(read),f);
        int extra=fgetc(f),read_error=ferror(f);fclose(f);
        if(n==sizeof(read) && extra==EOF && !read_error && moonx_aurora_metadata_valid(&read,n,&r->link)){
            *out=read;break;
        }
    }
    return true;
}
static bool metadata_write(const struct aurora_shortcut_info *r,const struct moonx_aurora_metadata *m)
{
    char path[260],temp[272],backup[272];
    if(!metadata_path(path,r) || snprintf(temp,sizeof(temp),"%s.new",path)>=(int)sizeof(temp) ||
       snprintf(backup,sizeof(backup),"%s.bak",path)>=(int)sizeof(backup))return false;
    FILE *f=fopen(temp,"wb");if(!f)return false;
    bool ok=fwrite(m,1,sizeof(*m),f)==sizeof(*m) && fflush(f)==0;
    if(fclose(f))ok=false;
    if(!ok){DeleteFileA(temp);return false;}
    f=fopen(temp,"rb");struct moonx_aurora_metadata check;
    ok=f && fread(&check,1,sizeof(check),f)==sizeof(check) && fgetc(f)==EOF &&
        !ferror(f) && !memcmp(&check,m,sizeof(check)) &&
        moonx_aurora_metadata_valid(&check,sizeof(check),&r->link);
    if(f)fclose(f);
    if(!ok){DeleteFileA(temp);return false;}
    bool had_old=regular(path);
    if(had_old && regular(backup))DeleteFileA(backup); /* completed prior replacement */
    if(had_old && !MoveFileA(path,backup)){DeleteFileA(temp);return false;}
    if(!MoveFileA(temp,path)){
        if(had_old)MoveFileA(backup,path);
        DeleteFileA(temp);return false;
    }
    if(had_old)DeleteFileA(backup);
    return true;
}
bool aurora_shortcut_update_metadata(int i,const char *title,const char *description,
                                     uint32_t title_id,char *error,size_t cap)
{
    const struct aurora_shortcut_info *r=aurora_shortcut_at(i);
    if(!r || !aurora_shortcut_exists(i) || !moonx_launch_text(title,128,1) ||
       !moonx_launch_text(description,256,0))
        {snprintf(error,cap,"Shortcut or display metadata is invalid.");return false;}
    struct moonx_aurora_metadata m;
    aurora_shortcut_metadata(i,&m);
    snprintf(m.display_title,sizeof(m.display_title),"%s",title);
    snprintf(m.description,sizeof(m.description),"%s",description);
    m.metadata_title_id=title_id;
    snprintf(m.source,sizeof(m.source),"%s",title_id?"manual":"none");
    moonx_aurora_metadata_seal(&m);
    if(!metadata_write(r,&m)){snprintf(error,cap,"Could not save Aurora metadata.");return false;}
    snprintf(error,cap,"Metadata saved. Run MoonX360Sync in Aurora.");return true;
}
bool aurora_shortcut_refresh_metadata(int i,uint32_t title_id,char *error,size_t cap)
{
    struct moonx_aurora_metadata m;
    if(!aurora_shortcut_metadata(i,&m)){snprintf(error,cap,"Shortcut not indexed.");return false;}
    return aurora_shortcut_update_metadata(i,m.display_title,m.description,title_id,error,cap);
}
static bool clean_input(const char *s,size_t max,bool required)
{
    if(!s)return false;size_t n=strlen(s);
    if(n>=max || (required && !n))return false;
    for(size_t i=0;i<n;i++)if((unsigned char)s[i]<32 || s[i]==127)return false;
    return true;
}
static bool ensure_tree(const char *path)
{
    char work[240];size_t n=strlen(path);
    if(n>=sizeof(work) || !moonx_launch_folder(path,240))return false;
    memcpy(work,path,n+1);
    char *colon=strchr(work,':');
    for(char *p=colon+2;*p;p++)if(*p=='\\'){
        char save=*p;*p=0;
        if(!directory(work) && !CreateDirectoryA(work,NULL)){*p=save;return false;}
        *p=save;
    }
    return directory(work) || CreateDirectoryA(work,NULL)!=0;
}
static bool write_link(const char *path,const struct moonx_launch_data *link)
{
    char temp[260];if(snprintf(temp,sizeof(temp),"%s.new",path)>=(int)sizeof(temp))return false;
    FILE *f=fopen(temp,"wb");if(!f)return false;
    bool ok=moonx_link_write_payload(f,link)==sizeof(*link) && fflush(f)==0;
    if(fclose(f))ok=false;
    if(!ok){DeleteFileA(temp);return false;}
    if(!MoveFileA(temp,path)){DeleteFileA(temp);return false;}
    return true;
}
static bool link_matches(int i)
{
    const struct aurora_shortcut_info *r=aurora_shortcut_at(i);
    if(!r)return false;
    char link[260];
    if(!child_path(link,sizeof(link),r->folder,"moonx360.link"))return false;
    FILE *f=fopen(link,"rb");if(!f)return false;
    struct moonx_launch_data actual;size_t n=fread(&actual,1,sizeof(actual),f);
    int extra=fgetc(f);int read_error=ferror(f);fclose(f);
    return n==sizeof(actual) && extra==EOF && !read_error && !memcmp(&actual,&r->link,sizeof(actual));
}
bool aurora_shortcut_exists(int i)
{
    const struct aurora_shortcut_info *r=aurora_shortcut_at(i);
    if(!r || !link_matches(i))return false;
    char xex[260];return child_path(xex,sizeof(xex),r->folder,"default.xex") && regular(xex);
}
bool aurora_shortcut_create(const struct config *cfg,const char *host,int app,
                            const char *name,const char *profile,char *error,size_t cap)
{
    load_index();
    if(!cfg || !cfg->aurora_enabled || read_only || !host || !name || !profile || app<=0 ||
       strlen(host)>=80 || strlen(name)>=128 || strlen(profile)>=80 ||
       !clean_input(host,80,true) || !clean_input(name,128,true) ||
       !clean_input(profile,80,false) || !moonx_launch_folder(cfg->aurora_shortcuts_folder,sizeof(cfg->aurora_shortcuts_folder)))
        {snprintf(error,cap,"Invalid shortcut data or read-only index.");return false;}
    int existing=aurora_shortcut_find(host,app);
    if(existing>=0){snprintf(error,cap,aurora_shortcut_exists(existing)?"Shortcut already exists.":"Shortcut is indexed but files are missing. Clean the index first.");return false;}
    if(index_file.count==AURORA_SHORTCUT_MAX){snprintf(error,cap,"Shortcut limit reached.");return false;}
    char main_xex[160];snprintf(main_xex,sizeof(main_xex),"%s",cfg->aurora_main_xex);
    if(!moonx_launch_path(main_xex,sizeof(main_xex)) || !regular(main_xex))
        {snprintf(error,cap,"MoonX360 XEX not found: %s. Select the XEX in Advanced > Aurora Integration.",main_xex);return false;}
    if(!xex_header(main_xex) || !current_image(main_xex))
        {snprintf(error,cap,"MoonX360 XEX path is not the running main XEX: %s",main_xex);return false;}
    const char *template_xex="game:\\MoonX360Shortcut\\default.xex";
    if(!regular(template_xex)){snprintf(error,cap,"Install MoonX360Shortcut\\default.xex beside MoonX360.");return false;}
    char leaf[64],folder[240],link_path[260],xex_path[260];
    moonx_shortcut_leaf(leaf,name,host,app);
    if(!child_path(folder,sizeof(folder),cfg->aurora_shortcuts_folder,leaf) ||
       !child_path(link_path,sizeof(link_path),folder,"moonx360.link") ||
       !child_path(xex_path,sizeof(xex_path),folder,"default.xex"))
        {snprintf(error,cap,"Shortcut folder path is too long.");return false;}
    if(directory(folder) || regular(link_path) || regular(xex_path))
        {snprintf(error,cap,"Shortcut folder already exists outside the index.");return false;}
    if(!ensure_tree(cfg->aurora_shortcuts_folder) || !CreateDirectoryA(folder,NULL))
        {snprintf(error,cap,"Could not create the shortcut folder.");return false;}
    struct aurora_shortcut_info record;memset(&record,0,sizeof(record));
    snprintf(record.folder,sizeof(record.folder),"%s",folder);
    snprintf(record.link.host_id,sizeof(record.link.host_id),"%s",host);
    record.link.app_id=app;
    snprintf(record.link.profile_id,sizeof(record.link.profile_id),"%s",profile);
    snprintf(record.link.title,sizeof(record.link.title),"%s",name);
    snprintf(record.link.main_xex,sizeof(record.link.main_xex),"%s",main_xex);
    moonx_launch_seal(&record.link);
    if(!CopyFileA(template_xex,xex_path,TRUE) || !write_link(link_path,&record.link)) {
        DeleteFileA(xex_path);DeleteFileA(link_path);RemoveDirectoryA(folder);
        snprintf(error,cap,"Could not write shortcut files.");return false;
    }
    index_file.items[index_file.count++]=record;
    if(!save_index()) {
        memset(&index_file.items[--index_file.count],0,sizeof(index_file.items[0]));
        DeleteFileA(link_path);DeleteFileA(xex_path);RemoveDirectoryA(folder);
        snprintf(error,cap,"Could not save shortcut index.");return false;
    }
    struct moonx_aurora_metadata metadata;metadata_default(&metadata,&record.link);
    bool metadata_saved=metadata_write(&record,&metadata);
    snprintf(error,cap,metadata_saved?"Shortcut created. Run MoonX360Sync after Aurora rescan.":
             "Shortcut created; Aurora metadata file could not be saved.");return true;
}
bool aurora_shortcut_delete(int i,char *error,size_t cap)
{
    load_index();if(read_only || i<0 || i>=(int)index_file.count)
        {snprintf(error,cap,"Shortcut index unavailable.");return false;}
    struct aurora_shortcut_info record=index_file.items[i];
    char link_path[260],xex_path[260];
    if(!child_path(link_path,sizeof(link_path),record.folder,"moonx360.link") ||
       !child_path(xex_path,sizeof(xex_path),record.folder,"default.xex"))return false;
    if(regular(link_path) && !link_matches(i))
        {snprintf(error,cap,"Shortcut files changed; deletion stopped.");return false;}
    if(!regular(link_path) && regular(xex_path))
        {snprintf(error,cap,"Link is missing; executable was preserved.");return false;}
    if(regular(xex_path) && !DeleteFileA(xex_path))
        {snprintf(error,cap,"Could not remove shortcut executable.");return false;}
    if(regular(link_path) && !DeleteFileA(link_path))
        {snprintf(error,cap,"Could not remove shortcut link.");return false;}
    char metadata_file[260];
    if(metadata_path(metadata_file,&record))for(int attempt=0;attempt<2;attempt++){
        char candidate[272];snprintf(candidate,sizeof(candidate),"%s%s",metadata_file,attempt?".bak":"");
        FILE *f=fopen(candidate,"rb");struct moonx_aurora_metadata found;
        bool owned=f && fread(&found,1,sizeof(found),f)==sizeof(found) && fgetc(f)==EOF &&
            !ferror(f) && moonx_aurora_metadata_valid(&found,sizeof(found),&record.link);
        if(f)fclose(f);if(owned)DeleteFileA(candidate);
    }
    RemoveDirectoryA(record.folder); /* Leaves folders containing unknown files. */
    moonx_index_remove(&index_file,(unsigned)i);
    if(!save_index()){snprintf(error,cap,"Files removed; index save failed. Use Clean Missing Shortcuts.");return false;}
    snprintf(error,cap,"Shortcut deleted. Aurora may cache it; run Aurora Database Cleaner or rescan.");
    return true;
}
bool aurora_shortcut_recreate(int i,const struct config *cfg,const char *profile,
                              char *error,size_t cap)
{
    load_index();
    if(read_only || !cfg || i<0 || i>=(int)index_file.count ||
       !clean_input(profile,80,false) || !link_matches(i))
        {snprintf(error,cap,"Indexed shortcut link is missing or changed.");return false;}
    const char *template_xex="game:\\MoonX360Shortcut\\default.xex";
    if(!regular(template_xex) || !moonx_launch_path(cfg->aurora_main_xex,sizeof(cfg->aurora_main_xex)) || !current_image(cfg->aurora_main_xex))
        {snprintf(error,cap,"Shortcut template or running MoonX360 XEX is missing. Select the XEX in Advanced > Aurora Integration.");return false;}
    char link[260],xex[260],link_new[260],xex_new[260],link_old[260],xex_old[260];
    const char *folder=index_file.items[i].folder;
    if(!child_path(link,sizeof(link),folder,"moonx360.link") ||
       !child_path(xex,sizeof(xex),folder,"default.xex") ||
       !child_path(link_new,sizeof(link_new),folder,"moonx360.link.new") ||
       !child_path(xex_new,sizeof(xex_new),folder,"default.xex.new") ||
       !child_path(link_old,sizeof(link_old),folder,"moonx360.link.old") ||
       !child_path(xex_old,sizeof(xex_old),folder,"default.xex.old") ||
       regular(link_new) || regular(xex_new) || regular(link_old) || regular(xex_old))
        {snprintf(error,cap,"Shortcut update files are unavailable or occupied.");return false;}
    struct aurora_shortcut_info before=index_file.items[i],next=before;
    snprintf(next.link.profile_id,sizeof(next.link.profile_id),"%s",profile);
    snprintf(next.link.main_xex,sizeof(next.link.main_xex),"%s",cfg->aurora_main_xex);
    moonx_launch_seal(&next.link);
    if(!CopyFileA(template_xex,xex_new,TRUE) || !write_link(link_new,&next.link)) {
        DeleteFileA(xex_new);DeleteFileA(link_new);
        snprintf(error,cap,"Could not stage replacement shortcut files.");return false;
    }
    bool had_xex=regular(xex),old_xex_moved=false,old_link_moved=false;
    if(had_xex)old_xex_moved=MoveFileA(xex,xex_old)!=0;
    if(!had_xex || old_xex_moved)old_link_moved=MoveFileA(link,link_old)!=0;
    bool new_xex_moved=false,new_link_moved=false;
    if(old_link_moved && MoveFileA(xex_new,xex)) {
        new_xex_moved=true;
        if(MoveFileA(link_new,link))new_link_moved=true;
    }
    if(new_link_moved) {
        index_file.items[i]=next;
        if(save_index()) {
            DeleteFileA(link_old);if(old_xex_moved)DeleteFileA(xex_old);
            snprintf(error,cap,"Shortcut recreated. Rescan Aurora if its title is stale.");return true;
        }
        index_file.items[i]=before;
    }
    if(new_link_moved)DeleteFileA(link);
    if(new_xex_moved)DeleteFileA(xex);
    if(old_link_moved)MoveFileA(link_old,link);
    if(old_xex_moved)MoveFileA(xex_old,xex);
    DeleteFileA(link_new);DeleteFileA(xex_new);
    snprintf(error,cap,"Recreation failed; previous files were restored when possible.");return false;
}
int aurora_shortcut_clean_missing(void)
{
    load_index();if(read_only)return -1;
    int removed=0;
    for(int i=(int)index_file.count-1;i>=0;i--) {
        const struct aurora_shortcut_info *r=index_file.items+i;
        char link[260],xex[260];
        if(!child_path(link,sizeof(link),r->folder,"moonx360.link") ||
           !child_path(xex,sizeof(xex),r->folder,"default.xex"))continue;
        if(regular(link) || regular(xex))continue;
        moonx_index_remove(&index_file,(unsigned)i);removed++;
    }
    if(removed && !save_index())return -1;
    return removed;
}
static int aurora_probe_xex_file(const char *path)
{
    if(!regular(path))return 0;
    FILE *file=fopen(path,"rb");if(!file)return 0;
    int valid=aurora_xex_stream_header(file);fclose(file);return valid;
}
static int aurora_candidate_xex(const char *path,void *)
{
    if(aurora_probe_xex_file(path))return 1;
    char folder[160],pattern[164];snprintf(folder,sizeof(folder),"%s",path);
    if(!aurora_path_parent(folder))return 0;
    int n=snprintf(pattern,sizeof(pattern),"%s%s*",folder,folder[strlen(folder)-1]=='\\'?"":"\\");
    if(n<0 || n>=(int)sizeof(pattern))return 0;
    WIN32_FIND_DATAA data;HANDLE search=FindFirstFileA(pattern,&data);
    if(search==INVALID_HANDLE_VALUE)return 0;
    int found=0;
    do {
        if((data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) || _stricmp(data.cFileName,"Aurora.xex"))continue;
        char actual[160];
        if(aurora_path_join(actual,sizeof(actual),folder,data.cFileName))found=aurora_probe_xex_file(actual);
        if(found)break;
    }while(FindNextFileA(search,&data));
    FindClose(search);return found;
}
bool aurora_shortcut_valid_aurora(const char *folder)
{
    if(!folder || !moonx_launch_folder(folder,160))return false;
    char xex[160];return aurora_path_join(xex,sizeof(xex),folder,"Aurora.xex") &&
                          aurora_candidate_xex(xex,NULL)!=0;
}
bool aurora_shortcut_install_sync(const char *aurora_folder,char *error,size_t cap)
{
    const char *source="game:\\AuroraScripts\\MoonX360Sync\\Main.lua";
    const char *icon_source="game:\\AuroraScripts\\MoonX360Sync\\icon.png";
    char user[240],scripts[240],utility[240],target_dir[240],target[260],icon[260],backup[264];
    if(!error || !cap)return false;
    if(!aurora_shortcut_valid_aurora(aurora_folder)){
        snprintf(error,cap,"Set a valid Aurora Location first.");return false;
    }
    if(!regular(source) || !regular(icon_source)){
        snprintf(error,cap,"Missing AuroraScripts\\MoonX360Sync files beside MoonX360.");return false;
    }
    if(!aurora_path_join(user,sizeof(user),aurora_folder,"User") ||
       !aurora_path_join(scripts,sizeof(scripts),user,"Scripts") ||
       !aurora_path_join(utility,sizeof(utility),scripts,"Utility") ||
       !aurora_path_join(target_dir,sizeof(target_dir),utility,"MoonX360Sync") ||
       !aurora_path_join(target,sizeof(target),target_dir,"Main.lua") ||
       !aurora_path_join(icon,sizeof(icon),target_dir,"icon.png")){
        snprintf(error,cap,"Aurora script path is too long.");return false;
    }
    if((!directory(user) && !CreateDirectoryA(user,NULL)) ||
       (!directory(scripts) && !CreateDirectoryA(scripts,NULL)) ||
       (!directory(utility) && !CreateDirectoryA(utility,NULL)) ||
       (!directory(target_dir) && !CreateDirectoryA(target_dir,NULL))){
        snprintf(error,cap,"Could not create Aurora utility folder.");return false;
    }
    if(!CopyFileA(icon_source,icon,FALSE)){
        snprintf(error,cap,"Could not install Aurora sync icon.");return false;
    }
    snprintf(backup,sizeof(backup),"%s.bak",target);
    if(regular(target) && !CopyFileA(target,backup,FALSE)){
        snprintf(error,cap,"Could not back up the existing Aurora sync script.");return false;
    }
    if(!CopyFileA(source,target,FALSE)){
        snprintf(error,cap,"Could not install Aurora sync script.");return false;
    }
    snprintf(error,cap,"Installed. In Aurora: Back > Scripts > MoonX360 Shortcut Metadata.");
    return true;
}
static int aurora_detect_list(const char *folder,char names[][64],int capacity,void *)
{
    if(!folder || !names || capacity<1 || !aurora_shortcut_folder_ready(folder))return -1;
    char pattern[164];
    int length=snprintf(pattern,sizeof(pattern),"%s%s*",folder,folder[strlen(folder)-1]=='\\'?"":"\\");
    if(length<0 || length>=(int)sizeof(pattern))return -1;
    WIN32_FIND_DATAA data;HANDLE search=FindFirstFileA(pattern,&data);
    if(search==INVALID_HANDLE_VALUE){
        DWORD error=GetLastError();
        return error==ERROR_FILE_NOT_FOUND || error==ERROR_NO_MORE_FILES?0:-1;
    }
    int count=0;
    do {
        if(!(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) ||
           moonx_aurora_skip_dir(data.cFileName) || strlen(data.cFileName)>=64)continue;
        char child[160];
        if(!aurora_path_join(child,sizeof(child),folder,data.cFileName))continue;
        snprintf(names[count++],64,"%s",data.cFileName);
        if(count==capacity)break;
    }while(FindNextFileA(search,&data));
    FindClose(search);return count;
}
static int aurora_detect_cancel(void *)
{
    XINPUT_STATE state;
    for(DWORD user=0;user<4;user++)if(XInputGetState(user,&state)==ERROR_SUCCESS &&
        (state.Gamepad.wButtons&XINPUT_GAMEPAD_B))return 1;
    return 0;
}
static void aurora_detect_log(const char *line)
{OutputDebugStringA(line);plat_log(line);}
static void aurora_detect_checked(const char *path,int valid,void *)
{
    char line[220];snprintf(line,sizeof(line),"[AuroraDetect] checking %s = %s\n",path,valid?"XEX2 candidate":"not found/invalid");
    aurora_detect_log(line);
}
static void aurora_detect_phase(int stage,void *)
{if(stage==2)aurora_detect_log("[AuroraDetect] bounded search started depth=4 budget=320\n");}
int aurora_shortcut_detect_aurora(char *path,size_t cap)
{
    if(!path || cap<16)return 0;
    aurora_detect_log("[AuroraDetect] begin; looking for Aurora.xex\n");
    if(path[0] && aurora_shortcut_valid_aurora(path))return 1;
    static const char *probe_roots[]={"Hdd1:\\","Usb0:\\","Usb1:\\","Usb2:\\"};
    for(unsigned i=0;i<sizeof(probe_roots)/sizeof(probe_roots[0]);i++){
        moonx_mount_report report;bool accessible=moonx_mount_for_path_ex(probe_roots[i],&report);
        char line[180];snprintf(line,sizeof(line),"[AuroraDetect] mount %s = %s existing=%d status=0x%08lX\n",
            probe_roots[i],accessible?"OK":"FAIL",report.existed,(unsigned long)report.status);aurora_detect_log(line);
    }
    struct aurora_browser_entry roots[AURORA_BROWSER_MAX];
    int count=aurora_browser_roots(roots,AURORA_BROWSER_MAX);
    char root_paths[20][20];const char *root_ptrs[20];
    if(count>20)count=20;
    for(int i=0;i<count;i++) {
        snprintf(root_paths[i],sizeof(root_paths[i]),"%s:\\",roots[i].name);root_ptrs[i]=root_paths[i];
        char line[80];snprintf(line,sizeof(line),"[AuroraDetect] mount %s = OK\n",root_paths[i]);aurora_detect_log(line);
    }
    aurora_detect_log("[AuroraDetect] checking fast known paths\n");
    moonx_aurora_search_io io={aurora_candidate_xex,aurora_detect_list,aurora_detect_cancel,aurora_detect_checked,aurora_detect_phase,NULL};
    int found=moonx_aurora_find(path,cap,root_ptrs,count,&io);
    if(found>0){char line[200];snprintf(line,sizeof(line),"[AuroraDetect] selected candidate directory=%s\n",path);aurora_detect_log(line);}
    else aurora_detect_log(found<0?"[AuroraDetect] cancelled\n":"[AuroraDetect] not found\n");
    return found;
}
int aurora_shortcut_edit_path(char *path,size_t cap,const char *title)
{
    if(!path || cap>160 || cap<2)return -1;
    WCHAR initial[160],result[160],heading[96];
    if(!MultiByteToWideChar(CP_UTF8,0,path,-1,initial,160) ||
       !MultiByteToWideChar(CP_UTF8,0,title,-1,heading,96))return -1;
    ZeroMemory(result,sizeof(result));XOVERLAPPED pending;ZeroMemory(&pending,sizeof(pending));
    DWORD rc=XShowKeyboardUI(0,VKBD_DEFAULT,initial,heading,L"Enter a drive path. B cancels.",result,(DWORD)cap,&pending);
    if(rc==ERROR_IO_PENDING){while(!XHasOverlappedIoCompleted(&pending))Sleep(16);rc=XGetOverlappedResult(&pending,NULL,FALSE);}
    if(rc==ERROR_CANCELLED)return 0;
    if(rc!=ERROR_SUCCESS || !WideCharToMultiByte(CP_UTF8,0,result,-1,path,(int)cap,NULL,NULL))return -1;
    return 1;
}
int aurora_browser_edit_name(char *name,size_t cap)
{
    if(!name || cap<2 || cap>64)return -1;
    WCHAR initial[64],result[64];
    if(!MultiByteToWideChar(CP_UTF8,0,name,-1,initial,64))return -1;
    ZeroMemory(result,sizeof(result));XOVERLAPPED pending;ZeroMemory(&pending,sizeof(pending));
    DWORD rc=XShowKeyboardUI(0,VKBD_DEFAULT,initial,L"Create Folder",L"Enter one folder name.",result,(DWORD)cap,&pending);
    if(rc==ERROR_IO_PENDING){while(!XHasOverlappedIoCompleted(&pending))Sleep(16);rc=XGetOverlappedResult(&pending,NULL,FALSE);}
    if(rc==ERROR_CANCELLED)return 0;
    if(rc!=ERROR_SUCCESS || !WideCharToMultiByte(CP_UTF8,0,result,-1,name,(int)cap,NULL,NULL))return -1;
    return 1;
}
int aurora_shortcut_edit_metadata_id(char text[9])
{
    WCHAR initial[9],result[9];
    if(!MultiByteToWideChar(CP_UTF8,0,text,-1,initial,9))return -1;
    ZeroMemory(result,sizeof(result));XOVERLAPPED pending;ZeroMemory(&pending,sizeof(pending));
    DWORD rc=XShowKeyboardUI(0,VKBD_DEFAULT,initial,L"Xbox 360 Metadata Title ID",
        L"Enter 8 hex digits, or leave empty for None. No automatic matching.",result,9,&pending);
    if(rc==ERROR_IO_PENDING){while(!XHasOverlappedIoCompleted(&pending))Sleep(16);rc=XGetOverlappedResult(&pending,NULL,FALSE);}
    if(rc==ERROR_CANCELLED)return 0;
    if(rc!=ERROR_SUCCESS || !WideCharToMultiByte(CP_UTF8,0,result,-1,text,9,NULL,NULL))return -1;
    return 1;
}
int aurora_shortcut_edit_metadata_text(char *text,size_t cap,bool description)
{
    if(!text || cap<2 || cap>256)return -1;
    WCHAR initial[256],result[256];
    if(!MultiByteToWideChar(CP_UTF8,0,text,-1,initial,256))return -1;
    ZeroMemory(result,sizeof(result));XOVERLAPPED pending;ZeroMemory(&pending,sizeof(pending));
    DWORD rc=XShowKeyboardUI(0,VKBD_DEFAULT,initial,description?L"Aurora Description":L"Aurora Display Title",
        L"This changes Aurora metadata only; Direct Launch stays intact.",result,(DWORD)cap,&pending);
    if(rc==ERROR_IO_PENDING){while(!XHasOverlappedIoCompleted(&pending))Sleep(16);rc=XGetOverlappedResult(&pending,NULL,FALSE);}
    if(rc==ERROR_CANCELLED)return 0;
    if(rc!=ERROR_SUCCESS || !WideCharToMultiByte(CP_UTF8,0,result,-1,text,(int)cap,NULL,NULL))return -1;
    return 1;
}
