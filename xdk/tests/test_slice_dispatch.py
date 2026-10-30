"""Real legacy/generated slice-pool bodies on desktop threads; no Xbox timing claims."""
from pathlib import Path
import hashlib,importlib.util,json,os,re,subprocess,sys,tempfile,unittest
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'xdk/scripts'))
import slice_dispatch as dispatch
SOURCE=ROOT/'xdk/deps/XBMC-360/libraries/ffmpeg/libavcodec/xb_thread.c'

class Policy(unittest.TestCase):
    def test_report_identity_and_legacy_unavailable(self):
        sys.path.insert(0,str(ROOT/'xdk/tests'))
        from collect_ab_results import parse_report
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'decoder-session.txt'
            p.write_text('slice_dispatch=batched\nslice_dispatch_version=2\nfast_decode_config=0\nrecovery_count=3\nfull_frames=40\nfast_frames=20\ncallback_total_avg_us=15000\ncallback_gap_avg_us=300\n')
            report=parse_report(p);self.assertEqual(report['slice_dispatch'],'batched');self.assertEqual(report['slice_dispatch_version'],2)
            self.assertEqual((report['fast_decode_config'],report['recovery_count'],report['full_frames'],report['reduced_filter_frames']),(0,3,40,20))
            self.assertEqual((report['callback_mean_us'],report['callback_gap_mean_us']),(15000,300))
            p.write_text('received=0\n');report=parse_report(p)
            self.assertIsNone(report['slice_dispatch']);self.assertIsNone(report['reduced_filter_frames'])

    def test_exact_source_and_rejection(self):
        text=SOURCE.read_text(encoding='utf-8');generated=dispatch.generate(text)
        self.assertIn('InterlockedIncrement(&c->next_job)',generated)
        self.assertNotIn('int dummy_ret;',dispatch.function(generated,'static int avcodec_thread_execute('))
        self.assertEqual(text[text.index('/* ============================================================\n * Frame Threading'):],
                         generated[generated.index('/* ============================================================\n * Frame Threading'):].split('\nint ff_xbox_slice_dispatch_version')[0])
        for variant in (text+'\n/* unknown */',generated,text.replace('self_id = c->current_job++;','self_id = 99;')):
            with self.assertRaises(RuntimeError):dispatch.generate(variant)
        with tempfile.TemporaryDirectory() as d:
            dest=Path(d)/'generated.c';dispatch.write_generated(SOURCE,dest);stamp=dest.stat().st_mtime_ns
            dispatch.write_generated(SOURCE,dest);self.assertEqual(dest.stat().st_mtime_ns,stamp)

    def test_link_rejects_scheduler_and_hash_mismatch(self):
        for mode in ('legacy','batched'):
            with tempfile.TemporaryDirectory() as d,patch.dict(os.environ,{'XDK_BUILD_DIR':d,'XDK_SLICE_DISPATCH':mode,'XDK_OPTIMIZATION':'compatible'}):
                spec=importlib.util.spec_from_file_location('native_scheduler_'+mode,ROOT/'xdk/build.py');b=importlib.util.module_from_spec(spec);spec.loader.exec_module(b)
                ff=Path(d)/'ffmpeg';ff.mkdir();manifest={'profile':'compatible','worker_map':'current','worker_sha256':hashlib.sha256(SOURCE.read_bytes()).hexdigest(),'slice_dispatch':'batched' if mode=='legacy' else 'legacy'}
                (ff/'build-manifest.json').write_text(json.dumps(manifest))
                with self.assertRaisesRegex(SystemExit,'slice dispatch changed'):b.target_xex()
                if mode=='batched':
                    manifest.update(slice_dispatch='batched',slice_dispatch_sha256='bad')
                    (ff/'build-manifest.json').write_text(json.dumps(manifest))
                    with self.assertRaisesRegex(SystemExit,'dispatcher changed'):b.target_xex()
        with patch.dict(os.environ,{'XDK_SLICE_DISPATCH':'unknown'}):
            spec=importlib.util.spec_from_file_location('bad_scheduler',ROOT/'xdk/build.py');b=importlib.util.module_from_spec(spec)
            with self.assertRaises(SystemExit):spec.loader.exec_module(b)

    def test_same_output_switches_rebuild_dispatch_object(self):
        import shutil
        from types import SimpleNamespace
        with tempfile.TemporaryDirectory() as d,patch.dict(os.environ,{'XDK_BUILD_DIR':d,'XDK_OPTIMIZATION':'compatible'}):
            spec=importlib.util.spec_from_file_location('ffmpeg_dispatch_cache',ROOT/'xdk/scripts/build_ffmpeg.py');b=importlib.util.module_from_spec(spec);spec.loader.exec_module(b)
            source=Path(d)/'source/libavcodec/xb_thread.c';source.parent.mkdir(parents=True);shutil.copy2(SOURCE,source)
            b.FFMPEG=str(source.parent.parent);b.VCPROJ=str(source.parent);b.LIBS={'avcodec':('fake.vcxproj',[],[],[])};b.EXTRA_SOURCES={}
            seen=[]
            def compile_unit(job):
                src,obj,flags=job
                if not Path(obj).exists():seen.append(b.SLICE_DISPATCH);Path(obj).write_text(b.SLICE_DISPATCH)
                return src,0,''
            def librarian(*args,**kwargs):
                (Path(b.OUT)/'avcodec.lib').write_bytes(b'fake archive');return SimpleNamespace(returncode=0)
            with patch.object(b,'sources_of',return_value=[str(source)]),patch.object(b,'compile_one',side_effect=compile_unit),patch.object(b.subprocess,'run',side_effect=librarian):
                for mode in ('legacy','batched','batched','legacy'):
                    b.SLICE_DISPATCH=mode;self.assertTrue(b.build('avcodec'))
            self.assertEqual(seen,['legacy','batched','legacy'])

def fixture():
    text=SOURCE.read_text(encoding='utf-8')
    out=r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <limits.h>
typedef long LONG;
static LONG InterlockedIncrement(volatile LONG *p){return _InterlockedIncrement(p);}
static LONG InterlockedDecrement(volatile LONG *p){return _InterlockedDecrement(p);}
static LONG InterlockedExchange(volatile LONG *p,LONG v){return _InterlockedExchange(p,v);}
static LONG InterlockedCompareExchange(volatile LONG *p,LONG value,LONG expected){return _InterlockedCompareExchange(p,value,expected);}
static std::atomic<unsigned> locks(0),starts(0),joins(0);
static int fail_at=-1;
static int allocation_calls,allocation_fail_at=-1;
struct test_mutex{std::mutex *p;};struct test_cond{std::condition_variable *p;};
struct test_thread{std::thread native;};
typedef test_mutex pthread_mutex_t;typedef test_cond pthread_cond_t;typedef test_thread *pthread_t;typedef void *HANDLE;
struct test_event{std::mutex mutex;std::condition_variable condition;bool signaled;};
static std::atomic<int> event_count(0);static int event_calls,event_fail_at=-1;
#define FALSE 0
#define INFINITE 0xffffffffu
static HANDLE CreateEvent(void*,int manual,int signaled,void*){
    assert(!manual);if(event_calls++==event_fail_at)return NULL;
    test_event *e=new test_event;e->signaled=signaled!=0;event_count++;return e;
}
static int SetEvent(HANDLE h){test_event *e=(test_event*)h;{std::lock_guard<std::mutex> lock(e->mutex);e->signaled=true;}e->condition.notify_one();return 1;}
static unsigned WaitForSingleObject(HANDLE h,unsigned timeout){
    assert(timeout==INFINITE);test_event *e=(test_event*)h;std::unique_lock<std::mutex> lock(e->mutex);
    e->condition.wait(lock,[=]{return e->signaled;});e->signaled=false;return 0;
}
static void CloseHandle(HANDLE h){delete (test_event*)h;event_count--;}
static void pthread_mutex_init(pthread_mutex_t *m,void*){m->p=new std::mutex;}
static void pthread_mutex_destroy(pthread_mutex_t *m){delete m->p;}
static void pthread_mutex_lock(pthread_mutex_t *m){locks++;m->p->lock();}
static void pthread_mutex_unlock(pthread_mutex_t *m){m->p->unlock();}
static void pthread_cond_init(pthread_cond_t *c,void*){c->p=new std::condition_variable;}
static void pthread_cond_destroy(pthread_cond_t *c){delete c->p;}
static void pthread_cond_wait(pthread_cond_t *c,pthread_mutex_t *m){std::unique_lock<std::mutex> l(*m->p,std::adopt_lock);c->p->wait(l);l.release();}
static void pthread_cond_signal(pthread_cond_t *c){c->p->notify_one();}
static void pthread_cond_broadcast(pthread_cond_t *c){c->p->notify_all();}
static int pthread_create(pthread_t *t,void*,void *(*fn)(void*),void *arg){
    int index=(int)starts++;if(index==fail_at)return 1;
    *t=new test_thread;(*t)->native=std::thread([=](){if(index%3==0)std::this_thread::sleep_for(std::chrono::milliseconds(1));fn(arg);});return 0;
}
static void pthread_join(pthread_t t,void*){t->native.join();delete t;joins++;}
static HANDLE pthread_getw32threadhandle_np(pthread_t t){return t;}
static void SuspendThread(HANDLE){}static void ResumeThread(HANDLE){}
static void XSetThreadProcessor(HANDLE,int ht){assert(ht>=1 && ht<=5);}
static void *av_mallocz(size_t n){if(allocation_calls++==allocation_fail_at)return NULL;return calloc(1,n);}static void av_free(void *p){free(p);}
static void av_freep(void *p){void **v=(void**)p;free(*v);*v=NULL;}
#define attribute_align_arg
#define av_always_inline inline
#define MAX_AUTO_THREADS 16
#define FF_THREAD_SLICE 2
#define FFMIN(a,b) ((a)<(b)?(a):(b))
'''
    for mode,source in [('legacy',text),('batched',dispatch.generate(text))]:
        context=dispatch.function(source,'typedef struct ThreadContext')+' ThreadContext;'
        if mode=='batched':context=dispatch.function(source,'typedef struct SliceWorker')+' SliceWorker;\n'+context
        mapping=re.search(r'static const int xenon_worker_map[^;]+;',source)[0]
        out+='\nnamespace '+mode+' {\n'+r'''
struct AVCodecContext;
typedef int (action_func)(AVCodecContext*,void*);
typedef int (action_func2)(AVCodecContext*,void*,int,int);
struct AVCodecContext{
    void *thread_opaque;int active_thread_type,thread_count;
    int (*execute)(AVCodecContext*,action_func*,void*,int*,int,int);
    int (*execute2)(AVCodecContext*,action_func2*,void*,int*,int);
};
static int ff_get_logical_cpus(AVCodecContext*){return 4;}
static int avcodec_default_execute(AVCodecContext *c,action_func *f,void *arg,int *ret,int n,int size){for(int i=0;i<n;i++){int r=f(c,(char*)arg+i*size);if(ret)ret[i]=r;}return 0;}
'''+mapping+'\n#define hw_thread(x) xenon_worker_map[(x)%5]\n'+context+'\n'
        for sig in ('static void* attribute_align_arg worker(','static av_always_inline void avcodec_thread_park_workers(','static void thread_free(','static int avcodec_thread_execute(','static int avcodec_thread_execute2('):
            out+=dispatch.function(source,sig)+'\n'
        out+='static void ff_thread_free(AVCodecContext *c){thread_free(c);}\n'+dispatch.function(source,'static int thread_init(')+'\n'
        out+=r'''
struct Jobs{std::vector<std::atomic<int> > seen;int workers;Jobs(int n,int w):seen(n),workers(w){for(auto &v:seen)v=0;}};
static int action2(AVCodecContext*,void *arg,int job,int worker_id){
    Jobs *jobs=(Jobs*)arg;assert(job>=0 && job<(int)jobs->seen.size());assert(worker_id>=0 && worker_id<jobs->workers);
    assert(jobs->seen[job].fetch_add(1)==0);if(job%7==0)std::this_thread::yield();return job*17-23;
}
static int action1(AVCodecContext*,void *arg){int *v=(int*)arg;int before=*v;*v+=7;return before-19;}
static unsigned run(){
    unsigned batch_locks=0;
    for(int workers=2;workers<=5;workers++)for(int cycle=0;cycle<12;cycle++){
        AVCodecContext c={};c.thread_count=workers;c.active_thread_type=FF_THREAD_SLICE;
        assert(thread_init(&c)==0);
        for(int round=0;round<24;round++)for(int count:{0,1,2,3,4,5,8,12,16,47,127}){
            Jobs jobs(count,workers);std::vector<int> ret(count,INT_MIN);
            c.execute2(&c,action2,&jobs,ret.data(),count);
            for(int i=0;i<count;i++){assert(jobs.seen[i]==1);assert(ret[i]==i*17-23);}
            Jobs no_ret(count,workers);c.execute2(&c,action2,&no_ret,NULL,count);
            for(auto &v:no_ret.seen)assert(v==1);
            std::vector<int> data(count),results(count,INT_MIN);for(int i=0;i<count;i++)data[i]=i*3;
            c.execute(&c,action1,data.data(),results.data(),count,sizeof(int));
            for(int i=0;i<count;i++)assert(data[i]==i*3+7 && results[i]==i*3-19);
        }
        if(workers==4 && cycle==0)for(int n:{2,3,4,8,12,16}){
            Jobs jobs(n,workers);unsigned before=locks.load();c.execute2(&c,action2,&jobs,NULL,n);batch_locks=locks.load()-before;
            printf("POOL_LABEL: jobs=%d workers=4 explicit_mutex_acquisitions=%u\n",n,batch_locks);
        }
        thread_free(&c);assert(c.thread_opaque==NULL && event_count==0);
    }
    for(int fail=0;fail<4;fail++){
        starts=0;fail_at=fail;AVCodecContext c={};c.thread_count=4;c.active_thread_type=FF_THREAD_SLICE;
        assert(thread_init(&c)==-1 && c.thread_opaque==NULL);fail_at=-1;
    }
    EVENT_FAILURE_TESTS
    return batch_locks;
}
#undef hw_thread
}
'''
    out+=r'''
int main(){
    unsigned before=legacy::run(),after=batched::run();
    assert(before==17 && after==0);
    printf("PASS: real pools execute every job once; execute/execute2, errors/results, NULL results, uneven work, 2/3/4/5 workers, repeated batches, legacy generation wrap, delayed startup, allocation/startup/event failure and join/cleanup.\n");
    printf("Explicit mutex acquisitions for 16 jobs / 4 workers: legacy=%u batched=%u (condition-variable internal locking excluded).\n",before,after);
}
'''
    # C source is compiled as C++ only for the desktop thread adapter. These
    # casts do not alter production. Narrow generations exercise real wrap cycles.
    out=out.replace('AVCodecContext *avctx = v;','AVCodecContext *avctx = (AVCodecContext*)v;')
    out=out.replace('SliceWorker *w = v;','SliceWorker *w = (SliceWorker*)v;')
    out=re.sub(r'(ThreadContext \*c\s*=\s*)avctx->thread_opaque;',r'\1(ThreadContext*)avctx->thread_opaque;',out)
    out=out.replace('c = av_mallocz(sizeof(ThreadContext));','c = (ThreadContext*)av_mallocz(sizeof(ThreadContext));')
    out=out.replace('c->workers = av_mallocz(', 'c->workers = (pthread_t*)av_mallocz(')
    out=out.replace('c->worker_args = av_mallocz(', 'c->worker_args = (SliceWorker*)av_mallocz(')
    out=out.replace('c->worker_start = av_mallocz(', 'c->worker_start = (HANDLE*)av_mallocz(')
    out=out.replace('unsigned int current_execute;','uint8_t current_execute;')
    out=out.replace('int last_execute = 0;','uint8_t last_execute = 0;').replace('unsigned last_execute = 0;','uint8_t last_execute = 0;')
    out=out.replace('POOL_LABEL', 'slice-pool')
    out=out.replace('    EVENT_FAILURE_TESTS', '',1)
    out=out.replace('    EVENT_FAILURE_TESTS', '''    for(int fail=0;fail<5;fail++){
        event_calls=0;event_fail_at=fail;AVCodecContext c={};c.thread_count=4;c.active_thread_type=FF_THREAD_SLICE;
        assert(thread_init(&c)==-1 && c.thread_opaque==NULL && event_count==0);event_fail_at=-1;
    }
    for(int fail=0;fail<4;fail++){
        allocation_calls=0;allocation_fail_at=fail;AVCodecContext c={};c.thread_count=4;c.active_thread_type=FF_THREAD_SLICE;
        assert(thread_init(&c)==-1 && c.thread_opaque==NULL && event_count==0);thread_free(&c);allocation_fail_at=-1;
    }''',1)
    return out

def run_native():
    out=Path(os.environ.get('MOONX_SLICE_OUT',ROOT/'evidencias/slice-dispatch-20261029/host'));out.mkdir(parents=True,exist_ok=True)
    vcvars=Path(os.environ.get('VS_VCVARS64',r'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat'))
    r=subprocess.run(f'cmd.exe /d /s /c ""{vcvars}" >nul && set"',capture_output=True,text=True,check=True)
    env=dict(os.environ)
    for line in r.stdout.splitlines():
        if '=' in line and not line.startswith('='):
            k,v=line.split('=',1);env[k]=v
    cl=next(str(Path(p)/'cl.exe') for p in env['PATH'].split(';') if (Path(p)/'cl.exe').exists())
    src=out/'slice-pools.cpp';src.write_text(fixture(),encoding='utf-8')
    subprocess.run([cl,'/nologo','/O2','/EHsc','/W3','/D_CRT_SECURE_NO_WARNINGS',str(src),'/Feslice-pools.exe'],cwd=out,env=env,check=True)
    subprocess.run([str(out/'slice-pools.exe')],cwd=out,env=env,check=True,timeout=60)
    # Production open_h264 must fail closed if its linked dispatcher is wrong.
    bench=out/'benchmark-batched.cpp'
    subprocess.run([sys.executable,str(ROOT/'xdk/tests/make_benchmark_test.py'),str(bench)],check=True)
    subprocess.run([cl,'/nologo','/O2','/EHsc','/DXDK_SLICE_DISPATCH_BATCHED=1','/D_CRT_SECURE_NO_WARNINGS',
                    '/I'+str(ROOT/'host-tests'),str(bench),'/Febenchmark-batched.exe'],cwd=out,env=env,check=True)
    subprocess.run([str(out/'benchmark-batched.exe')],cwd=out,env=env,check=True,timeout=60)

if __name__=='__main__':
    result=unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(Policy))
    if not result.wasSuccessful():raise SystemExit(1)
    run_native()
