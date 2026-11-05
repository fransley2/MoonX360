# Xbox Remote Dev validation — 2026-11-05

Source commit for the local artifacts below:
`9e867b7fab2c36c56923c2c0e51a6dbfb2ba9f9f`.
All builds are isolated under `xbox_remote_dev/build/` and have local
`manifest.json` files with hashes. No console files or configuration were
changed.

| Artifact | SHA-256 | Local evidence |
| --- | --- | --- |
| `RemoteAgent.xex` | `61fd90477cebf7761fac23c074e6a537b870d3fa9341e4232d7cc468e9152682` | XEX2, DLL module at `0x81C00000`; `imagexex` warned IM1034/IM1040. Not installation-ready. |
| `RemoteTest.xex` | `ed68eb1f690d886bb4f7f38de4cc0ef1df0766c748326c8c36c5c7cdac1ceb1f` | XEX2, title module at `0x82000000`, title ID `58524454`. No imagexex warning. |
| `RemoteDev.exe` | `3856cf9ad2e67789674124a5db15cde54579b8e988072153f69e75b89905d112` | .NET 4 C# build passed with both `csc.exe` and MSBuild; read-only UI. |

Local Python tests: **8 passed**. They cover multiline status, a `414`
error, `getfile` length, complete/truncated screenshot transfer, PNG refusal
on incomplete data, input watchdog, stale sequence and physical takeover.
The Xenos tiled address calculation matched OXDK's client over every point
in a 128×128 grid. This is a host comparison, not a console screenshot.

The preserved known-good MoonX360 XEX still has SHA-256
`0e62c46c05b59bdb39cf9075c30edbc074a9f559168d7e502e2029ffcdaeeb44`.
The original dirty MoonX360 source tree was not staged into these commits.

## Hardware state and gates

`Test-NetConnection 192.168.15.10 -Port 730` returned
`TcpTestSucceeded=False`; ping reported `DestinationHostUnreachable`.
Therefore there was no fresh XBDM command, launch.ini backup, screenshot,
module check, debug notification, plugin heartbeat, title transition,
Aurora/game input, latency measurement, or repeated recovery test.

When the console becomes reachable, collect a fresh read-only diagnosis in a
new directory and compare its state with the independent OXDK XBDM client.
Try screenshot only after confirming the debugger is in `start`, record the
announced and received lengths, then check debugger state again. Investigate
`414` separately for `HDD:` and `Hdd1:`; do not assume alias equivalence.

For startup, collect debugger notifications from a known-good title first,
then run `RemoteTest.xex` under a new name and verify `main entered` plus the
module list at intervals. Repeat with the exact failed candidate only after
the logging path is proven. Preserve `default.xex` throughout.

Before installing any plugin, locate and back up the active `launch.ini`,
confirm a local physical recovery method, resolve the RemoteAgent system DLL
export warning, and inspect its load/unload behavior on the actual dashboard.
Only then add a separately authenticated input service; test neutral input on
disconnect and physical-controller takeover in Aurora and a game. Video and
audio work starts after a complete screenshot and remains optional.

The current deliverables do **not** meet remote administration, remote
controller or Remote Play acceptance on hardware. The Windows app has no
privileged pairing or launch controls; the input model does not inject into
XAM. The plugin build is not evidence of residence across titles.
