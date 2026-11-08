# Game video/audio capture experiment

`RemoteAV.exe` is an isolated Windows .NET 4 / Visual Studio 2010 receiver
for the observable HY37 protocol in the user's existing 360Stream package.
It displays complete JPEG/I420 frames, plays stereo PCM through WinMM and
writes a bounded session summary. It does not depend on MoonX360, Sunshine,
the RemoteAgent experiment, or XBDM to receive media.

**There is no newly compiled Xbox capture XEX in this stage.** The available
360Stream package contains its Xbox binary, not the Xbox source or an explicit
redistribution license. A from-source replacement is blocked on capture-hook,
loader and lifecycle work described in [the architecture audit](360STREAM_ANALYSIS_20261108.md).
The existing RemoteTest and RemoteAgent builds cannot capture a running game.
The standalone launch failure and agent export warnings remain unresolved.

## Build and host tests

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File xbox_remote_dev/av/build.ps1 -Test
```

The application is `xbox_remote_dev/build/av/RemoteAV.exe`. The separate
`manifest.json` records source commit, source hashes, application SHA-256 and
whether the AV source differs from the commit. `RemoteAV.csproj` can be opened
with VS2010. Tests cover framing, truncated media, JPEG dimensions, I420
colours, PCM format limits, reconnect, sequence wrap and unload acknowledgement
through a real loopback TCP session. Loopback tests mute playback and are
**simulation**, not an Xbox/game capture or an audible playback test.

## Prepare personal local files

The audited original XEX is expected at
`%USERPROFILE%\Downloads\360Stream(1)\USB\Xbox360Stream.xex`. To stage that
user-provided binary and a new configuration outside OneDrive:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File xbox_remote_dev/av/prepare-local.ps1 -PcIp 192.168.15.8
```

The script verifies the audited binary's hash, checks the PC address belongs
to this machine, and creates a unique directory below
`%LOCALAPPDATA%\XboxRemoteDev\av-staging`. It does not upload, install, load or
unload a module, edit `launch.ini`, or change firewall rules. The original
capture binary is not copied into Git or a distributable project package.

## Physical game test (pending)

1. Verify that the console has an existing module loader capable of loading
   and unloading a DLL while a game runs, and that local console recovery is
   available. Loading this XEX through Aurora's ordinary title launcher does
   not establish the required in-game module load. The release author says to
   start the module while the game is running.
2. Place the staged original `Xbox360Stream.xex` and `PERFORMER-AV.ini` at the
   USB root expected by 360Stream. Preserve any existing files first. This
   remains a manual, temporary experiment; do not add it to `launch.ini`.
3. Open `RemoteAV.exe`. Select PC `192.168.15.8`, expected console
   `192.168.15.10`, and the 360p profile. Click **Listen**. If Windows requests
   firewall access, use the trusted private LAN only; the application does not
   grant itself network access. No Internet port forwarding is needed.
4. Start a game locally, then load the original capture DLL using the console's
   existing module loader. The Xbox should connect out to PC TCP 36000.
   Receiver compatibility with this real binary is still unverified.
5. Check that the picture actually shows the current game and that the sound
   matches it. Move using a physical controller. **Next audio source** cycles
   through driver IDs if the selected source is silent. Use **Save displayed
   frame** and **Save report** to retain evidence locally.
6. Record a short fixed scene at 360p, then repeat in a separate session at
   480p. The requested default is 10 FPS / Q45 / one JPEG worker / capture C4;
   inspect actual received dimensions and counts rather than assuming the
   module accepted these settings. No CPU choice is known optimal for this
   console/game. Measure game FPS/response separately from capture FPS.
7. Before leaving the game or unloading the DLL, click **Prepare module
   unload**. Keep the receiver connected until the original module reports
   **SAFE_TO_UNLOAD**. Then unload it manually using the same module loader.
   The acknowledgement is a third-party status, not our proof of safe teardown.
   If it never arrives, do not hot-unload or switch titles; use the established
   local recovery procedure. A dropped TCP connection is not an acknowledgement.

The author explicitly warns that unprepared unload can crash the console:
[360Stream release instructions](https://www.reddit.com/r/360hacks/comments/1vpg5jj/360stream_beta_release/).
Its compatibility with other resident plugins, title transitions and this
specific game is unverified. No automatic module installation is provided.

## Reports and limits

Reports and explicitly saved PNGs stay under
`%LOCALAPPDATA%\XboxRemoteDev\av-evidence`, outside the repository. Reports
count only complete media payloads; incomplete EOF includes expected/received
bytes. Preview retains one frame, timing histories retain 600 samples, audio
uses four buffers and at most 100 ms of queued PCM, and driver IDs are capped
at 32. Reconnect resets the session and unload acknowledgement.

The report distinguishes received PCM, PCM submitted to the Windows device,
and playback errors. Audibility and game identity still require observation.
Capture/encode times come from the external module and are not independently
verified CPU/GPU cost. HY37 has no common audio/video presentation clock;
this receiver does not claim lip sync, one-way latency or Remote Play.
No controller input is sent. A successful PC simulation is not a physical
console test. Existing XBDM screenshot truncation is also still unresolved.
