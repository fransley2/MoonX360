# 360Stream setup diagnosis - 2026-11-08

The PC receiver startup and USB destination were repaired. The original
capture DLL was observed loaded under Aurora after the user's restart, but
no media connection or game A/V session was established. The user deferred
the next restart and capture test. Findings below describe that session,
not the console's current live state.

## Findings and corrections

| Item | Evidence/action |
| --- | --- |
| Addresses | Ethernet `192.168.15.8`; Cloudflare WARP `172.16.0.2` |
| Ports | Original viewer binds TCP 36000. Console TCP 21 returned `220 FtpDll Ready`; XBDM uses TCP 730 |
| Initial PC state | No process listening on 36000. Default Python 3.15 lacked Pillow/sounddevice; 3.14 had Pillow only |
| Repaired runtime | Isolated Python 3.12.10, Pillow 13.0.0, sounddevice 0.5.6, cffi 2.1.1, pycparser 3.12, outside OneDrive |
| IP selection defect | Original function returned WARP before Ethernet. Local patch now returns `192.168.15.8` and passes five address-order fixtures |
| Audio device | Accepted stereo 48 kHz / signed 16-bit settings; audible playback was not tested |
| Firewall | Read-only netsh output showed enabled TCP allow rules for the actual Python 3.12 executable on private/public profiles. No rules or network categories were changed. This does not prove connectivity |
| USB XEX | Read all 61,440 bytes; SHA-256 `f9ca633356554a0d5f8c2fd5a027ca075d5ac6db91dae6fc5b3c4527c59374fb`, matching the original |

The patched viewer opened successfully. Its startup record at
`%LOCALAPPDATA%\XboxRemoteDev\360stream-viewer\runs\20261108-201802-651\startup.json`
recorded `listener_verified: true`. The listener belonged to the child Python
interpreter. No probe client was connected, since that would consume the
original viewer's single acceptance. No media connection was observed before
the deferred test.

## Boot configuration and module observation

The first USB launch.ini read matched the prior backup: 12,829 bytes,
SHA-256 `bca886d9dfd686c9fd1621225b5a354ef760a7a283f123b6d82f2830668c4d81`,
with XBDM and the USB patch in slots 1-2 and slots 3-5 empty. The user reported
saving a DashLaunch change and requested an edit. During preparation, a fresh
read found `plugin3 = Usb:\Xbox360Stream.xex` already present.

Our first activation attempt refused the occupied slot before any remote
upload or rename. The later idempotent check recorded `applied: false` and
`already configured and verified; no remote write`. The observed launch.ini
hash was `151a3532648da791ece3934bcf9c95edca9ac5ce017961ff18b60e258d30bc54`.
XBDM and the USB patch entries remained present. The
[DashLaunch sample](https://github.com/cOzInABox/dashLaunch/blob/main/ReadMe/info_launch.ini)
documents `Usb:` plugin paths; XBDM used `Usb0:` for file access here.

The user restarted the console. TCP 21/730 briefly stopped responding, then
returned. XBDM modules included `Xbox360Stream.xex` at `0x91D00000`, image
size `0x48000`, alongside XBDM and Aurora. Debugger state was `start`. This
proves observed module presence under Aurora, not working capture callbacks,
safe title transitions or game support. The agent issued no reboot, module
load, hot-unload or title-launch command.

## USB media INI repair and provenance

The USB INI initially contained `192.168.15.8`. After the restart its PC_IP
value was `172.16.0.2`, matching the original viewer's suggested WARP address.
The observation does not establish who or what saved that value.

At source commit `ef8613e6e104c8456d8a702ed1258a8b45e7dfa6`, only PC_IP in
`Usb0:\PERFORMER-AV.ini` was changed to `192.168.15.8`. The staged file,
promoted active file and renamed backup were all read back and verified.

| Material | SHA-256/location |
| --- | --- |
| Before media INI | `2fc857e299cd61828ebb08f25d405207e154f160e9bcf0e9a28f0927b17fade7` |
| Corrected media INI | `54de4fb24a402480e303c6915a8ca5f7dc46d6e5748a6e44417a4f59b91f0f6e` |
| Remote pre-repair backup | `Usb0:\xrd-av-bak-20261108-231614.ini` (old WARP value) |
| Local INIs/report | `%LOCALAPPDATA%\XboxRemoteDev\recovery\av-ip-fix-20261108-231614` |
| Original Python viewer | `7be4cf4c559b698480d5192591cf97463e89e88dbb1d6efe02bcef41a937a48b` |
| Patched personal viewer | `f5530813079b892217a1c39469d1ac4a8ca562964fbcd1bf66b6954f6be669ab` |

The original viewer backup is
`%LOCALAPPDATA%\XboxRemoteDev\360stream-viewer\PC-PERFORMER-AV-v38.original.py`.
The personal patch changes IP candidate ranking only. No third-party Xbox
binary was rebuilt, patched or redistributed; the full viewer remains outside
the tracked project.

## Recovery and pending hardware test

The original pre-360Stream launch.ini is ready at
`%LOCALAPPDATA%\XboxRemoteDev\recovery\360stream-20261108-231115\restore-usb\launch.ini`.
Its SHA-256 is the original `bca886d9...c4d81` above. For offline recovery,
attach the pendrive to the PC with the console powered off and restore that
file as the USB root launch.ini. It preserves the previous XBDM/USB-patch
entries and omits the capture DLL. No console bypass sequence was validated.
A planned extra boot-INI backup upload was not performed because its
preflight coincided with the user's reboot; do not assume it exists on USB.

The module was already loaded when PC_IP was corrected; runtime rereading of
that setting is unknown. The next restart was deferred, so connectivity with
the corrected destination remains untested. The
[capture author's instructions](https://www.reddit.com/r/360hacks/comments/1vpg5jj/360stream_beta_release/)
request in-game loading and preparation before unload. Boot-time module
presence does not establish safe title transitions. A controlled physical
session still needs complete video, audible matching game audio, measured
overhead and prepared unloading.

Host checks: 16 Python transport/configuration tests passed, including
promotion-failure rollback and preserving boot configuration during media
INI repair. Five IP-order fixtures passed against the actual patched function.
These are host checks, not Xbox capture evidence. No new RemoteAV.exe,
RemoteTest.xex or RemoteAgent.xex build was made during this setup repair.
