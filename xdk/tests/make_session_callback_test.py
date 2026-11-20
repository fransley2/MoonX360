"""Production XDK termination callback ownership with a delayed scheduler."""
from pathlib import Path
import sys
root=Path(__file__).resolve().parents[2]
s=(root/'xdk/deps/moonlight-common-c/src/Connection.c').read_text(encoding='utf-8')
def fn(sig):
    a=s.index(sig);b=s.index('{',a)+1;d=1
    while d:d+=(s[b]=='{')-(s[b]=='}');b+=1
    return s[a:b]
a=s.index('static unsigned connectionGeneration;');b=s.index('\n#endif',a)
fixture=r'''
#include <assert.h>
#include <stdio.h>
#define _XBOX 1
typedef unsigned DWORD;typedef long LONG;
static LONG InterlockedCompareExchange(volatile LONG *p,LONG n,LONG expected){LONG old=*p;if(old==expected)*p=n;return old;}
typedef void (*ConnListenerConnectionTerminated)(int);
typedef void (*Entry)(void*);
struct PLT_THREAD {Entry entry;void *context;bool active;};
static PLT_THREAD terminationCallbackThread;
static ConnListenerConnectionTerminated originalTerminationCallback;
static int terminationCallbackErrorCode;
static bool alreadyTerminated,ConnectionInterrupted;
static unsigned joins,detaches,delivered;static int delivered_error;
static DWORD owner=1;
static DWORD GetCurrentThreadId(){return owner;}
static int PltCreateThread(const char*,Entry fn,void *context,PLT_THREAD *thread){assert(!thread->active);thread->entry=fn;thread->context=context;thread->active=true;return 0;}
static void PltJoinThread(PLT_THREAD *thread){assert(thread->active);joins++;owner=2;thread->entry(thread->context);owner=1;thread->active=false;}
static void PltDetachThread(PLT_THREAD *thread){assert(thread->active);detaches++;thread->active=false;}
#define LC_ASSERT assert
#define Limelog(...) ((void)0)
/* STATE */
/* CODE */
static void callback(int error){delivered++;delivered_error=error;}
int main(){
 for(unsigned session=1;session<=12;session++) {
    terminationClaimed=0;connectionGeneration=session;alreadyTerminated=ConnectionInterrupted=false;originalTerminationCallback=callback;
    ClInternalConnectionTerminated(100+session);assert(terminationThreadCreated && terminationCallbackThread.active);
    MoonxTermination snapshot=terminationNotice;assert(snapshot.generation==session && snapshot.error==100+(int)session);
    // Stop has joined every producer; delayed notification is suppressed and drained.
    ConnectionInterrupted=true;moonxDrainTermination();assert(!terminationThreadCreated && !terminationCallbackThread.active && !delivered);
    connectionGeneration++;ConnectionInterrupted=false;terminationCallbackThreadFunc(&snapshot);assert(!delivered);
 }
 terminationClaimed=0;connectionGeneration=50;alreadyTerminated=ConnectionInterrupted=false;originalTerminationCallback=callback;
 ClInternalConnectionTerminated(777);moonxDrainTermination();assert(delivered==1 && delivered_error==777 && joins==13 && !detaches);
 puts("PASS: actual XDK callback captures immutable generation/error/callback, rejects 12 stale sessions and joins pending notifications before reuse; current callback delivered once");
}
'''
assert s.index('moonxDrainTermination();',s.index('void LiStopConnection('))<s.index('cleanupPlatform();',s.index('void LiStopConnection('))
Path(sys.argv[1]).write_text(fixture.replace('/* STATE */',s[a:b]).replace('/* CODE */',fn('static void terminationCallbackThreadFunc(')+'\n'+fn('static void ClInternalConnectionTerminated(')),encoding='utf-8')
