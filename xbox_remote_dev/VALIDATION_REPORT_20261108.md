# Xbox Remote Dev console continuation — 2026-11-08

Console `192.168.15.10`, XBDM TCP 730. All actions below used new evidence
paths. No installed `default.xex`, `launch.ini`, or plugin entry was changed.
The standalone test title was **not launched**.

## Read-only baseline

- `evidence/20261108-readonly-01/diagnosis.json`: XBDM reported
  `getexecstate=start`; `default.xex` was loaded at that instant. A later
  independent OXDK query reported `Aurora.xex`, so title identity must be
  treated as time-specific. XBDM remained usable afterward.
- `evidence/20261108-baseline-01/baseline.json`: `Usb0:\launch.ini` was
  readable and backed up locally (12,829 bytes, SHA-256
  `bca886d9dfd686c9fd1621225b5a354ef760a7a283f123b6d82f2830668c4d81`).
  Its plugin entries include XBDM and do not reference RemoteAgent. This is
  a candidate configuration backup; the active DashLaunch source was not
  independently proven. The local backup and directory listings are private
  and intentionally excluded from Git. The configuration backup was moved to
  `%LOCALAPPDATA%\XboxRemoteDev\recovery\20261108\Usb0-launch.ini` outside the
  synchronized checkout; its SHA-256 was rechecked after the move.
- `HDD:\Apps\RemoteAgent\RemoteAgent.xex` was read in full, 16,384 bytes.
  Its SHA-256 `61fd90477cebf7761fac23c074e6a537b870d3fa9341e4232d7cc468e9152682`
  matches the previous local artifact. It was absent from the module list.
- `HDD:\Apps\MoonX360\default.xex` could not be read via either our XBDM
  client or OXDK: both received `414- access denied`. Its on-console hash is
  unverified. `Hdd1:\Apps\MoonX360` returned `414` in the first listing and
  was readable in a later listing, so alias behavior is context-dependent.

## Binary transfer and screenshot

- `evidence/20261108-screenshot-01/diagnosis.json`: while Aurora was active,
  screenshot metadata announced a 1280×720 tiled surface with
  `framebuffersize=3,932,160` bytes. The connection closed after **1,460**
  bytes. The receiver kept `screenshot.partial` and refused PNG conversion.
  XBDM left the debugger in `stop`; `go` returned `200 OK`, followed by
  `getexecstate=start`.
- The independent OXDK client accepted another incomplete screenshot and
  wrote a 2,934-byte PNG. It too left the debugger in `stop`, which was again
  restored to `start`. That PNG is **not a valid full-frame capture**. OXDK's
  [screenshot implementation](https://github.com/MrMilenko/OXDK/blob/main/xbox360/tools/xbdm/xbdm.py)
  pads missing texels rather than verifying the announced length.
- The installed XBDM identifies itself as BetterXBDM in `help`. Its public
  [screenshot handler](https://github.com/theaperturecat/BetterXBDM/blob/d0d1071c5b8142545ae3d7350f8e443f05ee0107/xbdm/dmserv.c#L2014)
  stops execution, hands the framebuffer pointer to the send path, and
  resumes only on normal completion. This is consistent with the observed
  stop after a short transfer. It does not yet prove which low-level send
  failure closes the connection.
- The same source's
  [getfile handler](https://github.com/theaperturecat/BetterXBDM/blob/d0d1071c5b8142545ae3d7350f8e443f05ee0107/xbdm/dmserv.c#L2231)
  maps file-open failure to `XBDM_CANNOTACCESS` (`414`); a `414` alone does
  not distinguish permission, sharing, path or other open failures.
  The send path does not show a retry for a temporary `send` failure; this is
  a source-level hypothesis for truncated streams, not a demonstrated cause
  on the installed binary. No replacement XBDM was deployed.

## RemoteTest gate

`RemoteTest.xex` was uploaded under the new name
`HDD:\Apps\RemoteAgent\RemoteTest-20261108.xex`. XBDM returned upload
completion and directory size 102,400 bytes, matching the local file.
The readback then closed at **87,476/102,400** bytes. A later readback
returned `414`, including through OXDK. The remote SHA-256 is unverified.
`host/probe_remote_test.py` therefore refused `magicboot`; the last checks
still showed `getexecstate=start` and `Aurora.xex` loaded. The copied test
file remains under its unique name and must not be mistaken for a validated
title. No other file was overwritten.

A second probe allowed a size-only test after the failed readback, but its
preflight found `default.xex` active instead of Aurora and refused to launch.
The console title was not interrupted. After the screenshot investigation,
the collector was updated to resume a debugger that was confirmed running
before capture and stopped afterward; this behavior has not yet been tested
on the console.

Aurora FTP accepted a TCP connection but rejected anonymous login with
`530 Need username`. No credentials were guessed or sent. A separately
authorized, read-only FTP download or another complete-file verification path
could unblock the standalone test. The resident agent still has the XDK
`IM1034/IM1040` export-container warning and must not be installed.

A host-only `imagexex` experiment without `<sysdll/>` removed the warning but
produced a title-plus-DLL image at `0x88000000`, which does not establish a
DashLaunch-compatible resident plugin. That variant was not installed or
added to the project build.
Adding `<exportnames/>` preserved both warnings. The installed XDK's ImageXEX
error-code documentation labels IM1034 "No Exp Container" and IM1040 "Image
May Not Run". The heartbeat plugin remains an uninstalled experiment.
