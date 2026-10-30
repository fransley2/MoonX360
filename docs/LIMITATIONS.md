# Limitations and validation status

MoonX360 is experimental homebrew for Xbox 360. Its Xenon CPU, Xenos GPU, shared memory and legacy tooling constrain throughput and maintainability. It streams a compatible PC; direct Xbox Cloud Gaming, browser/cloud-service login and a hosted game catalog are not implemented.

## CPU and GPU boundaries

H.264 parsing/decoding runs in FFmpeg on the CPU, using slice workers and selected VMX CPU routines (`xdk/src/video_xdk.cpp`, `xdk/ffmpeg_xbox/h264_vmx.c`). VMX is not GPU decoding. Opus decoding and audio preparation also run on the CPU; XAudio2 handles dashboard audio output. The GPU handles textures, YUV conversion, scaling, image processing and UI presentation through the XDK Direct3D backend. There is no implemented H.264 GPU decoder.

The cached video path has three fenced texture slots and presents the newest ready frame. It can skip presentation while decoding continues; fallback staging/upload remains for failures. This is an implementation description, not proof of stall-free hardware execution or bounded memory usage in every failure case.

## Configuration is not measured performance

Fresh defaults request 720p60 at 30 Mbps; preset values are listed in [SETUP.md](SETUP.md). Target FPS/resolution/bitrate do not guarantee received, decoded or presented rates. **1080p Smooth**, **Smooth**, **Performance**, **Standard** and **Maximum Quality** are configuration names, not measured results. No stable 1080p60 or constant-60-FPS claim is made. There is no universally validated recommended preset for this snapshot.

Host encoder settings and slice layout, image content/motion, entropy coding, bitrate, network contention, console configuration, display refresh and decoder scheduling all affect performance. Higher bitrate can increase network and decoding work. Lower resolution alone does not establish improved playback if the bottleneck is elsewhere. V-Sync trades tearing against presentation latency, and requested rates which do not divide the physical refresh can have uneven cadence.

Fast Decode skips H.264 deblocking and can show more blocking; it does not reduce resolution or move decoding to the GPU. With Fast Decode off, overload recovery can reduce non-reference-frame filtering temporarily. GPU **Image quality** is separate. Auto Quality uses CPU submission-time observations, not GPU timestamps or utilization. Automatic quality adjustment can reconnect with lighter settings and is not seamless adaptation or a frame-rate guarantee.

Statistics have limited scope: payload bitrate excludes link overhead; skipped displays are not packet-loss measurements; packet-loss/FEC/jitter counters are unavailable. Developer percentiles describe bounded rolling samples, not an entire long session. Build policies called aggressive/compatible and synthetic benchmark scores do not prove improved real-console playback.

## Confirmed discrepancies and incomplete areas

- Some existing in-app help calls Standard/30 FPS the new-install default. Current `config_defaults()` instead selects Custom/720p60/30 Mbps. Documentation follows the code; application text is preserved in this source-only publication.
- Per-game bindings to a named profile must be reassigned after that profile is renamed or deleted; the UI already warns about this.
- Diagnostic checkpoints are not crash backtraces. Sudden failures can lose RAM logs. Reports and host fixtures cannot establish crash freedom.
- Verified settings-file replacement does not establish FATX power-loss atomicity. Preserve backups and both named-profile generations.
- The Xbox presentation code forces GPU rendering. Shared CPU renderer labels/code should not be read as selectable dashboard features.
- The legacy XeLL build has an unpinned Moonlight master/toolchain path and was not built here. It differs in network, input and storage behavior from the dashboard backend.
- Dependency Git provenance is incomplete. Version evidence and local adapted source are supplied, but historical abbreviated revisions are not authenticated pins. Old cryptographic/codec libraries and an external proprietary XDK remain maintenance constraints.

The above are confirmed source/maintenance limitations. This publication does not import historical crash or performance reports as newly reproduced bugs. No additional console-specific failure is claimed to have been reproduced during preparation.

## Hardware and compatibility evidence

This publication completed a clean dashboard build, embedded-resource checks and desktop simulated regression suites; see [PUBLICATION.md](PUBLICATION.md). No physical Xbox/LAN test was performed for the published snapshot. Wi-Fi, multicast discovery on different routers, wake behavior, repeated Guide transitions, controller/keyboard devices, 5.1 channel output, cached-texture coherency/fences, reconnect/recovery and long sessions still require validation with the actual console and host.

Upstream Xenonlight reports Sunshine pairing and 720p30 hardware streaming. That report is not a MoonX360 server-version compatibility matrix. Apollo, Vibepollo, other GameStream hosts, encoder/GPU combinations and all preset combinations are unverified here unless a future report explicitly identifies build, hardware, settings, workload and duration. Source-level host-name strings are not sufficient evidence of compatibility.

When submitting measurements, distinguish requested settings, received/decoded/shown frame rates, wall-clock CPU timings and physical presentation. Include sampling duration and context. Do not reuse historical averages without their original conditions or claim stability from a short sample.
