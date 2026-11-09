# FPS cadence investigation — 2026-11-02

## Status and conclusion

**Root cause remains unconfirmed.** The 2026-11-01 GTA: San Andreas physical
baseline requested 60 FPS and measured 31.886 complete RTP/FEC frames/s,
31.886 decoder callbacks/s, 31.886 decoded frames/s, and 31.886 Present
calls/s over 45.036 s. The first *measured* reduction is between the request
and complete frames at the Xbox. That interval includes the game, Apollo
display/capture/encoder, network transport, and RTP frame assembly; the old
capture cannot isolate one of them.

A separate, previously recorded 2026-11-02 GTA session offers an important
constraint: its first 45.045 s measured 28.416 complete frames/s, 28.327
decoder callbacks/decoded frames/s, and 28.216 Present calls/s. Its 267.735 s
session report shows 49.50 presented FPS overall, and its final log has four
approximately two-second samples of **59.9–60.1 received, decoded, and shown
FPS** with no drops in those samples. These are different windows, not an
average or a repeat of the 2026-11-01 baseline. They show that the same Xbox
pipeline was capable of receiving and showing about 60 FPS later in that
session; a fixed 30 FPS client cap is not supported. They do not identify the
cause of the slower opening window or prove that the host rendered 60 distinct
images; encoded frames can repeat an application image.

The matching Apollo log for the 2026-11-02 GTA session records a
`1280x720x60` client mode request, 12,000 kbps, a **2560x1440, 180.001 Hz
host display**, and H.264 NVENC creation with a 16,666,666 ns encoding-frame
threshold. A requested rate and threshold are not measured capture or encoder
output rates. The Xbox report has no wall-clock timestamp, so correlation to
the Apollo session is supported by the same host IP and approximately matching
268 s session duration, not a shared session ID.

## Client FPS request path

The source audit is in
[`evidencias/fps-cadence-20261102-154355/fps-source-path.txt`](../evidencias/fps-cadence-20261102-154355/fps-source-path.txt).
The table separates source-derived values from values observed on a physical
session. The new diagnostic XEX has **not** run on the Xbox.

| Stage | Current source behavior for Smooth | Physical evidence |
| --- | --- | --- |
| Stream Profile | `config_apply_preset()` sets Smooth to 1280x720, **60** FPS, 12,000 kbps. Saved settings and per-game profiles can override it. | Both separate GTA captures report Smooth and requested **60**. |
| Video options | `vo.target_fps = cfg.fps`, hence **60** for this effective config. This assignment occurs after the GameStream launch call, although it uses the same `cfg`. | New per-session stage file NOT TESTED on Xbox. |
| `STREAM_CONFIGURATION` | `lj.sc.fps = cfg.fps`, hence **60**; `clientRefreshRateX100` comes separately from Xbox display refresh. | New per-session stage file NOT TESTED on Xbox. |
| GameStream `/launch` or `/resume` | `mode=1280x720x60`. The only special case sets zero for an NVIDIA host when requested FPS is **greater than 60**; it does not apply to Apollo at 60. The URL contains keys and is not logged. | Apollo logged **1280x720x60** for the 2026-11-02 GTA session. Earlier Steam trace also logged a 60 FPS request, for a different session. |
| Moonlight configuration copy | `memcpy()` copies `lj.sc` to `StreamConfig`; no FPS division is present. | New per-session stage file NOT TESTED on Xbox. |
| RTSP ANNOUNCE/SDP | `x-nv-video[0].maxFPS` serializes `StreamConfig.fps`, hence **60** if the effective config is Smooth. `clientRefreshRateX100` is a separate client-display attribute for protocol major version 7 or later. | SDP content was not captured from a new Xbox session. These are client requests, not host-confirmed FPS. |
| Apollo | Receives the requested mode; it may pace capture/encode independently. | 2026-11-02 GTA Apollo log: **60 requested**, 180.001 Hz host display, H.264 NVENC. Actual source/capture/encoder FPS unavailable. |
| Xbox complete frames | Last FEC block of a frame completes before depacketizer submission. | **31.886 FPS** in the 2026-11-01 GTA baseline; **28.416 FPS** in the separate 2026-11-02 opening window. |
| Decoder / presentation | Video setup receives the requested `StreamConfig.fps`; it is not a host negotiation callback. | **31.886 / 31.886 FPS** decoded/Present in the original baseline. About **60/60 FPS** in the final short samples of the separate 2026-11-02 session. |

Source search found no `fps / 2`, `refresh / 2`, `60000/1001`,
`30000/1001`, `59.94`, or `29.97` conversion in the inspected client path
(`half-rate-search.txt` is empty). `30` appears in other places, including
the Standard preset and fallback timing calculations; neither changes a
Smooth 60 request. `plat_display_refresh_x100()` rounds the **Xbox display
refresh** to hundredths of a hertz; it does not halve `cfg.fps`.

**Negotiated/host-confirmed FPS: unavailable.** Moonlight's video setup FPS
argument copies `StreamConfig.fps`; Apollo's requested frame-rate log and
SDP `maxFPS` are requests, not confirmation of output cadence.

## Apollo and display evidence

Apollo Service is installed and running on this PC. The current local host IP
is `192.168.15.4`; the historical 2026-11-01 setup used `192.168.15.10`.
The filtered Apollo log, source-log hash, Windows display snapshot, and copied
Xbox reports are in the [raw evidence directory](../evidencias/fps-cadence-20261102-154355/).
No Apollo setting was changed.

| Source/session | Apollo requested mode | Host display reported by Apollo | Source FPS | Actual capture FPS | Actual encoder FPS | Xbox result |
| --- | --- | --- | --- | --- | --- | --- |
| 2026-11-01 GTA baseline | 60 in Xbox report; GTA-specific Apollo log unavailable | NOT MEASURED for that GTA capture | NOT MEASURED | NOT MEASURED | NOT MEASURED | 31.886 complete/decoded/Present FPS in 45.036 s |
| 2026-11-02 Desktop log at 14:19 | 1280x720x60 | 2560x1440 at 180.001 Hz | NOT MEASURED | NOT MEASURED | NOT MEASURED | No source-specific Xbox capture retained here |
| 2026-11-02 GTA log at 14:20 | 1280x720x60, 12,000 kbps | 2560x1440 at 180.001 Hz | NOT MEASURED | NOT MEASURED | NOT MEASURED | Separate opening window: 28.416 complete, 28.327 decoded, 28.216 Present FPS; later short samples about 60 FPS |
| Earlier Steam trace | 1280x720x60 | NOT MEASURED in that trace | NOT MEASURED | NOT MEASURED | NOT MEASURED | Do not combine with GTA |

The earlier Steam trace's **59.94 Hz** was the *client/Xbox display refresh*
sent with the request, not a measurement of the Apollo host display. It must
not be compared as a host monitor mode against the later 180.001 Hz Apollo
log.

The Apollo startup enumeration named `DISPLAY1` as an ARZOPA-27 display at
180.001 Hz. Windows reports the NVIDIA output at 2560x1440/180 Hz. Virtual
display adapters are installed but had no active mode in the read-only Windows
snapshot. The log's `Virtual Desktop` size is a DXGI desktop extent, not proof
that a virtual display driver was selected. Whether a virtual display is
selected during a future controlled stream must be checked from the active
Apollo display identity and actual mode for that session. The current Apollo
log exposes requested rate, display refresh, and encoder selection/threshold;
it contains no measured source FPS, capture FPS, or encoded-frame FPS for
these sessions.

### GTA source frame-limit configuration

The local GTA installation has competing limiter settings. Open Limit
Adjuster's `[SALIMITS]` section sets `FrameLimit = 30`; MixSets and Framerate
Vigilante each set `FPSlimit = 60`, and Framerate Vigilante has
`AutoLimitFPS = 1` for certain activities. The 2026-11-02 modloader log records
the corresponding ASI modules loading, while the MixSets runtime log says
`Open Limit Adjuster = true` and `System: FPSlimit = 60`. Those log files were
last written around 14:25, near the end of the likely matching GTA stream.
The [configuration excerpts](../evidencias/fps-cadence-20261102-154355/gta-fps-settings-excerpts.txt),
[configuration hashes](../evidencias/fps-cadence-20261102-154355/gta-fps-config-hashes.json),
and [runtime excerpts](../evidencias/fps-cadence-20261102-154355/gta-mod-runtime-excerpts.txt)
are preserved separately. They establish that multiple limits were configured
or loaded, but do **not** establish which limiter governed a particular scene
or the game's actual frame rate. No GTA setting was changed. Capture a host
process presentation counter in the same GTA scene before classifying branch A.

## New bounded client trace

The isolated diagnostic build is
[`fps-cadence-diagnostic.xex`](../evidencias/fps-cadence-20261102-154355/fps-cadence-diagnostic.xex),
SHA-256 `2d7753595b5da805134a33c661dec15d4486a5ef2c97d8c554371a0f9828208a`.
Host fixtures and the XDK build passed; `imagexex /dump` found XEX2 imports,
title ID and SPA. **Console deployment/launch: NOT TESTED.** The known-good
production XEX is unchanged. The 4 ms controller polling constant is unchanged,
and the isolated 2 ms candidate was not used.

The read-only XBDM title probe found the console running the existing
`MoonX360-rebuilt-20261101.xex`. Starting the four controlled scenes requires
Xbox/Aurora or controller navigation, which is unavailable during this run.
No new physical capture was started, and no console recovery or reboot was
attempted. All four source comparisons below remain **NOT TESTED** with the
new diagnostic build.

With Diagnostic capture enabled, `fps-request-path.txt` records the numeric
FPS at six client stages once per attempt, including an observation count for
stereo retries. A zero `client_refresh_x100` on the SDP stage means the
version-gated attribute was absent. It explicitly marks host-confirmed FPS
unavailable. The values are recorded in fixed storage and written only after
the session ends. No launch URL, input key, certificate, or SDP payload is
persisted.

`frame-cadence.txt` stores at most the **last 128 completed RTP frame IDs**
inside the diagnostic capture, with monotonic completion timestamps,
inter-completion intervals and skipped IDs. The hook is at the last FEC block
for each frame, before depacketizer submission. It uses fixed memory, one
clock read per completed frame, and cold file output after transport stop; it
does not log payloads or allocate per frame. Capture stops at about 45 s even
if the stream continues. The tail rate is not the whole-window FPS.
`xdk/scripts/collect_latency_xbdm.py` now collects both files.
`xdk/scripts/analyze_frame_cadence.py` summarizes the tail's p50/p95/p99
intervals, 13–20 ms versus 28–38 ms bands, ID gaps and nonincreasing IDs.
Synthetic 16.667 ms and 33.333 ms/gapped-ID traces passed the analyzer; they
are **not Xbox measurements**.

If completion intervals cluster near 33.33 ms with contiguous IDs, the Xbox
has received a ~30 FPS numbered stream, but source/capture/encoder output is
still needed to locate where it fell. If IDs skip regularly, inspect the
unrecoverable/loss counters and host encoded-frame IDs before attributing the
gap to Moonlight. A host may itself skip IDs; frame-ID gaps alone do not prove
client packet loss.

## Controlled source comparison procedure

Use one Apollo host identity and network route throughout. For **each source**,
select Smooth and record effective 1280x720, requested 60 FPS, 12 Mbps,
1024-byte packets, VSync ON, H.264 format, same capture setting, and about
45 s. Disable automatic adaptive profile changes for comparability or record
every resulting reconnect as a separate session. Do not average across apps.
Save a fresh XBDM title probe, Apollo log excerpt with wall-clock timestamps,
`fps-request-path.txt`, `frame-cadence.txt`, `latency-capture.txt`,
`performance-session.txt`, decoder report, and collection hashes in a unique
directory per run. Verify the active XEX hash/path and the host identity.

| Source | Controlled motion and source FPS check |
| --- | --- |
| Desktop | Continuous visible movement for the full window; record desktop output identity, actual refresh, and a host-side presentation/capture FPS counter if available. |
| Steam Big Picture | Continuous animation or repeatable scrolling; record Steam's own FPS/presentation counter if available. |
| GTA: San Andreas | Same save, scene, camera movement and duration on every repeat; capture an in-game/host process FPS counter if available. Separate opening/menus from active gameplay. |
| Known ~60 FPS reference app/game | First verify actual host process presentation near 60 FPS in that exact scene, then stream the same scene; record its counter and method. |

For every row, record **host application FPS**, **host display refresh and
physical/virtual output**, **requested FPS**, **host-confirmed FPS only if a
real negotiated field exists**, **actual capture FPS**, **actual encoder output
FPS**, **Xbox complete-frame FPS**, **decoder FPS**, and **Present-call FPS**.
Mark fields `NOT MEASURED` when Apollo or the app lacks a counter; neither a
60 FPS request nor a 16.667 ms encoder threshold substitutes for output FPS.
Use a host-side frame presentation tool if available; PresentMon was not
installed in this read-only check. Do not change Apollo configuration merely
to produce a counter. Keep all measurement windows aligned by timestamp and
app/session identity. Repeat per source if practical.

The first physical follow-up is one GTA run with the diagnostic XEX and a
host-side process FPS counter in the exact slow scene. Collect the 45 s Xbox
trace and Apollo log for the same wall-clock window. If that source runs near
60 while Xbox completion remains near 30, obtain independent Apollo capture
and encoder cadence before testing a client-side frame-assembly hypothesis.

## Decision against A–E

| Branch | Current assessment |
| --- | --- |
| A — game/source ~30 | **Unknown.** GTA application FPS was not measured in the slow window. Installed 30 and 60 FPS limiter settings conflict, and the runtime log does not identify their effective precedence or scene-specific behavior. Later 60 FPS delivery can contain duplicate game images. |
| B — source/display ~60, capture ~30 | **Unknown.** The display ran at 180.001 Hz in the separate Apollo GTA session; display refresh does not establish source presentation or capture FPS. |
| C — capture ~60, encoder output ~30 | **Unknown.** Apollo logs the H.264 encoder and threshold, not actual frame output. |
| D — host encodes ~60, Xbox completes ~30 | **Unknown.** No host encoded-frame count or fresh Xbox frame-ID trace is available. The later 60 FPS Xbox samples argue against a fixed client half-rate path in the separate session. |
| E — request becomes ~30 before Apollo | **Not supported for the separate 2026-11-02 GTA session.** Apollo logged a 60 FPS mode request. Current code has no 60→30 conversion along the inspected request path. The original 2026-11-01 GTA capture lacks a matching GTA-specific Apollo request log. |

**First known boundary:** after the 60 FPS request and before Xbox complete
frame arrival in the original physical baseline. For the separate 2026-11-02
GTA session, Apollo also logged 60 requested; its slow opening window lies
somewhere after that request and before complete-frame arrival. The missing
measurements are source presentation, capture, encoded output, and client
frame-ID cadence in the same time window. No decoder, renderer, VSync, input,
queue, worker, slice, GPU filter, or audio setting was changed.
