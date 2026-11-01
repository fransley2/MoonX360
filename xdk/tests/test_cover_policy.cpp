#include "../../src/ui/cover_cache.h"
#include <assert.h>
#include <stdio.h>
static ui_cover_key key(int id){ui_cover_key k={};strcpy(k.host,"uuid:pc");k.app=id;k.w=150;k.h=200;return k;}
int main(){
 ui_cover_cache c={};ui_cover_key k=key(1);int slot=ui_cover_slot(&c,&k,true);
 ui_cover_attempt(&c,&k,100,false);assert(!ui_cover_retry(&c,&k,101));
 for(int i=2;i<20;i++){ui_cover_key next=key(i);assert(ui_cover_slot(&c,&next,true)>=0);}
 assert(ui_cover_find(&c,&k)<0 && !ui_cover_retry(&c,&k,10099) && ui_cover_retry(&c,&k,10100));
 ui_cover_invalidate(&c,k.host);assert(!ui_cover_retry(&c,&k,101));
 k=key(2);ui_cover_attempt(&c,&k,UINT32_MAX-99,false);assert(!ui_cover_retry(&c,&k,0));assert(ui_cover_retry(&c,&k,9900));
 ui_cover_attempt(&c,&k,1,true);assert(ui_cover_retry(&c,&k,1));
 memset(&c,0,sizeof(c));for(int i=1;i<=UI_COVER_FAILURE_LIMIT;i++){k=key(i);ui_cover_attempt(&c,&k,500,false);}
 k=key(999);assert(!ui_cover_retry(&c,&k,501) && ui_cover_retry(&c,&k,10500));
 ui_cover_dispose(&c);puts("PASS: cooldown survives pixel eviction/invalidation, host/app/size isolation, success reset, saturation and clock wrap");
}
