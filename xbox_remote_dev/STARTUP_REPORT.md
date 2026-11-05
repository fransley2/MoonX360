# Startup and plugin build report — 2026-11-05

The exact failed release candidate was found locally at
`xdk/build/release-candidate-20261104/default.xex` (SHA-256
`6d0f3db83afd4298adec9149b9e7611f0bb1a8b6b59c1d6a1b79153b168b0f45`).
The known-good control remains
`xdk/build/steam-library-ab-20261101/default.xex` (SHA-256
`0e62c46c05b59bdb39cf9075c30edbc074a9f559168d7e502e2029ffcdaeeb44`).

`imagexex /dump` comparison:

| Field | Known-good | Failed candidate |
| --- | --- | --- |
| XEX flags | title module | title module |
| Base | `0x82000000` | `0x82000000` |
| Image size | `0x790000` | `0x1260000` |
| Entrypoint | `0x823626A0` | `0x82373390` |
| Title ID | `4D583336` | `4D583336` |
| System imports | `xam.xex`, `xboxkrnl.exe` | same |
| SDK versions | 2.0.21256.0 | same |

Both use the same XDK tool family and allowed media. The enlarged image and
shifted entrypoint are observations, not a demonstrated failure cause. The
2026-11-04 console report saw the candidate absent at one second and Aurora
again by three seconds; the startup log was stale. With the console now
unreachable, the failure cannot yet be assigned to loader, CRT, `main`, or
teardown. A fresh XBDM debugger notification capture and a new startup marker
from the exact candidate are required.

`RemoteTest.xex` prints `main entered`, one marker per second for twelve
seconds, then `main returning` through `OutputDebugStringA`. It is a separate
test title. Its build does not demonstrate that these messages appear on an
RGH console. Do not infer a pre-main failure solely from a missing debug line;
first establish that the debugger collects these notifications for a
known-good title.

`RemoteAgent.xex` is a heartbeat-only XDK DLL/XEX with no input hook or
network service. The XDK emitted `IM1034: missing export container for system
module` and `IM1040: final image may not run properly`. This is a build
warning with a real compatibility implication. It must not be put in
`launch.ini` or hot-unloaded until the export container and lifecycle are
resolved and a safe on-console recovery path is verified. The explicit stop
export is for a future verified loader; DashLaunch does not call it here.

No console deployment, debugger notification capture, title transition,
heartbeat observation, controller action, or game session was performed in
this stage.
