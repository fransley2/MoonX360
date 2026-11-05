#include <xtl.h>

// Standalone startup probe. A missing main marker cannot distinguish CRT,
// loader, import, and entrypoint failures without debugger notifications.
int main()
{
    OutputDebugStringA("[RemoteTest] main entered\n");
    for (int i = 0; i < 12; ++i) {
        Sleep(1000);
        OutputDebugStringA("[RemoteTest] alive\n");
    }
    OutputDebugStringA("[RemoteTest] main returning\n");
    return 0;
}
