# A/V receiver validation - 2026-11-08

Later user setup and USB IP corrections are recorded separately in
[SETUP_DIAGNOSTIC_20261108.md](SETUP_DIAGNOSTIC_20261108.md). The host build
and earlier validation results below retain their original scope.

The selected task is capture of a running game's picture and sound. This
stage implements the PC receiver and prepares a user-provided third-party
capture XEX for a manual experiment. It does not implement a new from-source
Xbox game capture module and does not establish a successful physical test.

## Exact final build

| Field | Value |
| --- | --- |
| Source commit | `54df2afc9c31f20eab9226e635de422a8ab777ba` |
| Application | `xbox_remote_dev/build/av/RemoteAV.exe` |
| Bytes | 34,304 |
| SHA-256 | `26282bbb79bf7e790d87ac3e08c36e623648796c9c96efd8c052a2bedaa4091a` |
| Toolchain | .NET Framework 4 C# compiler, x86 Windows Forms; VS2010 project supplied |
| Manifest | `xbox_remote_dev/build/av/manifest.json`, source hashes included, AV tree clean at build |
| Hardware flag | `hardware_tested: false` |

`av/build.ps1 -Test` compiled the application and ran **14 passing host tests**.
The actual receiver accepted fragmented JPEG, I420, PCM and status packets
through loopback TCP, decoded two frames, counted complete PCM, sent `X`,
recognized the requested unload acknowledgement, reconnected with cleared
state, and rejected the second connection's partial PCM payload with exact
`1023/1024` byte counts. Audio playback was muted in that test. This is
simulation; it proves neither Xbox capture nor audible Windows playback.

Other tests covered a fixed network-order header vector, clean/partial EOF,
empty status frames, oversized payload rejection, invalid codec/PCM formats,
exact I420 length and BT.601 colour conversion, JPEG dimensions/truncation,
and unload acknowledgement gating. The existing nine Python XBDM/input-state
host tests also passed. No capture benchmark was substituted by these tests.

The final application was rendered to an offscreen image and visually
inspected for layout and readable controls. It was then opened as an
interactive Windows application; the process remained running and exposed
the expected window title. No real media had been received at this stage.

## Personal local test material

`av/prepare-local.ps1 -PcIp 192.168.15.8` verified the original third-party
XEX hash and created a new personal staging directory under
`%LOCALAPPDATA%\XboxRemoteDev\av-staging\20261108-193300-000`.
It contains the original `Xbox360Stream.xex`, a fresh `PERFORMER-AV.ini`
pointing to `192.168.15.8`, and `provenance.json`. No file from this directory
was installed or loaded on the console. The third-party capture XEX is not
included in the repository or our build output.

Original XEX SHA-256:
`f9ca633356554a0d5f8c2fd5a027ca075d5ac6db91dae6fc5b3c4527c59374fb`.
It is 61,440 bytes, an exports-to-title DLL, not a standalone test title.
The available package does not include its Xbox source or an explicit
redistribution license. Its hook implementation and compatibility remain
unverified; see [the source audit](360STREAM_ANALYSIS_20261108.md).

## Remaining physical work

The console user reported having no known module loader. A fresh read-only
XBDM preflight returned `Aurora.xex`, debugger `start`, and a `help` command
list with no advertised `loadmodule` or `unloadmodule` command. No 360Stream
or XBGuard module was observed in the module list. This does not prove that
no unloaded loader exists on storage; it establishes that this session has
no verified in-game load/unload path. No unknown extension command was
invoked, and no loader or plugin was installed. The physical capture test
remains gated on establishing that path.

- Identify an existing in-game module loader and a local recovery procedure.
- Open a game and manually load the original capture module as a temporary
  experiment, then receive complete video and PCM in `RemoteAV.exe`.
- Confirm that picture and audible sound belong to the live game. Inspect
  360p and 480p separately, retaining session reports and a selected PNG.
- Measure game overhead independently; module-reported capture/encode times
  are not full CPU/GPU measurements or end-to-end latency.
- Request preparation, receive `SAFE_TO_UNLOAD`, manually unload and verify
  recovery. Do not infer safe unload from socket close or timer expiry.

No plugin installation, launch.ini edit, title launch, framebuffer register
patch or audio hook was performed by this stage. The preserved local
known-good MoonX360 XEX still hashes to
`0e62c46c05b59bdb39cf9075c30edbc074a9f559168d7e502e2029ffcdaeeb44`.
The RemoteTest startup failure, RemoteAgent system-module warnings and
XBDM screenshot truncation remain unresolved. No Remote Play claim is made.
