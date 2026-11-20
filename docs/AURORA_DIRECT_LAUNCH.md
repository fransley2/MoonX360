# Aurora shortcuts

MoonX360 can create optional Aurora entries for individual host apps. Each entry points to the installed main MoonX360 executable and carries the host and app identity. The host must already be paired in MoonX360.

1. Build the main dashboard XEX, then run **python xdk\build.py shortcut_xex** in the same build environment.
2. Install the main XEX and its generated **MoonX360Shortcut** template together on the console.
3. Enable Aurora integration in MoonX360. Use the game options in **Library** to create a shortcut and add its folder to Aurora's scan paths.
4. Launch the entry from Aurora. Keep the main executable and its **moonlight** folder at their configured paths.

See the [build guide](BUILD.md) for SDK setup.
