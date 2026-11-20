#include "../../src/app_catalog.h"
#include <assert.h>
#include <map>
#include <string>
#include <vector>
static std::map<std::string,std::vector<char>> files;
char *storage_read(const char *name,size_t *length)
{
    auto it=files.find(name);if(it==files.end())return NULL;
    char *out=(char*)malloc(it->second.size()+1);assert(out);
    memcpy(out,it->second.data(),it->second.size());out[it->second.size()]=0;
    if(length)*length=it->second.size();return out;
}
bool storage_write(const char *name,const void *data,size_t length)
{files[name]=std::vector<char>((const char*)data,(const char*)data+length);return true;}
int main()
{
    const char *one="uuid:01234567-89ab-cdef-0123-456789abcdef";
    const char *two="uuid:fedcba98-7654-3210-fedc-ba9876543210";
    gs_app apps[64]={};apps[0].id=10;strcpy(apps[0].name,"Same title");
    assert(app_catalog_save(one,apps,1));apps[0].id=20;
    assert(app_catalog_save(two,apps,1));
    gs_app loaded[64]={};assert(app_catalog_load(one,loaded,64)==1 && loaded[0].id==10);
    assert(app_catalog_load(two,loaded,64)==1 && loaded[0].id==20);
    assert(!app_catalog_save("192.168.1.4",apps,1));
    char path[64];assert(app_catalog_path(path,one));
    files[std::string(path)+".bak"]=files[path];files[path][20]^=1;
    assert(app_catalog_load(one,loaded,64)==1 && loaded[0].id==10); // verified backup
    files.erase(std::string(path)+".bak");files[path].resize(10);
    assert(app_catalog_load(one,loaded,64)<0); // truncated catalog
    puts("PASS: persistent offline app catalog, host/App ID isolation, backup and corruption");
}
