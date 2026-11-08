# 360Stream architecture and source audit - 2026-11-08

Scope: capture a running Xbox 360 game's video and audio to a Windows PC.
The user explicitly selected game capture rather than a standalone pattern
and generated tone. No third-party Xbox module was installed or loaded during
this audit. The console's read-only module query reported Aurora and XBDM;
no capture module was observed at that instant.

## Evidence and availability

The locally supplied `Downloads/360Stream.zip` contains only:

| File | Finding |
| --- | --- |
| `PC/PC-PERFORMER-AV-v38.py` | Inspectable Python receiver source; SHA-256 `7be4cf4c559b698480d5192591cf97463e89e88dbb1d6efe02bcef41a937a48b` |
| `PC/README.txt` | Installation and tuning instructions; no explicit software license |
| `USB/PERFORMER-AV.ini` | `PC_IP` destination setting |
| `USB/Xbox360Stream.xex` | 61,440-byte compressed/encrypted Xbox DLL; SHA-256 `f9ca633356554a0d5f8c2fd5a027ca075d5ac6db91dae6fc5b3c4527c59374fb` |

No C/C++ Xbox source or explicit license file was in this ZIP. The
[release post](https://www.reddit.com/r/360hacks/comments/1vpg5jj/360stream_beta_release/)
links the binary ZIP, requests in-game module loading and warns about crash
risk on unprepared unload. A public source repository for this capture binary
was not verified in the targeted search; this is an availability finding,
not proof that none exists. The package is not assumed open source or
licensed for redistribution. None of its source code or binaries was copied
into the project. The new receiver independently implements wire facts.

XDK `imagexex /dump` on the original Xbox binary reports module flags `0xA`
(DLL, exports to title), base `0x91D00000`, image size `0x48000`, entrypoint
`0x91D0FE40`, 4 KB pages, and imports from `xam.xex`/`xboxkrnl.exe` version
2.0.21256.0 (minimum 2.0.1861.0). Those headers do not establish compatibility
with this kernel, DashLaunch configuration, or other plugins. Its binary
format is different from a standalone title and from the current RemoteAgent
system-module experiment. No exports, hooks, offsets or lifecycle behavior
were reconstructed from the compressed binary.

## Architecture observed at the PC boundary

```text
running Xbox game + externally loaded capture DLL
    | outbound TCP to PC_IP:36000
    | HY37 status / JPEG or I420 video / stereo PCM audio
    v
Windows receiver --> JPEG decode or BT.601 I420 conversion --> latest image
                 --> bounded stereo PCM playback queue --> Windows output
    | capture settings, audio settings, prepare-unload request
    +-----------------------------------------------------> Xbox DLL
```

The Python viewer listens on all local interfaces. It depends on Pillow,
optionally OpenCV/NumPy for I420, and optionally sounddevice for playback.
It exposes capture CPU/JPEG worker counts, JPEG quality/sampling, raw I420,
audio service CPU and rate/bit depth. Those controls demonstrate a configurable
interface; they do not reveal the Xbox implementation or prove any advertised
FPS or latency. The receiver exposes statuses such as `AUDIO_HOOK_INSTALLED`,
`STOPPING_AUDIO`, `STOPPING_WORKERS`, and `SAFE_TO_UNLOAD`. Audio interception
and worker teardown are therefore protocol-level expectations, not validated
hook implementation details.

Video and audio share a TCP byte stream. Large JPEG/raw frames can delay
audio through stream ordering. The PC preview queue keeps one frame and the
original audio queue keeps four packets. Multiple audio driver IDs can be
reported. Sequence numbers apply to video; for audio, one header field is a
driver ID. There is no shared presentation timestamp or clock synchronization
in the visible header, so cross-device end-to-end latency and lip sync cannot
be established from these counters alone.

## XDK and public-source limits for a replacement XEX

The installed XDK headers confirm `Direct3DCreate9`, `CreateDevice`,
`D3DDevice_Resolve` and texture locking for a title's own graphics resources.
XAudio2 can play audio owned by that title. Those APIs alone do not supply a
verified cross-title capture service to a separate resident module. Searches
of the installed audio/XBDM headers did not establish a documented game-output
loopback API appropriate for this task.

[Xenia's kernel audio implementation](https://github.com/xenia-project/xenia/blob/master/src/xenia/kernel/xboxkrnl/xboxkrnl_audio.cc)
models render-driver registration and sample submission. It is useful primary
research about an import boundary, not evidence that a resident capture tap
is a supported XDK API or safe to install on hardware. No ordinals or
function prologues from emulator research were transplanted here.

The public [BetterXBDM screenshot handler](https://github.com/theaperturecat/BetterXBDM/blob/d0d1071c5b8142545ae3d7350f8e443f05ee0107/xbdm/dmserv.c#L2047)
uses GPU register/framebuffer addresses and stops execution. The installed
XBDM already truncates screenshot transfers and can leave the debugger stopped.
That path was not adopted as a live game capture implementation. Its register
values are not proof of a portable capture backend.

A new from-source resident capture XEX still requires a verified title/DLL
build format, capture acquisition and audio interception mechanisms,
bounded callback work, worker ownership, quiescence acknowledgement, explicit
unhooking, kernel/title compatibility guards and local recovery validation.
The current RemoteAgent has unresolved IM1034/IM1040 warnings; it remains
uninstalled. The standalone RemoteTest attempt returned to Aurora without
confirmed load. Adding guessed hooks would not resolve those dependencies.

## Implemented test boundary

`RemoteAV.exe` binds a selected private IPv4 address and accepts only the
configured Xbox peer. It uses independent HY37 framing/validation, checks
complete JPEG/I420 frames, plays bounded PCM through Windows WinMM, keeps
bounded statistics and provides an explicit prepare-unload command. It
does not load a module or change a console file or launch configuration.
It does not implement gamepad input, privileged RPC, or authentication for
HY37 (the existing external module has no verified pairing protocol).

The XEX available for a manual physical capture experiment is the user's
**original third-party binary**, staged locally with provenance. It is not
our compiled Xbox capture artifact, and compatibility remains unverified.
The exact installed title, actual picture/audio, title-frame-rate impact,
capture/encode costs and unload behavior must be observed during a physical
game session before making performance or Remote Play claims.
