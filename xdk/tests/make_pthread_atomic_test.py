"""Regress the static-pthreads NULL CAS dispatch using the real trylock body.
Uses desktop Win32 InterlockedCompareExchange; native join/close is checked
separately on the console. Usage: python .../make_pthread_atomic_test.py out.cpp
"""
from pathlib import Path
import sys

tests = Path(__file__).resolve().parent
video = (tests.parent / 'src/video_xdk.cpp').read_text(encoding='utf-8')
pthread = (tests.parent / 'deps/XBMC-360/libraries/ffmpeg/vcproj/external/pthreads/pthread_mutex_trylock.c').read_text(encoding='utf-8')
start = video.index('static LONG WINAPI pthread_compare_exchange(')
wrapper = video[start:video.index('\n}\n', start) + 3]
start = video.index('{', video.index('void video_xdk_init(')) + 1
initialization = video[start:video.index('    avcodec_register(', start)]
trylock = pthread[pthread.index('int\npthread_mutex_trylock'):]
out = r'''
#include <windows.h>
#include <stdint.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
static LONG (WINAPI *ptw32_interlocked_compare_exchange)(LONG*,LONG,LONG);
#define PTW32_INTERLOCKED_COMPARE_EXCHANGE ptw32_interlocked_compare_exchange
#define PTW32_INTERLOCKED_LPLONG LONG*
#define PTW32_INTERLOCKED_LONG LONG
struct test_mutex { LONG lock_idx; int kind, recursive_count; DWORD ownerThread; };
typedef test_mutex *pthread_mutex_t;
#define PTHREAD_ERRORCHECK_MUTEX_INITIALIZER ((pthread_mutex_t)(uintptr_t)-3)
#define PTHREAD_MUTEX_NORMAL 0
#define PTHREAD_MUTEX_RECURSIVE 1
static DWORD caller=7;
static DWORD pthread_self() { return caller; }
static bool pthread_equal(DWORD a,DWORD b) { return a==b; }
static int ptw32_mutex_check_need_init(pthread_mutex_t *m) {
    static test_mutex initialized={}; *m=&initialized; return 0;
}
static void selftest_log(const char*) {}
static int initialized;
static int ptw32_processInitialize() {
    // Ordering matters: initialization itself may acquire library locks.
    assert(ptw32_interlocked_compare_exchange != NULL);
    LONG n=9;
    assert(ptw32_interlocked_compare_exchange(&n,11,8)==9 && n==9);
    assert(ptw32_interlocked_compare_exchange(&n,11,9)==9 && n==11);
    initialized++; return 1;
}
'''
out += wrapper + '\nstatic void actual_pthread_startup_prefix() {\n' + initialization + '\n}\n' + trylock
out += r'''
int main() {
    assert(!ptw32_interlocked_compare_exchange);
    actual_pthread_startup_prefix();
    assert(initialized==1);
    test_mutex normal={}; pthread_mutex_t m=&normal;
    assert(pthread_mutex_trylock(&m)==0 && normal.lock_idx==1);
    assert(pthread_mutex_trylock(&m)==EBUSY);
    test_mutex recursive={0,PTHREAD_MUTEX_RECURSIVE,0,0}; m=&recursive;
    assert(pthread_mutex_trylock(&m)==0 && recursive.recursive_count==1 && recursive.ownerThread==7);
    assert(pthread_mutex_trylock(&m)==0 && recursive.recursive_count==2);
    caller=8; assert(pthread_mutex_trylock(&m)==EBUSY);
    m=PTHREAD_ERRORCHECK_MUTEX_INITIALIZER;
    assert(pthread_mutex_trylock(&m)==0);
    puts("PASS: real startup CAS dispatch precedes pthread init; actual trylock normal/busy/recursive/static paths");
}
'''
Path(sys.argv[1]).write_text(out, encoding='utf-8', newline='\n')
