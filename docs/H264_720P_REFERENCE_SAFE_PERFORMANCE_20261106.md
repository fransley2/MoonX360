# MoonX360 720p reference-safe performance — 2026-11-06

## Current physical evidence

Read-only XBDM on `192.168.15.10:730` succeeded; Aurora FTP on port 21 answered but rejected anonymous login (`530 Need username`). The running title during collection was Aurora. XBDM directory listings located the E installation at `HDD:\Apps\MoonX360\E - XDK Release build\default.xex` and its own `moonlight` directory. The remote XEX is 6,336,512 bytes; a bounded read-only XBDM download closed during transfer, so its SHA-256 was **not** verified. The local E XEX remains SHA-256 `638C34841ACFE01F3ECBEAA2AF77AF4F037B36BFB526BFF2BDDD7D6ACD37BC85`.

Raw reports and collection metadata are preserved under `evidencias/h264-720p-reference-safe-E-20261106T215311Z/`. The collected reports are `latency-capture.txt`, `fps-request-path.txt`, `frame-cadence.txt`, `performance-session.txt`, `present-trace.txt`, `decoder-session.txt`, `last-session.txt`, `last-log.txt`, `h264-parallel-session.txt`, `h264-parallel-trace.txt`, `diagnostic-stage.txt`, `shutdown-stage.txt`, `connection-timing.txt`, `gpu-cached-selftest.txt`, `vmx-selftest.txt`, and `performance-menu.txt`. `last-video-stall.txt`, `capture-info.txt`, `decoder-benchmark.txt`, and `decoder-frame-hashes.txt` were **absent from the E directory listing**; this is distinct from a `414 access denied` response. All listed requested reports were fetched. No credential or pairing file was read.

The E report says 640×480/30 requested, 10 Mbps requested, 142.214 seconds, 4,040 decoded and 4,039 presented calls, with no periodic refresh requests and two IDRs received/decoded. It does not contain a camera record, so the user's clean-image observation remains the visual evidence. The exact installed E hash and active host encoder for that historic session are not independently verified.

| E metric | Native report | Scope |
| --- | ---: | --- |
| Raw IDC 0 / 1 / 2 | 32,320 / 0 / 0 slices | Full recorded session |
| Slices | 8 per frame | 4,040 frames |
| CABAC / CAVLC slices | 0 / 32,320 | Full recorded session |
| Requested / effective contexts | 4 / 1 in tail trace | Serial fallback on 4,039 frames |
| Serial / parallel dispatches | 32,320 / 0 | Zero participating slice workers |
| Decode mean / rolling p95 / rolling p99 | 14.443 / 20.375 / 20.629 ms | Mean full session; percentiles last 256 |
| Decoded / Present-call FPS | 28.466 / 28.459 | Full session; Present is not visible scanout |
| Decode queue age p99 / max | 11 µs / 9.317 ms | First bounded 45-second diagnostic window |
| YUV transfer p99 / CPU GPU submit p99 | 0.988 / 0.055 ms | First bounded 45-second window |
| Present-call interval p99 | 40.156 ms | First bounded window; last 64 trace p99 41.541 ms |

The first 45-second performance capture auto-closes, explaining its 1,136 RTP complete frames versus the full-session 4,040 decode callbacks. It must not be treated as a full-session count. Queue age, YUV transfer, and CPU GPU submission are small compared with decode in this run. Input-to-photon latency was not measured, so E's less-responsive feel cannot be attributed to decoder time alone. The present interval tail also warrants comparison against E at 720p.

The `h264-parallel-*` and `decoder-session.txt` reports require Stats Overlay. `performance-session.txt` requires diagnostic capture; the bounded capture closes after 45 seconds. Reports are written during stream cleanup and shutdown, so collecting while streaming or from the main installation can produce absent or stale data. The E folder contains the requested files and `diagnostic-stage.txt` says clean local return. No relevant write failure appeared in `last-log.txt`; this does not prove every future write succeeds.

## Root performance constraint

The bundled `h264.c` maps raw IDC 0 to normative across-slice filtering. With FAST clear and more than one configured slice context, its slice-header path changes `max_contexts` to one. The E capture confirms this branch's effect: 8 slices, 4 configured contexts, 1 effective context, and no parallel dispatch. This is a measured constraint at 640×480; 720p decode time and p99 remain unmeasured. The 30 FPS 720p target is p99 below 25 ms as an engineering goal, not a historical result.

The active Windows service is Apollo and its installed configuration contains `nvenc_h264_cavlc = enabled` and `nvenc_preset = 7`. The latest accessible Apollo log is from 2026-11-05 and records `h264_nvenc`, not this E session. [Apollo's NVENC source](https://github.com/ClassicOldSong/Apollo/blob/master/src/nvenc/nvenc_base.cpp) requests `sliceMode=3` with `slicesPerFrame` and sets CAVLC when configured. It does not establish that NVENC can emit IDC 2 for this host. The Xbox parsed eight CAVLC/IDC-0 slices. Host configuration was only read, not changed.

The decoder's `loop_filter()` consumes neighboring macroblock state and the row finish path can report reference progress. Parallel reconstruction followed by cross-slice deblocking would need explicit boundary ownership, filter order, and progress barriers before reference pictures become visible. No shortcut was applied. VMX loop-filter primitives passed the collected self-test, but no current-capture tail-latency comparison exists, so no VMX optimization is promoted.

## Isolated G profiling build

`xdk/build/h264-ref-G-stage-profile-20261106/default.xex` keeps E's reference mode 2, FAST clear, RFI enabled, current worker map, legacy slice dispatcher, C loop filter, and aggressive XDK Release profile. FFmpeg alone is rebuilt with `XDK_H264_STAGE_PROFILE=1`. It uses QPC probes for whole slice work and loop-filter calls, plus one in 32 entropy macroblock calls. `h264-stage-session.txt` holds aggregate QPC-based timings. `h264-parallel-trace.txt` adds per-frame QPC tick totals so p95/p99 can be computed after collection. Probe cost is included; sampled entropy timing is an estimate, and parallel slice totals would be aggregate worker CPU time. Do not use G's absolute FPS as proof of an uninstrumented speedup.

G XEX SHA-256: `B89682DD40EBA4AAD8F48644E5971D3C064590A3E69EC859BAED489882F138B1`. The post-build `imagexex /dump` reports Title ID `4D583336`, XEX imports, and the embedded dashboard resource. All host regression fixtures passed. E's XEX hash was rechecked and is unchanged. The native G runs below are at 480p60 and 720p60; there is still no collected 720p30 measurement or identical-setting E/G A/B.

### First G console run, collected after user feedback

The user observed that G was clean but unplayable at 720p and that 480p30 looked good. The first reports retrievable from the actual G folder, `HDD:\Apps\MoonX360\G - default\moonlight`, describe a **later 640×480/60 session lasting 31.788 seconds**. They do not measure the reported 720p run. Raw files and hashes are preserved under `evidencias/G-480p60-reference-safe-20261106T223520Z/`. All requested reports including `h264-stage-session.txt` were available through read-only XBDM; FTP credentials were not needed.

That 480p60 session used eight raw-IDC-0 CAVLC slices per frame. It made 10,272 serial and zero parallel dispatches, with 1,283 frames forced to one context. Decode averaged 15.507 ms with rolling p95 20.374 ms and p99 20.722 ms; decoded and Present-call rates were both 40.368 FPS versus 60 requested. Decode queue age reached 265 ms and p99 of the diagnostic window was 261 ms; `last-log.txt` records several decode-unit queue overflows and IDR requests, while periodic refresh requests remained zero. No macroblock corruption was reported by the user. The stream had 1,284 decoded and presented calls, zero logged decode errors, and a clean return; visual correctness still relies on the user's observation.

G's aggregate QPC stage probe measured 19.547 s in slice work and 6.735 s in loop filtering over the session (34.5% of measured slice CPU time). In the final 64-frame trace, loop filtering averaged 5.413 ms and reached 7.737 ms at p99; nonfilter slice work averaged 10.520 ms and reached 12.908 ms at p99. Sampled entropy decoding averaged 3.576 µs per probed macroblock over the session, including probe cost; this is not an exact full entropy subtotal. The stage measurements establish loop filtering as a substantial hotspot at 480p60, but they do not predict its 720p cost or prove that VMX improves p99. The profiler itself adds timing overhead.

The console folder name is `G - default`, not the proposed `G - stage profile`. Collect from that exact name. The reports in this folder are overwritten by the next session, so a fresh 720p run must be collected before another 480p test.

### G 720p60 run collected immediately after teardown

The subsequent `G - default` reports were collected after the running title returned to Aurora. Raw files and hashes are preserved under `evidencias/G-720p60-reference-safe-20261106T223933Z/`. The actual session was **1280×720/60 requested**, 12 Mbps requested, Smooth profile, 135.926 seconds. It is not a 720p30 run. The user reported a clean image but unplayable responsiveness.

The stream had 8 IDC-0 CAVLC slices per frame, 25,240 serial dispatches, zero parallel dispatches, and 3,154 frames forced to one context. Decode averaged 38.158 ms, rolling p95 46.370 ms and p99 47.260 ms; 3,155 frames were decoded and 3,154 Present calls made, about 23.23 FPS. Queue age peaked at 482.810 ms. YUV transfer p99 was 2.501 ms in the bounded diagnostic window; CPU GPU submission p99 was 42 µs. Slice work totaled 118.961 s, of which loop filtering used 47.791 s (40.2%). The final 64 frames had loop-filter mean 17.978 ms and p99 30.824 ms; remaining slice work mean 23.657 ms and p99 28.171 ms. This is the first native 720p bottleneck split, but G's probes add some overhead.

`decoder-session.txt` reports 207 IDRs received and zero periodic refresh requests. The bounded log also records decode-unit queue overflows and recovery IDR requests. Thus this clean-looking 135-second session does **not** prove two minutes of correct references without an IDR: recovery requests repeatedly refreshed the stream. The requested 60 FPS has a 16.67 ms frame period, far below the measured decode mean; queue growth and poor pacing are expected from these measured values. The Xbox reported zero decode errors and no audio glitch observations in the bounded window, but this is not an input-to-photon or visual correctness test.

## Five-session log retention variant H

The user requested at least five retained diagnostic logs after the 720p report was overwritten by a later 480p session. A separate H build copies ten bounded, named report files into five numbered slots **after** a clean stream teardown, when both Stats Overlay and Diagnostic Capture are enabled. Each slot has a manifest marked `writing` until its files finish copying, then `complete`; the next-slot pointer advances only after completion. These files live in that XEX's own `game:\moonlight` directory. No pairing files, certificates, credentials, capture video, settings, or existing E/G XEX are copied or modified. The collector fetches the numbered archives and manifests by an allowlist. An actual XDK build and a host fixture covering six rotations passed; physical H write behavior remains untested.

H does not optimize decoding. It retains G's stage probes and E's reference-safe decoder policy, allowing multiple profiling sessions to be compared without immediate collection. The isolated artifact is `xdk/build/h264-ref-H-five-session-logs-20261106/default.xex`, SHA-256 `64CD3891465E1C58C79DC3C55820EB8404D1D58E0B3576CD03F7964E2DE431F6`. `imagexex /dump` confirms Title ID `4D583336` and expected imports. E and G XEX hashes remain unchanged. The full host regression suite passed, including the six-session archive rotation fixture; physical H archive behavior remains untested.

## Next exact hardware procedure

1. Preserve E. G is currently installed in `HDD:\Apps\MoonX360\G - default\default.xex`. Keep its `moonlight` directory separate. Record the copied XEX hash using a local transfer route where possible; an XBDM file size alone does not prove identity.
2. With Stats Overlay and diagnostic capture enabled, run E at **1280×720/30** using a profile that actually requests 30 FPS, 12 Mbps, H.264, periodic refresh OFF for at least two minutes with static and moving scenes. Record image/video, input response, audio, first artifact time, and exact settings. Return cleanly to Aurora. Collect reports using `python xdk/scripts/collect_xbox_variant_reports.py 192.168.15.10 "E - XDK Release build" evidencias/E-720p30-<unique-timestamp>`. Verify `last-session.txt` actually says `video=1280x720@30` before any subsequent run.
3. Repeat the same scene/settings with G, collecting from `"G - default"` into a different new directory. Check parsed IDC, slice count, context count, decoder/presented FPS, rolling decode and present p99, queue-age trend, and `h264-stage-session.txt`. Run `python xdk/scripts/analyze_h264_stage_trace.py <G-evidence>/h264-parallel-trace.txt <G-evidence>/h264-stage-session.txt` for last-64-frame stage p95/p99. Use G only to decide whether filtering, entropy, or remaining reconstruction work warrants the next isolated optimization. To retain five sessions, install H in a **new** folder and test its archive manifests; do not replace E or G.
4. Arm the bounded H.264 capture on one matching run. Preserve and hash `capture.bin`. Replay **those same AUs** through the existing native benchmark's C full-filter and available VMX groups; compare Y/U/V hashes with a PC reference decoder before choosing a filter candidate. A capture from another live session is not identical material.
5. If G identifies a credible low-risk gain, build an uninstrumented candidate and compare it with unchanged E on the same 720p30 material. Require two minutes clean without periodic refresh and no growing queue age. Only then attempt 720p60/12 Mbps, whose 16.67 ms period makes p99 much tighter.

Keep host experiments separate from decoder measurements. On the confirmed Apollo installation, preserve the original `sunshine.conf`, then compare one setting at a time and restore it after each test: current CAVLC versus CABAC (`nvenc_h264_cavlc`), current requested eight slices versus a lower requested slice count, and current P7 versus a conservative faster NVENC preset. Record the actual encoder in the host log and verify SPS/PPS, emitted slice count, raw IDC, reference count, and bitrate for every run. Apollo's slice-count request does not imply IDC 2 or active client parallelism. A custom host build is outside this baseline; do not assume hardware encoding can emit a reference-safe IDC-2 stream until its output proves it.

No host configuration was modified, no F-style VMX build was promoted, and no release was published.
