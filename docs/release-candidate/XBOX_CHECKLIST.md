# Deferred Xbox acceptance checklist

Status for every case: **PENDING PHYSICAL XBOX 360 VALIDATION**. Do not infer Xbox performance from host fixtures or compiler estimates.

Before testing, verify `default.xex` against `manifest.json`, retain the working binary and record console/display mode, host name/version, encoder/GPU, link type, game/scene and candidate hash. Confirm the exact candidate is launched. Disable recording, periodic refresh and refresh smoothing initially. Fix image-quality shader/VSync/thread options across comparisons.

Back up and temporarily set aside any old `video-h264.ini` diagnostic override before comparable runs. A retained `fast_flag=0` restricts Full to accurate Off for the active session; stream information and decoder diagnostics show Off. Capture/workers/RFI overrides can also invalidate comparisons. Do not delete credentials or the `moonlight` folder.

For each row, test Off, Partial and Full separately on the same repeatable scene and host encoder settings. Use Custom so a factory preset cannot select a different policy. Verify the displayed active mode. Warm up 30 seconds, then measure at least 120 seconds; repeat runs in reversed mode order. Run an additional 10-minute artifact check and a 30-minute endurance session for the intended release profile.

| Resolution | FPS | Bitrate | Modes |
|---|---:|---:|---|
| 640×480 | 30 | 6 Mbps | Off, Partial, Full |
| 640×480 | 60 | 9 Mbps | Off, Partial, Full |
| 1280×720 | 30 | 6 Mbps | Off, Partial, Full |
| 1280×720 | 60 | 12 Mbps | Off, Partial, Full |
| 1920×1080 | 24 | 8 Mbps | Off, Partial, Full; experimental resolution |

For every run:

- [ ] Static detail and fine text remain readable; no new seams, blocks, colour errors or progressive drift.
- [ ] Fast camera motion and low-bitrate scenes: record stutters separately from compression artifacts and tearing.
- [ ] Record requested, RTP-completed, decoded and CPU-presented FPS; decode p50/p95/p99, queue age, drops, FEC/recovery and reference/non-reference counts. Record unavailable fields as unavailable.
- [ ] Audio remains synchronized; check crackles/underruns and controller responsiveness subjectively. Local input timing is not end-to-end latency.
- [ ] Quick Menu, return to game, overlay, mouse, keyboard/USB keyboard and button shortcuts work; no simultaneous menu/video ownership.
- [ ] Stop stream, reopen the same app, switch apps with termination confirmation and stale host state; cancel connection and reconnect. Repeat ten start/stop cycles.
- [ ] Interrupt/recover the network; cancel recovery. No permanent black screen, unbounded retry or shutdown hang.
- [ ] Repeat with periodic refresh at 2 seconds, then 5 seconds; record IDR receipt and visible flashing separately. Optional smoothing remains experimental: test only after the unsmoothed control, check ghosting, motion and extra latency. Never treat refresh as a corruption fix.
- [ ] Check 4:3/16:9, CRT display adjustments, VSync and all three languages. Check artwork cache/fallback and UI transitions after stopping.
- [ ] Verify saved profile create/save/rename/delete and persistence after app restart. Back up format 5 before rollback.
- [ ] Test Aurora Direct Launch, existing shortcuts, launcher creation, XEX browser, saved paths, game identity, artwork fallback and return handoff.

Host/link coverage is a separate matrix: Sunshine, Apollo, Vibepollo and any Android-compatible host actually available; wired and Wi-Fi. Record the exact versions and result per case. Untested combinations stay pending.

After each stopped run, copy `decoder-session.txt`, `h264-parallel-session.txt`, `network-session.txt`, `performance-session.txt`, `refresh-frame-trace.txt`, `present-trace.txt` and any audio/input reports that exist from the writable `moonlight` folder. Archives rotate through five sessions; copy promptly into a folder named for resolution/FPS/bitrate/mode/repetition. Reports without matching session provenance must not be combined.

Offline summary on the development PC (reads local files only):

```powershell
python xdk/tests/collect_ab_results.py <local-results-folder> <summary.json>
```

Rolling percentiles cover the last 256 samples, not the full session. CPU Present completions do not prove scanout or visible unique frames. Parser reference counts describe parsed access units with slices, not successfully displayed pictures. Packet deficit and recovery counters are not exact packet-loss percentages. Use manual observations alongside telemetry.

Acceptance requires no confirmed critical regression, no progressive corruption in the selected release policy and acceptable cadence/response confirmed by the developer. Partial is not promoted to default without comparable measurements. Publication additionally requires explicit developer approval; a successful physical run alone does not authorize a GitHub release.
