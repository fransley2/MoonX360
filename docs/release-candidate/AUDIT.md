# Stabilization audit — 2026-11-08

This audit describes the local working tree, not just committed HEAD. Classification is an engineering disposition based on source and desktop fixtures. **KEEP never means physically validated.** All real console behavior is **PENDING PHYSICAL XBOX 360 VALIDATION**.

## Protected baseline

Branch: `release/stabilization-fastdecode`, created from `feat/quick-panel-h264-recovery` at `939c476d754c9d3570bce5f42407bb04cab02e72`. The checkout already contained extensive uncommitted dashboard, recovery, Aurora, decoder and fixture work. No reset, commit, tag, remote-console operation or publication was performed. Pre-edit modified/untracked sources, the tracked diff and status are saved under `build/release-candidate/evidence`. The previous distributable `dist/MoonX360/default.xex` was copied and hashed before edits: 6,037,504 bytes, SHA-256 `f830e08de52a79e9f337d301e7ec4e0e823b705dca3b341a862968705856e27c`. Its physical working status cannot be established from a local file. HEAD is a provenance revision, not a claim of a known-good hardware revision.

Baseline defaults: 1280×720, 60 requested FPS, 12 Mbps, packet size 1024, Smooth preset, Boolean Fast Decode enabled, GPU renderer, VSync enabled, four slice workers with the existing balanced layout. The old production decode policy enabled FAST and `AVDISCARD_ALL` for Fast Decode; Off could switch to NONREF filtering under deadline pressure. Diagnostic build overrides were already present. Baseline desktop regression execution passed before source changes; see `evidence/host-baseline.log`.

Build entry points are `xdk/build.py` and `xdk/scripts/build_ffmpeg.py`; the root Makefile is the separate XeLL target. SDK root is `C:\Program Files (x86)\Microsoft Xbox 360 SDK`; Jul12 compiler file version 16.10.16045.00, product version 10.10.16045.00. The resulting XEX imports identify SDK 2.0.21256.0. Release uses static runtime, NDEBUG and aggressive optimization (`/Ox /Ou /Oc` for hot modules, base `/O2 /Oi` where retained). Existing current worker map and legacy slice dispatch are preserved. Optional adaptive-fast, optimized-copy, legacy-upload, single-slice, no-RFI and stage-profiling experiments remain disabled in this candidate.

Vendored dependency pins documented by `xdk/README.md`: XBMC-360 `20658be9` (FFmpeg RELEASE 1.2.12, actual libavcodec version 54.92.100), moonlight-common-c `4a95eb35`, mbedTLS 2.16.12 and Opus 1.5.2. These dependency directories are files in the parent Git tree, not independent current checkouts: running `git rev-parse` inside them returns the parent revision. Local patches are substantial. The candidate source hash manifest and rebuilt libraries identify the actual inputs; an upstream pin alone is insufficient provenance.

## Architecture and feature disposition

| Feature / actual implementation | Disposition | Source/fixture evidence and limits |
|---|---|---|
| Native entry, writable storage, startup/fallback | KEEP | `xdk/src/main_xdk.cpp`, `src/app.c`; storage/display/launch fixtures. No title launch performed. |
| Shutdown and callback lifetime | KEEP | `video_stop` joins render worker; moonlight stop joins transport before cleanup. teardown/session-callback/lifecycle fixtures. A real stuck GPU wait remains hardware-dependent. |
| Discovery, saved hosts, mDNS | KEEP | `src/mdns.c`, `src/host_cache.c`, app discovery worker; malformed DNS, cancellation, host identity and caching fixtures. |
| PIN pairing, cancellation, HTTPS state | KEEP | `src/gamestream.c`, connection-task and host-protocol fixtures. No real TLS pairing exchange tested. |
| Sunshine/Apollo/Vibepollo capability/permission paths | KEEP | `src/host_compat.h`, XML parsers; host-compat/protocol/recovery fixtures. No live compatibility certification. |
| Unknown / Android-compatible GameStream hosts | EXPERIMENTAL | Generic protocol path retained. No live Android-compatible host tested; this is not an Android client runtime. |
| Reconnect, cancellation, stale/running host app | KEEP | `src/connection_flow.h`, app launch/stop confirmation, host-recovery/session-control/app-identity fixtures. |
| RTP reception and assembly | KEEP | MCC `VideoStream.c`, `VideoDepacketizer.c`, `RtpVideoQueue.c`; existing bounds, counters and frame-loss handling retained. Live UDP behavior pending. |
| FEC recovery and packet statistics | KEEP | MCC queue/nanors; existing diagnostics and aggregate counters. Host tests do not reproduce actual packet timing or radio loss. |
| H.264 decode/error propagation | KEEP | Bundled `h264.c`, `h264_refs.c`, worker error propagation and `video_submit`; existing corrupt-frame rejection and need-IDR gating retained. Prior incident root cause remains unproven. |
| Three-mode Fast Decode | FIX | New `src/fast_decode.h`; integer config/options/profile propagation, both decoder backends, settings/stream info/overlay. Real deblocking-decision fixture covers 18 cases. |
| Legacy INI/preset inference | FIX | Numeric 0/1 keep mode identity; 2 is Partial; invalid inputs fall back to Off. Untagged geometry cannot override Off/Partial. Explicit old factory selections retained. |
| Saved profiles/create/rename/delete | KEEP | Format 5 stores Partial; old versions 1–4 migrate with checksums; bounded two-slot publication and linked rename/delete rollback retained. User-profile/config fixtures. |
| Slice scheduling, worker count, frame threading | KEEP | FF_THREAD_SLICE and existing 3/4 selection retained; existing offline 3/4/5 diagnostic matrix remains available. No frame-thread latency/buffering added. |
| Cross-slice deblocking | KEEP | Raw IDC 0 with accurate filtering falls back to serial even with eight slices. No slice-header rewrite or forced parallel execution. |
| Full FAST cross-slice shortcut | EXPERIMENTAL | Actual bundled parser changes across-slice filtering to within-slice when FAST and multiple contexts are requested. Can drift on references. New installs do not default here. |
| Queue publication/latest-ready selection | KEEP | `video_cached.cpp`, existing bounded slots and allocation lifetime; cached-video/texture fixtures simulate failures, overwrite and ownership. |
| GPU uploads/fences/cache coherency | KEEP | Existing cached/legacy paths and conservative ownership preserved. Host fence fixtures are simulated; actual cache coherency and fence timing pending. |
| VSync/present pacing/frame counters | KEEP | `gfx_xdk.cpp`, render-pacing/video-report fixtures. Requested, RTP-complete, decoded and CPU Present metrics remain distinct. |
| First frame and later freeze watchdogs | KEEP | `src/video_watchdog.h`, app connection loop; bounded escalation and reconnect gates preserved. Fixtures cannot prove recovery from a real driver/decoder hang. |
| Audio stereo/5.1/fallback/queue | KEEP | `audio_xdk.cpp`, `src/audio_layout.h`; audio-remote/launch-audio fixtures. Existing engine glitch, queue-full and submit-failure telemetry retained. |
| Controller/calibration/hotplug/shortcuts | KEEP | `input_xdk.cpp`, `src/input_session.h`, `src/input_shortcut.h`; mapping and hotplug fixtures. Local polling is not end-to-end latency. |
| Mouse/keyboard/USB keyboard | KEEP | Existing input mode and async keyboard wrappers; profile-keyboard/control fixtures. Actual USB devices pending. |
| Quick Menu, return, disconnect/close | KEEP | `gfx_stream_menu.cpp`, app session loop; expansion/session-control/switch-app/launch-handoff fixtures. Layout unchanged. |
| Performance overlay/export | OPTIMIZE | Existing runtime gates and rolling windows retained. Added mode/reference facts and offline parser fields; no new per-frame clock or disk write. |
| Deadline/backlog handling | FIX | Selected reconstruction policy now stays fixed for the session. Partial still participates in backlog observation and bounded recovery; no mode-change IDR requests. |
| Smooth/Standard/Quality/Performance/1080p | KEEP | Existing explicit factory behavior preserved, including effective Full on speed-oriented factories. 1080p remains experimental. No performance guarantee. |
| Custom profiles/Stream Adjustment | KEEP | Actual config/menu snapshots retain Partial without Boolean truncation. Mode changes apply on next connection. |
| Periodic Picture Refresh | KEEP | Existing `LiRequestIdrFrame()` path, cooldown/request generation and delayed-IDR identity; refresh fixtures pass. Requesting IDR does not prove host intra-refresh behavior or artifact cause. |
| Refresh smoothing | EXPERIMENTAL | Existing opt-in GPU display-only history, raw-frame separation, motion/Guide/invalid-history bypass retained. Default Off. No new smoothing promoted. |
| Artwork/disk cache/loading/fallback | KEEP | `src/cover_disk.h`, `src/ui/cover_cache.h`, `src/cover_decode.h`, app artwork workers and cover fixtures; old artwork retained on failed refresh. |
| Native UI/fade/transitions/loading/history | KEEP | Existing renderer and navigation preserved; GPU UI, focus-motion, home/history and loading fixtures. No redesign. |
| English/Spanish/Portuguese | FIX | New mode/help entries are in sorted lookup order; dictionary and UI fixtures check translated lookup and bounds. |
| CRT/4:3/16:9/display profiles | KEEP | Existing independent UI/output geometry and CRT clarity; config/display/wireless-crt fixtures and shader compilation. Physical CRT/presentation pending. |
| Aurora Direct Launch/shortcut identity | KEEP | Existing validated launch-data schema, UUID-first host resolution and game identity unchanged; launch protocol fixture and separate launcher build. |
| Aurora detection/browser/saved paths | KEEP | `src/aurora_*.h`, `xdk/src/aurora_shortcuts_xdk.cpp`; existing bounds, metadata and path validation retained. Actual handoff pending. |
| Ethernet/Wi-Fi/network interruption | KEEP | Transport paths unchanged; host lifecycle and simulated failures covered. No network adapter or console was accessed. |
| Xenon VMX128/DSP/scalar fallback | KEEP | Existing `h264_vmx.c` dispatch/selftests/fallbacks remain. No unmeasured SIMD routine promoted. Host IDCT/loopfilter tests do not execute Xenon instructions. |

## Decoder correctness and performance decisions

Off clears FAST and uses DEFAULT for loop filter/frame/IDCT. Partial clears FAST and uses NONREF for loop filter, DEFAULT for frame/IDCT. Full uses FAST with DEFAULT loop filter/frame/IDCT. New contexts own all setup, and the existing running-session guard rejects changes to options. The settings help explicitly says next connection. No extra decoder instance, RTSP workaround or live mutation is introduced.

The pre-existing `video-h264.ini` diagnostic file remains supported. Disabling its FAST flag restricts active Full to Off before decoder setup; the active-session UI/export reflects that restriction. Requested/saved settings remain intact. The real options/callback fixture covers all three modes, both diagnostic flag values and attempts to change options while running.

`AVDISCARD_NONREF` depends on `nal_ref_idc==0`, not simply P/B picture type. Reference picture filtering remains required even when the picture is not currently on screen. The diagnostic counts parsed access units separately from reference slices and output pictures. Streams with no non-reference pictures may see no Partial speedup.

FFmpeg remaps raw disable_deblocking_filter_idc values: raw 0 requires across-slice filtering; raw 1 disables filtering by encoder choice; raw 2 filters within slices. Serial fallback for raw 0 is normative in the accurate path. Frame threading or delayed boundary filtering would require additional reference-ready barriers and careful neighbor ownership; no unsupported rewrite was made. Existing VMX code uses XDK intrinsics and runtime reference tests, not standard AltiVec assembly assumptions. Scalar fallbacks and selected VMX masks remain unchanged.

The implemented hot-path simplification removes repeated per-picture loop-filter policy assignment: setup now fixes it once. No measurable Xbox performance improvement is claimed. No new per-frame allocations or full-frame copies were added. Existing CPU/GPU, memory queueing, shader choice and input affinity experiments were not promoted without comparable hardware evidence.

Periodic refresh sends transport IDR requests; decoder drift, RTP loss, host encoding, intra-refresh and rendering flashes are separate hypotheses. Existing smoothing is display-only and optional. Repeated IDRs are not presented as a fix for decoder-induced reference corruption. The old observed corruption/stuttering incident has not been reproduced with this exact XEX.

## Cleanup and unresolved risks

Removed only: the binary UI toggle, Boolean mode truncations, the unsafe production `AVDISCARD_ALL` decision, deadline-driven changes to Off's filtering, misleading old filter labels/help and redundant per-frame policy assignment. No feature, diagnostic, callback, resource, launcher ABI or conditional entry point was deleted. No bulk dead-code removal was justified by sufficient evidence.

Normal telemetry remains opt-in. Detailed rolling traces require the overlay/profiler; export happens after teardown. CPU present timing is not GPU execution or display latency. The existing engine glitch counter is not a precise device-underrun timeline. Network/FEC fixtures and live radio behavior remain separate.

Accurate filtering can be slower than the old unsafe Fast mode, especially at 720p60 with cross-slice dependencies. Full remains risky; existing speed-oriented presets still select it and display that fact. New installations use Custom/Off, preserving the prior requested geometry, FPS and bitrate. Partial has no promised benefit. Physical image quality, stutters, memory coherency, audio timing, long sessions and real host compatibility all remain pending.

## Validation and build reproduction

Per-test desktop environment, result, errors and source components are recorded in `evidence/host-results.json`; suite results are in `evidence/pc-results.json`. Baseline and final logs are separate. `tools/validate_release_pc.py` runs local checks without console access. MSVC `/analyze` covers configuration/persistence only; real XDK compilation checks native sources. There is no whole-program Xenon static analyzer or GPU emulator here.

```powershell
$env:XDK_BUILD_DIR = "$PWD\build\release-candidate\toolchain"
$env:XDK_CONFIGURATION = 'Release'
$env:XDK_OPTIMIZATION = 'aggressive'
python xdk/scripts/build_ffmpeg.py
python xdk/build.py mbedtls opus moonlight compat app xex shortcut_xex
python tools/validate_release_pc.py
python tools/prepare_stabilization_rc.py
```

Clear any experimental `XDK_*` overrides before reproducing. Exact compile flags, source digests, imported modules and warning logs accompany the candidate. Main/shortcut linker and image conversion output, including return codes, are now retained even on successful builds. A repeated link with identical object/library inputs preserved `.text` and the other measured sections except `.rdata`, where two bytes differed. Full PE/XEX byte reproducibility is not claimed; see `evidence/relink-reproducibility.json`. Runtime resource extraction and source/library provenance are also verified. The prior distributable remains unchanged.
