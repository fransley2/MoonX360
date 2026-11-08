# Physical capture attempt - 2026-11-08

Result: **no media connection, decoded image, or received audio was observed**.
The console recovered from one authorized cold reboot with XBDM, Aurora and
the original 360Stream module present. The corrected IP and intact XEX were
verified by full file readback. Boot-time module presence did not establish
a functioning capture service. No game-capture or Remote Play claim is made.

## Procedure and observations

The user authorized autonomous testing with the Xbox left powered on.
Preflight reported `default.xex`, the capture DLL and XBDM loaded, debugger
`start`, correct USB PC_IP `192.168.15.8`, and a PC listener without a media
connection. The existing idle viewer was closed gracefully after verifying
there was no established TCP session. No unrelated process was closed.

`observe-360stream.py` then ran the audited personal viewer in-process with
its normal UI and receiver. It selected 640x360 / 10 FPS / Q45 / one worker /
capture C4, enabled bounded-duration WAV evidence, and saved periodic status.
Its planned second phase was 640x480, followed by an explicit prepare-unload
request and acknowledgement check. Those phases require a media connection;
they were not reached. No probe client consumed the original single-accept
receiver. No synthetic images or sound were supplied to the live observer.

Times below use America/Sao_Paulo (UTC-03:00):

| Time | Observation |
| --- | --- |
| 20:33:47 | Viewer ready and TCP 36000 listening |
| 20:34:05 | One `magicboot cold` command; XBDM replied 200. The reply alone was not treated as reboot/title proof |
| 20:34:27 | First post-command XBDM query timed out |
| 20:35:22 | XBDM returned modules including Aurora, XBDM and `Xbox360Stream.xex`; debugger `start`; FTP banner also returned |
| 20:36:17 | 150-second receiver observation completed with zero accepted media connections, zero audio packets/bytes, no decoded PNG and no WAV |

The observation window includes the reboot interval; it is not 150 seconds
of verified post-boot streaming. At least about 55 seconds followed the first
successful post-boot module query before the bounded observation ended.
The receiver remained listening at the final passive check, with no later
connection event in its log.

The final console query reported debugger `start`, XBDM and the capture DLL,
with `default.xex` active again. That title change was not commanded by this
test. The agent issued no `magicboot title=...`, controller input, screenshot,
hot-unload, memory write or additional reboot. The failed media connection
prevented a prepare request, so no unload acknowledgement was obtained.

## Read-only initialization diagnostics

Module sections were enumerated through XBDM. Read-only hex transfers of
the reported `.rdata` (7,876 bytes) and `.data` (169,936 bytes) were validated
against the requested lengths and saved privately. No unreadable bytes were
padded or patched. The module's constants included USB/PAV INI paths and
statuses such as CONFIG_USB0_INI_OK and CONFIG_PC_IP_CACHED. Finding a compiled
status string is not evidence that its code path executed.

In the sampled thread list, no direct thread start address fell inside the
module's reported code section. Generic system wrappers can start workers,
so that absence does not prove no capture thread existed. The runtime IP was
not established by this bounded memory inspection. Debugger state remained
`start`; no breakpoints or execution stops were used.

The cause is still unresolved. Failure to initialize in the boot-time loading
context is a hypothesis consistent with the author's documented in-game
loading procedure, not a demonstrated root cause. A network problem is also
not excluded solely by the presence of a Windows allow rule. Further cold
reboots or title switches without a new verified mechanism are not justified
by this result.

## Provenance and evidence

Observer source commit: `5127312`.

| Input | Verified SHA-256 |
| --- | --- |
| Observer source | `4672ee31fbc3e8b62807c87c6bec06696c4d5449c457fa2eae8c5d2169d5f623` |
| Personal patched viewer | `f5530813079b892217a1c39469d1ac4a8ca562964fbcd1bf66b6954f6be669ab` |
| USB capture XEX, 61,440 bytes | `f9ca633356554a0d5f8c2fd5a027ca075d5ac6db91dae6fc5b3c4527c59374fb` |
| USB launch.ini, 12,851 bytes | `151a3532648da791ece3934bcf9c95edca9ac5ce017961ff18b60e258d30bc54` |
| USB media INI, 218 bytes | `54de4fb24a402480e303c6915a8ca5f7dc46d6e5748a6e44417a4f59b91f0f6e` |

No console file or startup configuration was changed in this test. No Xbox
capture binary was compiled or installed. The full evidence is private at
`%LOCALAPPDATA%\XboxRemoteDev\av-evidence\physical-20261108-203346`:
ready/progress/finished JSON, viewer event log, reboot and post-boot module
records, thread information, private module reads and final provenance.
Raw third-party module memory is excluded from Git and distribution.

The observer's syntax and live startup were verified; it produced 149 periodic
snapshots and a timeout report. The connection-dependent PNG/WAV capture,
profile switching and prepare-acknowledgement paths remain unvalidated on
hardware. The prior host tests do not substitute for those missing results.

Next dependency is a verified in-game load/unload path or a source-available
resident capture implementation with demonstrated initialization and recovery.
A complete screenshot, actual game audio, 360p/480p overhead and supported
title transitions remain acceptance work.
