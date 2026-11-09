# GTA host/Xbox FPS correlation — 2026-11-02

## Current status after the interactive run

**GTA runtime presentation FPS is now measured; a matched 45-second
host/Xbox cadence comparison is still unavailable.** The host counter recorded
7,215 GTA D3D9 presentations over a 140.414 s span while the Xbox was in a
GTA stream: **51.376 FPS overall**, with full 5-second buckets ranging from
**39.4 to 60.0 FPS**. This confirms that GTA itself varied below 60 FPS in
that scene. The Xbox's bounded 45-second capture from the same session was
taken about nine minutes earlier; its rolling per-interval log was replaced
when the next stream launched. A later GTA 45-second Xbox capture began after
the host recording ended. Therefore the available files do **not** show
whether Xbox completion followed GTA's 40–60 FPS changes at matching times.
The cause of the particular ~30–45 FPS Xbox windows remains unconfirmed.
See the final section for the exact windows and raw evidence.

## Result

**Same-window host-to-Xbox FPS correlation: NOT TESTED.** A live GTA: San
Andreas stream occurred while this investigation began, and its Xbox reports
were collected read-only after disconnect. The available NVIDIA FrameViewSDK
PresentMon executable failed to start a capture (probe exit code 1, empty
stdout/stderr, no CSV). The Xbox was running the earlier
`MoonX360-rebuilt-20261101.xex`, not the separately built FPS-cadence
diagnostic XEX. Therefore there is no measured GTA process FPS or completed
frame-ID/interval trace for this session. The source FPS and first stage of
the rate reduction remain **unknown**.

**Latest follow-up at 18:09 local:** a direct graphics ETW probe returned
Access denied, and PresentMon still produced no samples. A separate,
preexisting GTA run with the diagnostic title yielded a valid 128-frame tail
trace and 42.893 complete FPS over 45.042 s, but no GTA process FPS. Details
and separately preserved evidence are below; the synchronized correlation
test remains **NOT TESTED**.

No MoonX360 code, polling setting, Apollo configuration, GTA limiter, bitrate,
packet size, or VSync setting was changed for this task. No XEX was deployed by
this investigation,
and no controller, Aurora, reboot, or console recovery action was attempted.
The conditional second GTA limiter test was not started because the untouched
runtime source FPS was not established.

## Session and evidence

Raw files are in
[`evidencias/gta-host-xbox-fps-20261102-175628`](../evidencias/gta-host-xbox-fps-20261102-175628/).
The XBDM collector saved hashes for each available Xbox report in
`xbox-readonly-post-session/collection.json`; its missing-file results are
preserved there too. The filtered Apollo lines are in
`apollo-session-1755-1757.txt`, with the filter and excerpt hash in
`apollo-excerpt-provenance.json`. GTA limiter excerpts and source-file hashes
are separate. PresentMon attempt metadata is in `presentmon-attempt.json`.

Apollo logged a `1280x720x60` client mode at **17:55:47.027 local**, started
the GTA process (PID 10560) at **17:55:48.344**, and reported client connected
at **17:55:48.766**. It reported the active host display at **180.001 Hz**,
requested **60 FPS**, and 12,000 kbps. Client disconnected at **17:57:20.162**;
the host application exited by 17:57:21.797. The 16,666,666 ns encoder
threshold in the log is a setting, not encoder-output FPS. Apollo exposed no
measured GTA presentation, capture, or encoded-frame cadence in this excerpt.

The Xbox `last-session.txt` records **91.164 s**, 4,250 received and decoded,
4,249 shown, one dropped, no decode errors. The Apollo connected interval is
about **91.396 s**. Xbox connection timing names the same host IP
`192.168.15.4`, and the game/session durations closely match. This supports
the files referring to the same stream, but Xbox diagnostic files lack a
shared wall-clock session ID or capture start/end timestamps. The exact
45.044 s Xbox window cannot be independently aligned to a host FPS series.

The Xbox 45.044 s `latency-capture.txt` reports Smooth, 1280x720, requested
60 FPS, 12 Mbps, 1024-byte packets, VSync ON, **45.023 complete RTP/FEC
frames/s**, **45.023 decoder callbacks/s**, **45.023 decoded frames/s**, and
**45.000 Present calls/s**. It reports seven FEC packets recovered, zero FEC
data deficits, and zero frame-loss notifications. The full session's one
dropped frame cannot be placed inside or outside the 45 s window from these
reports. Short Xbox log
samples vary from **23.8 to 59.4 FPS** in the visible tail; those samples
measure Xbox delivery/decoding/presentation, not GTA source presentation.

The diagnostic build from the prior investigation remains available and its
SHA-256 was rechecked as
`2d7753595b5da805134a33c661dec15d4486a5ef2c97d8c554371a0f9828208a`.
The known-good local XEX remains
`0e62c46c05b59bdb39cf9075c30edbc074a9f559168d7e502e2029ffcdaeeb44`.
XBDM's `modules` response identifies the running older XEX by name. Since the
new diagnostic XEX was not active, `fps-request-path.txt` and
`frame-cadence.txt` were unavailable (`414- access denied` from XBDM's
`getfile`). These responses indicate absent/inaccessible reports, not an
observed frame-ID pattern.

## Measurements

| Test | GTA runtime FPS | Host display Hz | Requested FPS | Xbox complete FPS | Decoded FPS | Presented FPS | Frame interval pattern |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| Existing limiter state, GTA stream 17:55–17:57; Xbox 45.044 s window | **NOT MEASURED** | 180.001 | 60 | 45.023 | 45.023 | 45.000 | **NOT MEASURED**; older XEX has no frame-ID trace |
| Verified 60 FPS GTA, conditional second test | **NOT TESTED** | NOT TESTED | 60 planned | NOT TESTED | NOT TESTED | NOT TESTED | NOT TESTED |

The previously recorded **31.886 FPS** GTA baseline is a different session
and is not combined with this row. The new Xbox rate of about 45 FPS is not
evidence that GTA itself rendered 45 FPS. A 180.001 Hz host display does not
establish game FPS, and Apollo's requested 60 FPS is not a negotiated or
measured output rate.

## Host counter and limiter state

The installed `PresentMon_x64.exe` is NVIDIA FrameViewSDK version
1.8.12407.0. It supports process filtering, wall-clock timestamps, a timed
capture, and CSV output. The initial capture attempt at **17:56:28 local**
returned without a CSV. A separate 5-second probe exited with code **1**
and empty stdout/stderr. The executable can display its help, but no actual
GTA presentation samples were collected; the cause of capture failure was not
reported by the tool. No average, minimum, 1% low, or timestamped source FPS
series can be stated. The host counter must be made operational before the
next physical comparison.

The GTA modloader and MixSets runtime logs were last written around
**17:57:22**, alongside the Apollo session's end. Their saved excerpts show
MixSets, Framerate Vigilante, and Open Limit Adjuster ASI modules loaded.
Open Limit Adjuster's `[SALIMITS]` config sets `FrameLimit = 30`; MixSets and
Framerate Vigilante each configure 60 FPS, with Framerate Vigilante automatic
limiting enabled. MixSets logs `Open Limit Adjuster = true` and
`System: FPSlimit = 60`. These are configured/loaded states, **not** a measured
runtime FPS or proof of which limiter won in the sampled GTA scene. The
scene itself was not observable through these automated reports.

## Classification and next test

None of cases A–D can be confirmed. GTA source FPS is missing, Apollo capture
and encoder output rates are missing, and the bounded Xbox frame-ID trace was
not active. Xbox complete, decoded, and Present rates followed each other in
the collected 45 s window, with no evidence of a decoder-side half-rate drop.
That observation does not locate the earlier ~30–32 FPS reduction.

For the next physical test, first verify a host process-presentation counter
produces a timestamped CSV for `gta_sa.exe`. Use the isolated diagnostic XEX
through the existing safe test path and verify its active title. In the same
slow GTA scene, start the host counter and Xbox Diagnostic capture within a
few seconds of each other for about 45 s. Record both start/end wall-clock
times and the matching Apollo session lines; collect
`fps-request-path.txt`, `frame-cadence.txt`, `latency-capture.txt`,
`performance-session.txt`, `decoder-session.txt`, and `last-session.txt`.
Analyze completed-frame IDs and intervals near 16.67 or 33.33 ms, and compare
the same window's GTA source FPS with Xbox complete FPS. Do not infer a
packet-loss mechanism from skipped IDs without matching loss/FEC evidence.

Only if the untouched GTA runtime is independently measured near 30–32 FPS
should the second test temporarily resolve the limiter conflict. Preserve
hashes and backups of each original GTA configuration, verify the new runtime
source FPS is near 60, repeat the same synchronized 45 s capture, then restore
and hash-check the originals. Until that first measurement exists, no GTA
limiter change is warranted. MoonX360 decoder or network optimization is not
warranted by the current evidence.

## Measurement workflow follow-up — 2026-11-02 18:09 local

**The synchronized physical correlation test was not started.** Host process
FPS capture remains unavailable in this Windows session. The new raw evidence
is in
[`evidencias/gta-host-xbox-fps-workflow-20261102-180942`](../evidencias/gta-host-xbox-fps-workflow-20261102-180942/).
This directory is separate from the earlier 17:55 GTA evidence.

### PresentMon and ETW preflight

The installed NVIDIA FrameViewSDK `PresentMon_x64.exe` (file version
1.8.12407.0) accepts `--process_name`, `--process_id`, `--date_time`,
`--timed`, and `--output_file` according to its own help. An `explorer.exe`/
`dwm.exe` process-name check showed DWM running. A five-second capture
filtered to `dwm.exe` exited with code **1** and no CSV. A second five-second
capture with **no process filter** also exited with code **1** after about
one second, with no CSV or stdout/stderr. Thus a GTA process-name typo or
GTA's presentation API cannot explain these pre-GTA failures. The same Temp
output directory accepted a direct write, ruling out a simple output-path
permission failure. No active PresentMon/FrameView/RTSS/Afterburner process or
similarly named ETW session was found in the read-only checks.

A direct, uniquely named `Microsoft-Windows-DxgKrnl` ETW start probe returned
**0x80070005, Access denied**, with the instruction to run as administrator.
The current process has a medium-integrity token and lacks the relevant ETW
profiling privilege. This is a confirmed blocker to starting a graphics ETW
capture here. PresentMon itself supplied no failure message, so no additional
PresentMon-specific cause is claimed. Since no capture started even without a
filter, GTA's D3D presentation visibility remains **NOT TESTED**. Installed
program and process checks found no other local tool that already exports a
trustworthy, timestamped GTA presentation counter. GPU utilization, the
180.001 Hz monitor refresh, Apollo's 60 FPS request/encoder threshold, and
GTA limiter settings are not substitutes.

Following the required gate, GTA was **not launched locally for this test**,
the diagnostic XEX was **not deployed by this run**, and no Xbox stream was
started. `--restart_as_admin` would require an interactive Windows elevation
step; that step was unavailable. The diagnostic XEX's local SHA-256 still
matches `2d7753595b5da805134a33c661dec15d4486a5ef2c97d8c554371a0f9828208a`;
the known-good local XEX still matches
`0e62c46c05b59bdb39cf9075c30edbc074a9f559168d7e502e2029ffcdaeeb44`.
Source polling remains `SEND_INTERVAL_MS 4`. No Apollo, GTA limiter, or
MoonX360 performance setting was changed.

### Separate, preexisting diagnostic-title session

The XBDM `modules` read-only probe at about 18:11 listed
`fps-cadence-diagnostic.xex` as the running title. That identifies the title
by module name, **not** its on-console file hash. It was already running when
checked; this investigation did not deploy or launch it. A read-only XBDM
collection retrieved the new diagnostic reports, which belong to an earlier
GTA stream. Apollo logged the GTA launch at **18:01:12.459**, client connected
at **18:01:12.928**, and disconnected at **18:02:24.512**. It logged
1280x720x60 requested, 12 Mbps, and a 180.001 Hz host display. The Xbox
`last-session.txt` reports 71.354 s, consistent with that Apollo interval,
but no shared wall-clock session ID is available.

The diagnostic `fps-request-path.txt` has **one observation of 60 FPS at each
of six client stages**: Stream Profile, `STREAM_CONFIGURATION`, GameStream
launch mode, video options, Moonlight config copy, and SDP `maxFPS` generation.
Its `client_refresh_x100=5994` is the Xbox client display refresh, not host
FPS. The 45.042 s Xbox capture reports **42.893 complete**, **42.893 decoded**,
and **42.871 Present FPS**, with zero frame-loss notifications and zero FEC
data deficits. The 71.354 s session reports 3,261 received/decoded/presented
and zero drops or decode errors. Neither report measures GTA source FPS.

The bounded **tail** contains 128 consecutive completed frame IDs
**1805–1932**, with zero internal ID gaps. Its 127 inter-completion intervals
have p50 **16.606 ms**, p95 **17.665 ms**, p99 **18.625 ms**; **126** intervals
fall in 13–20 ms, **zero** in 28–38 ms, and **one** outside both bands. The
tail-span rate is **60.097 completed IDs/s**. This demonstrates near-60
delivery at the end of that separate window, not a 60 FPS average over its
whole 45 s capture. The bounded tail does not give 5-second buckets for the
earlier slow portion. There is still no same-window GTA presentation series,
so cases A–D remain unclassified and the first rate-reduction stage unknown.

| Test/session | GTA runtime FPS | Host display Hz | Requested FPS | Xbox complete FPS | Decoded FPS | Presented FPS | Frame-ID interval pattern |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| Preexisting diagnostic-title GTA, 18:01–18:02; 45.042 s Xbox window | **NOT MEASURED** | 180.001 | 60 at six client stages | 42.893 | 42.893 | 42.871 | Last 128 IDs: 126/127 near 16.67 ms; no ID gaps |
| Required synchronized host/Xbox test | **NOT TESTED** | NOT TESTED | 60 planned | NOT TESTED | NOT TESTED | NOT TESTED | NOT TESTED |

The host-counter prerequisite above was satisfied during the interactive
follow-up below. The earlier NOT TESTED labels remain historical evidence
states for those sessions; they do not apply to the later GTA source capture.

## Interactive host measurement and Xbox window separation — 18:19–18:36

The [official portable PresentMon 2.6.0 release](https://github.com/GameTechDev/PresentMon/releases/tag/v2.6.0)
was downloaded into
[`evidencias/gta-host-xbox-fps-live-20261102-181530`](../evidencias/gta-host-xbox-fps-live-20261102-181530/).
Its SHA-256 matched the release asset digest
`b2a706bc6ad475749e3b7e3409263aa1e6906d45bdcf993f6dbc0f660188f1af`,
and Windows reported a valid signature. Elevated capture first produced
132 DWM CSV lines and **884 `gta_sa.exe` D3D9 rows** in a 15-second local
preflight. The GTA span was **14.931 s / 59.140 FPS**, median frame interval
**16.679 ms**, p95 **19.178 ms**, p99 **27.732 ms**. The executable is a
presentation counter; monitor refresh and Apollo's target are not used to
calculate those numbers.

During a GTA stream, a full-metric trace failed with no CSV and a report of
917,641 lost ETW events. An additional simultaneous unfiltered probe reported
220,046 lost events. A stale ETW session named `PresentMon` then blocked a
short probe. The successful recovery used PresentMon's
`--stop_existing_session` with `--no_track_gpu`, `--no_track_input`, and
`--no_track_display`, retaining process Present timestamps and frame
intervals. That produced **711 GTA D3D9 rows / 59.556 FPS** over 11.922 s
while Apollo was streaming. Failure logs, successful raw CSV, and exact
commands are in
[`evidencias/gta-host-xbox-fps-verified-20261102-182717`](../evidencias/gta-host-xbox-fps-verified-20261102-182717/).

The final host CSV is in
[`evidencias/gta-host-xbox-fps-matched-20261102-183149`](../evidencias/gta-host-xbox-fps-matched-20261102-183149/),
with `gta-presentmon-analysis.json` containing every 5-second bucket.
PresentMon's raw `TimeInDateTime` on this PC was three hours behind the
Windows local clock at capture start. The analysis applies an empirically
checked **+6-hour raw-to-UTC offset** (equivalent to +3 hours from raw to
local São Paulo time); this offset was verified against the preflight start
and the Apollo game-exit time. It is a clock calibration for this host, not
an assumption that the CSV strings were already UTC.

| Measurement | Value |
| --- | ---: |
| GTA process / presentation API | PID 29668 / D3D9 |
| Host presentation span | 18:32:10.542–18:34:30.956 local; 140.414 s |
| GTA Presents and span rate | 7,215 / **51.376 FPS** |
| Frame interval p50 / p95 / p99 | **17.727 / 27.275 / 32.038 ms** |
| Intervals below 13 / 13–20 / 20–28 / 28–38 / above 38 ms | **41 / 4,528 / 2,375 / 258 / 12** |
| Full 5-second bucket range | **39.4–60.0 GTA Presents/s** |

Representative **GTA-only** full 5-second buckets relative to the host
capture start at 18:32:07.819 local:

| Offset | GTA Presents/s | Xbox complete FPS at that offset |
| --- | ---: | --- |
| 5–10 s | 59.4 | NOT RETAINED |
| 25–30 s | 60.0 | NOT RETAINED |
| 45–50 s | 40.0 | NOT RETAINED |
| 75–80 s | 41.6 | NOT RETAINED |
| 95–100 s | 39.4 | NOT RETAINED |
| 125–130 s | 60.0 | NOT RETAINED |
| 135–140 s | 60.0 | NOT RETAINED |

The host source therefore **genuinely changes cadence** during streaming.
The first confirmed below-60 stage in those host measurements is GTA's own
presentation. The data do not establish which installed 30/60 limiter, game
scene behavior, or host condition caused it. No GTA limiter was changed;
hashes of the relevant configurations remain unchanged in the evidence.

### Xbox diagnostic windows and why they do not match

Apollo connected the long GTA session at **18:23:20.517 local** and reported
the process terminating around **18:34:32.398**. Its Xbox `last-session.txt`
records **671.752 s**, 37,086 received/decoded, 37,062 rendered, 24 drops,
and **55.17 shown FPS over the whole session**. These whole-session totals
cannot be compared as if they were the host's late 140.414 s window. The Xbox
45.003 s diagnostic capture was at the **opening** of this long session:
**45.641 complete, 45.575 decoded, 45.575 Present FPS**. Its last 128
completed IDs were consecutive, with a **16.549 ms** median interval,
**18.023 ms** p95, **21.873 ms** p99, **122/127** intervals in 13–20 ms,
zero in 28–38 ms, and a **59.396 FPS** tail span. Host PresentMon was not
recording during those opening 45 seconds.

The host's late 18:32–18:34 sample did overlap the same long Xbox *session*,
but the Xbox's rolling short-FPS log for that late period was replaced when a
new stream launched. The collected `last-log.txt` contains startup lines for
the next session, not a time series for the late host window. The bounded
frame trace was frozen after the opening 45 seconds. Consequently no honest
5-second GTA/Xbox bucket pairing exists for the host window.

Apollo connected a **second** GTA session at **18:34:40.609 local**, after
the host recording ended at **18:34:37.833**. Its separately collected Xbox
45.000 s capture reports **47.067 complete, 47.044 decoded, 46.933 Present
FPS**. Its last 128 completed IDs were consecutive: median **16.598 ms**,
p95 **17.619 ms**, p99 **18.982 ms**, **126/127** intervals in 13–20 ms,
none in 28–38 ms, and a **60.176 FPS** tail span. No host GTA presentation
samples overlap that second session, so it cannot be paired with the host
51.376 FPS measurement. Apollo logged a 1280x720x60 request, 12 Mbps, and
180.001 Hz host display for these GTA sessions. XBDM listed
`fps-cadence-diagnostic.xex` as the active module; the local diagnostic XEX
hash still matched the expected SHA-256, while the module name alone does
not establish the on-console file hash.

| GTA stream/window | GTA runtime FPS | Xbox complete FPS | Decoded FPS | Presented FPS | Complete-ID tail |
| --- | ---: | ---: | ---: | ---: | --- |
| Long session opening 45.003 s | NOT MEASURED | 45.641 | 45.575 | 45.575 | 128 contiguous; 59.396 FPS tail |
| Same long session, late host 140.414 s | **51.376**, varying 39.4–60.0 in full 5-second buckets | NOT RETAINED for matching window | NOT RETAINED | NOT RETAINED | NOT RETAINED |
| Second GTA session opening 45.000 s | NOT MEASURED | 47.067 | 47.044 | 46.933 | 128 contiguous; 60.176 FPS tail |

**Classification:** GTA source-side cadence variation is confirmed, but
whether Xbox completion tracked its exact changes remains **NOT TESTED**.
Neither a fixed client 30 FPS cap nor a decoder-side half-rate drop is
supported by these reports. Apollo capture and encoded-output FPS remain
unmeasured. The first reduction stage for the specific ~45 FPS Xbox windows
remains unknown. No MoonX360 code change or Phase 3 optimization is warranted
on this evidence, and no GTA limiter or Apollo setting was changed.
