# Xbox Remote Dev — experimental, isolated project

This directory does not change MoonX360's build or runtime. The preserved
physical-console control XEX is `xdk/build/steam-library-ab-20261101/default.xex`
(SHA-256 `0e62c46c05b59bdb39cf9075c30edbc074a9f559168d7e502e2029ffcdaeeb44`).
The working `default.xex` on the console must never be replaced by an
experimental build.

## Current stage

Phase 1 provides a bounded read-only XBDM client and screenshot receiver.
The XDK build also produces an experimental startup test and heartbeat-only
plugin. The Windows desktop project currently exposes read-only XBDM status,
modules, directory listing and an allowlisted terminal. The remote input
proof of concept is a local state model with a disconnect watchdog.

```powershell
py -B -m unittest discover -s xbox_remote_dev/tests -v
py -B xbox_remote_dev/host/diagnose.py 192.168.15.10 evidence/new-run
py -B xbox_remote_dev/host/diagnose.py 192.168.15.10 evidence/new-screen-run --screenshot
powershell -NoProfile -ExecutionPolicy Bypass -File xbox_remote_dev/windows/build.ps1
py -B xbox_remote_dev/xdk/build.py
py -B xbox_remote_dev/host/baseline.py 192.168.15.10 "$env:LOCALAPPDATA\XboxRemoteDev\recovery\new-baseline"
```

Use a **new** evidence directory for each run. Screenshot capture is explicit
because the installed XBDM previously stopped the debugger and truncated the
binary stream. The receiver writes `screenshot.partial` during transfer and
only makes `screenshot.png` after receiving the announced byte count. It logs
EOF/timeout and transfer counts in `diagnosis.json`, then queries the debugger
state. A complete transfer with an unsupported format keeps raw data and
reports a conversion error. A `200` reply to `magicboot` is not proof of a
running title; verify `modules` and `getexecstate` after every launch.
If screenshot changes a confirmed prior debugger state from `start` to
`stop`, the collector issues `go` and records the resulting state.

`HDD:` and `Hdd1:` are queried separately. The 2026-11-04 session showed
access through `HDD:` and `414` through `Hdd1:`; this may vary by XBDM and
title context. This tool does not assume they are interchangeable.

## Recovery

Before any future plugin installation, obtain a readable backup of
`launch.ini` from the console's actual active device, save its SHA-256 locally,
and verify a local recovery path using physical controller/USB access. Keep
the existing XBDM plugin and its configuration. Install experimental XEX files
under new names, never `default.xex`. To recover from a plugin boot issue,
boot with the established DashLaunch bypass/recovery method for this console,
remove only the new plugin entry, and restore the saved `launch.ini`. The
exact bypass sequence and launch.ini device are not yet verified on this unit.

No plugin installation or `launch.ini` edit is automated by this project.
The `RemoteAgent.xex` build currently reports an XDK system-module export
warning; see [STARTUP_REPORT.md](STARTUP_REPORT.md). Do not install it yet.
Fresh console findings are in [VALIDATION_REPORT_20261108.md](VALIDATION_REPORT_20261108.md).
The guarded standalone RemoteTest launch on 2026-11-08 returned to Aurora
within three seconds without a confirmed RemoteTest module load or startup
marker. Its remote hash could not be verified because XBDM readback failed.
This build is not hardware-validated as a working title.
The standalone `host/probe_remote_test.py` uploads only to a new dated name
and normally refuses to launch if its full remote hash cannot be verified.
When the installed XBDM truncates `getfile`, a separate, explicit
`--use-existing-upload --receipt <original-probe.json> --accept-size-only`
path checks the original upload record, local SHA-256, remote size, Aurora as
the active title and debugger `start` before launching. The report marks the
remote hash as unverified. Never use this path to interrupt an active title.

`RemoteDev.exe` has no privileged mode or pairing yet. Its UI disables launch,
file writes, terminal mutation and controller input until a verified agent and
authentication path exist. `PROTOCOL.md` describes the current protocol
boundary. The separate build manifests include source commit, artifact
SHA-256 and a `hardware_tested: false` field.

## Reference audit

| Project | Source/license observed | Relevance and limit |
| --- | --- | --- |
| [WirelessInput360](https://github.com/UncreativeXenon/WirelessInput360) | Xbox plugin and Windows source, MIT | Hooks XAM input using undocumented addresses/ordinals; supports specific dashboard builds and cannot be transplanted without console compatibility testing. |
| [x360controller](https://github.com/xecli/x360controller) | Windows/Android source MIT; current Xbox plugin distributed as binary | Useful host UX reference, not Xbox source for a new agent. |
| [hiddriver360](https://github.com/EinTim23/hiddriver360) | Xbox plugin source GPL-3.0 | USB HID virtual device research; its README says retail 17559 and devkit 17489, with no rumble support. License and device assumptions need review before any reuse. |
| [BetterXBDM](https://github.com/theaperturecat/BetterXBDM) | Source MIT | Replacement XBDM is WIP and its README documents thread-safety work remaining; do not install over working XBDM. Its screenshot code changes debugger state and emits a metadata line plus raw surface. |
| [OXDK](https://github.com/MrMilenko/OXDK) | XBDM client tool public domain; vendored components have separate licenses | Independent protocol and tiling reference, not proof that this console serves a complete screenshot. |
| [360Stream beta](https://www.reddit.com/r/360hacks/comments/1vpg5jj/360stream_beta_release/) | Binary release link; source not verified from the post | Author warns that unload without preparation can crash the console. No performance or compatibility claim is adopted. |

Only the protocol shape and public-domain tiling calculation are used here.
No third-party plugin or binary was copied into this repository.
