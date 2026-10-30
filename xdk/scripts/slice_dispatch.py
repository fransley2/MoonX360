"""Generate an opt-in slice scheduler from the exact pinned xb_thread.c.

The dependency stays unchanged. Only slice jobs are affected; frame threading,
affinity, decoder arithmetic, references and filters remain the original code.
"""
from pathlib import Path
from patch_worker_map import validate

MODES=('legacy','batched')

CONTEXT=r'''typedef struct SliceWorker {
    AVCodecContext *avctx;
    int self_id;
} SliceWorker;

typedef struct ThreadContext {
    pthread_t *workers;
    SliceWorker *worker_args;
    HANDLE *worker_start;
    HANDLE batch_done;
    action_func *func;
    action_func2 *func2;
    void *args;
    int *rets;
    int job_count, job_size, worker_count;
    volatile LONG next_job, workers_running, done;
} ThreadContext;'''

WORKER=r'''static void* attribute_align_arg worker(void *v)
{
    SliceWorker *w = v;
    AVCodecContext *avctx = w->avctx;
    ThreadContext *c = avctx->thread_opaque;
    int self_id = w->self_id;
    int our_job, result;

    for (;;) {
        /* Auto-reset events retain a wake even before this worker starts waiting.
         * No polling, shared pthread mutex or condition broadcast per batch. */
        WaitForSingleObject(c->worker_start[self_id], INFINITE);
        if (InterlockedCompareExchange(&c->done, 0, 0))
            return NULL;
        our_job = self_id;

        while (our_job < c->job_count) {
            result = c->func ? c->func(avctx, (char*)c->args + our_job*c->job_size) :
                              c->func2(avctx, c->args, our_job, self_id);
            if (c->rets) c->rets[our_job] = result;
            /* Pinned H.264 batches contain at most thread_count contexts.
             * Avoid even a next-job atomic in that common fixed-assignment case. */
            if (c->job_count <= avctx->thread_count) break;
            our_job = (int)InterlockedIncrement(&c->next_job) - 1;
        }

        /* Full-barrier completion publishes results. After this acknowledgement,
         * touch no mutable batch fields: the caller may publish the next batch. */
        if (InterlockedDecrement(&c->workers_running) == 0)
            SetEvent(c->batch_done);
    }
}'''

PARK=r'''static av_always_inline void avcodec_thread_park_workers(ThreadContext *c, int thread_count)
{
    (void)thread_count;
    WaitForSingleObject(c->batch_done, INFINITE);
}'''

FREE=r'''static void thread_free(AVCodecContext *avctx)
{
    ThreadContext *c = avctx->thread_opaque;
    int i;
    if (!c) return;
    InterlockedExchange(&c->done, 1);
    for (i = 0; i < c->worker_count; i++) SetEvent(c->worker_start[i]);
    for (i = 0; i < c->worker_count; i++) pthread_join(c->workers[i], NULL);
    if (c->worker_start) {
        for (i = 0; i < avctx->thread_count; i++)
            if (c->worker_start[i]) CloseHandle(c->worker_start[i]);
    }
    if (c->batch_done) CloseHandle(c->batch_done);
    av_free(c->worker_start);
    av_free(c->worker_args);
    av_free(c->workers);
    av_freep(&avctx->thread_opaque);
}'''

EXECUTE=r'''static int avcodec_thread_execute(AVCodecContext *avctx, action_func* func, void *arg, int *ret, int job_count, int job_size)
{
    ThreadContext *c = avctx->thread_opaque;
    int i, active;
    if (!(avctx->active_thread_type & FF_THREAD_SLICE) || avctx->thread_count <= 1)
        return avcodec_default_execute(avctx, func, arg, ret, job_count, job_size);
    if (job_count <= 0) return 0;
    active = FFMIN(job_count, avctx->thread_count);
    c->job_count = job_count;
    c->job_size = job_size;
    c->args = arg;
    c->func = func;
    c->rets = ret;
    c->next_job = avctx->thread_count;
    InterlockedExchange(&c->workers_running, active);
    for (i = 0; i < active; i++) SetEvent(c->worker_start[i]);
    avcodec_thread_park_workers(c, active);
    return 0;
}'''

INIT=r'''static int thread_init(AVCodecContext *avctx)
{
    int i;
    ThreadContext *c;
    HANDLE hThread;
    int thread_count = avctx->thread_count;
    if (!thread_count) {
        int nb_cpus = ff_get_logical_cpus(avctx);
        thread_count = avctx->thread_count = nb_cpus > 1 ? FFMIN(nb_cpus + 1, MAX_AUTO_THREADS) : 1;
    }
    if (thread_count <= 1) { avctx->active_thread_type = 0; return 0; }
    c = av_mallocz(sizeof(ThreadContext));
    if (!c) return -1;
    avctx->thread_opaque = c;
    c->workers = av_mallocz(sizeof(pthread_t) * thread_count);
    c->worker_args = av_mallocz(sizeof(SliceWorker) * thread_count);
    c->worker_start = av_mallocz(sizeof(HANDLE) * thread_count);
    c->batch_done = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (!c->workers || !c->worker_args || !c->worker_start || !c->batch_done) {
        thread_free(avctx); return -1;
    }
    for (i = 0; i < thread_count; i++) {
        c->worker_start[i] = CreateEvent(NULL, FALSE, FALSE, NULL);
        if (!c->worker_start[i]) { thread_free(avctx); return -1; }
    }
    for (i = 0; i < thread_count; i++) {
        c->worker_args[i].avctx = avctx;
        c->worker_args[i].self_id = i;
        if (pthread_create(&c->workers[i], NULL, worker, &c->worker_args[i])) {
            thread_free(avctx); return -1;
        }
        c->worker_count++;
        hThread = pthread_getw32threadhandle_np(c->workers[i]);
        SuspendThread(hThread);
        XSetThreadProcessor(hThread, hw_thread(i));
        ResumeThread(hThread);
    }
    avctx->execute = avcodec_thread_execute;
    avctx->execute2 = avcodec_thread_execute2;
    return 0;
}'''

def function(text,signature):
    start=text.index(signature);end=text.index('{',start)+1;depth=1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[start:end]

def generate(text):
    validate(text)  # Reject unknown, duplicate or partially patched dependency.
    old_context=function(text,'typedef struct ThreadContext')+' ThreadContext;'
    text=text.replace(old_context,CONTEXT,1)
    text=text.replace(function(text,'static void* attribute_align_arg worker('),WORKER,1)
    text=text.replace(function(text,'static av_always_inline void avcodec_thread_park_workers('),PARK,1)
    text=text.replace(function(text,'static void thread_free('),FREE,1)
    text=text.replace(function(text,'static int avcodec_thread_execute('),EXECUTE,1)
    text=text.replace(function(text,'static int thread_init('),INIT,1)
    # The app verifies this exported value when selecting the experimental build.
    text+='\nint ff_xbox_slice_dispatch_version(void) { return 2; }\n'
    return text

def write_generated(original,destination):
    original=Path(original);destination=Path(destination)
    text=generate(original.read_text(encoding='utf-8'))
    destination.parent.mkdir(parents=True,exist_ok=True)
    if not destination.exists() or destination.read_text(encoding='utf-8')!=text:
        destination.write_text(text,encoding='utf-8')
    return destination
