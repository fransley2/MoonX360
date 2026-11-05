#include <xtl.h>

// Experimental heartbeat only. No hooks, network sockets, or controller input.
static HANDLE stop_event = NULL;
static HANDLE exited_event = NULL;
static HANDLE worker = NULL;
static volatile LONG ticks = 0;

static DWORD WINAPI heartbeat(LPVOID)
{
    OutputDebugStringA("[RemoteAgent] worker started\n");
    while (WaitForSingleObject(stop_event, 5000) == WAIT_TIMEOUT) {
        InterlockedIncrement(&ticks);
        OutputDebugStringA("[RemoteAgent] heartbeat\n");
    }
    OutputDebugStringA("[RemoteAgent] worker stopped\n");
    SetEvent(exited_event);
    return 0;
}

extern "C" __declspec(dllexport) LONG WINAPI RemoteAgentHeartbeatCount(void)
{
    return InterlockedCompareExchange(&ticks, 0, 0);
}

// An explicit in-process stop API for a future verified loader. Do not hot
// unload through DashLaunch until stop and exit are confirmed on hardware.
extern "C" __declspec(dllexport) BOOL WINAPI RemoteAgentStop(DWORD timeout_ms)
{
    if (!stop_event || !exited_event || !SetEvent(stop_event)) return FALSE;
    if (timeout_ms > 3000) timeout_ms = 3000;
    return WaitForSingleObject(exited_event, timeout_ms) == WAIT_OBJECT_0;
}

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        stop_event = CreateEvent(NULL, TRUE, FALSE, NULL);
        exited_event = CreateEvent(NULL, TRUE, FALSE, NULL);
        if (!stop_event || !exited_event) {
            if (stop_event) CloseHandle(stop_event);
            if (exited_event) CloseHandle(exited_event);
            stop_event = exited_event = NULL;
            return FALSE;
        }
        worker = CreateThread(NULL, 32768, heartbeat, NULL, 0, NULL);
        if (!worker) {
            CloseHandle(stop_event);
            CloseHandle(exited_event);
            stop_event = exited_event = NULL;
            return FALSE;
        }
        OutputDebugStringA("[RemoteAgent] attached\n");
    } else if (reason == DLL_PROCESS_DETACH) {
        // Never wait under loader lock. Runtime unload is not enabled yet.
        if (stop_event) SetEvent(stop_event);
        OutputDebugStringA("[RemoteAgent] detach requested\n");
    }
    return TRUE;
}
