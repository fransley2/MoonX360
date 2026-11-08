# MoonX360 latency baseline (v0.2.2)

This phase measures the existing 720p60 Smooth path. It does not change the 4 ms input cadence, Moonlight input queue limit, RTP/FEC behavior, decoder policy, audio buffering, or VSync. Factory Smooth is 1280x720 at requested 60 FPS, 12,000 kbps, 1024-byte packet size, automatic slices, and VSync on. Saved settings and named profiles can override these; the capture records the effective requested values.

## What existed

`src/performance.h` and `xdk/src/performance_xdk.cpp` already supplied a 128-sample fixed ring, event marks, QPC clock, and cold session reports. Input poll interval/work, audio callback/decode and overflow, launch/setup, and UI timings were present. `src/decode_deadline.h` tracked decode pressure and queue maxima. `xdk/src/video_xdk.cpp` already tracked callback, decoded and rendered counts, decode/queue timing, fence *predicate check* cost, texture copy/CPU submit, and host processing via the Moonlight decode unit. The normal overlay and Technical diagnostics already displayed a smaller subset.

## Added probes and interpretation

| Stage | Source | Interpretation |
| --- | --- | --- |
| Input | XInput, `input_session`, Moonlight `InputStream` | Changed Gamepad sample to enqueue, queue holder age/depth at send dequeue, enqueue to network handoff, send call duration. `controller_local_latency` ends at InputSend handoff. Coalesced and queue-full events are counted. |
| Network | Moonlight `VideoStream`, `RtpVideoQueue` | UDP packets/bytes, accepted RTP data/FEC packets, expected data/FEC counts from Moonlight's FEC block headers, sampled receive syscall/decrypt/RTP CPU time, observed sequence gaps, late/OOS/invalid packets, FEC recovery/failure, completed FEC frames and loss notifications. Receive/decrypt/RTP CPU timings are sampled on approximately one of 64 receive calls. |
| Decode | Moonlight `VideoDepacketizer`, XDK renderer | Decode-unit queue depth and age at dequeue, H.264 avcodec call and subsequent CPU handling, host-reported processing (100 microsecond protocol units). |
| Display | Cached/legacy video renderer | Y/U/V plane transfer, total transfer, cached free texture slots at publish, legacy texture upload, texture-slot starvation, CPU submit, ready-to-Present, Present call interval/duration, Present start/return timestamps, and locally assigned presented frame serial/duplicate count. CPU submit is not GPU execution. |
| Audio | XAudio2/Opus | Opus decode, queued buffer count, nominal queued milliseconds, queue-full drops and engine glitches. Queued milliseconds assume negotiated nominal samples per buffer. |

All metric samples collect in fixed rings. Reports include current, EMA, p50/p95/p99 of the latest 128 samples and maximum/total across the recording interval. Percentiles are calculated after workers stop; they are not whole-session percentiles. Counter totals in `latency-capture.txt` refer to its capture window. `performance-session.txt` includes setup-through-capture-close samples, so its timing percentiles can include a short period before the 45-second window. Depth and audio milliseconds are the exceptions to the microsecond timing units. Queue depth is observed at dequeue, so an instantaneous peak between dequeues can be missed. There are no packet logs or hot-loop allocations. Clock reads on controller changes and frame boundaries, plus network sampling, add measurable CPU cost that should be assessed on hardware.

For coalesced controller packets, holder queue age starts at the original enqueue. Controller local latency uses the latest detected state time and ends when InputSend calls the send routine. Send work is reported separately.

## FPS pipeline


Requested FPS is the effective local configuration. Negotiated FPS remains unavailable because the Moonlight callback contract does not expose a host-confirmed rate. Completed RTP/FEC frames, decoder callback calls, successfully decoded frames and Present calls are counted independently. The bounded report divides each stage's count within the capture window by that window's duration. The last-session page shows the same stage separation. A 60 to 30 transition should be located by the first stage whose measured rate halves. Decode queue age in milliseconds can be divided by 16.667 to express 60 FPS frame budgets (for example 34 ms is about 2.0 budgets).

## Capture on the console

In Developer > Technical diagnostics > Diagnostic logging, enable **Diagnostic capture**, select the existing Smooth profile, and start a stream. Collection closes after about 45 seconds or at stream stop. After disconnect, open **Developer > Technical diagnostics > Latency diagnostics** for the last-session view. `latency-capture.txt`, `performance-session.txt`, `present-trace.txt` (last 64 local Present calls), and the existing video/session reports are written to the application's diagnostic storage after the session, without pairing keys or credentials. The detailed page is a stopped-session view because the video presenter exclusively owns the display during streaming. The normal overlay keeps its existing refresh cadence.

## Measurement limits

* No input-to-photon or host input receipt timestamp is available. `controller_local_latency` is local only.
* Host capture and encode times cannot be split from the protocol's reported host processing field. A zero or missing field is unavailable.
* A 10 ms `RTP_QUEUE_DELAY` macro exists in this Moonlight checkout but has no active reference. The measured wait starts at an observed missing sequence and ends at FEC block completion; it is not a configured fixed-delay measurement. FEC failed data deficit and frame loss notifications are distinct from exact network packet loss.
* XDK GPU execution timestamps and exact fence-completion age are unavailable here. Fence *check CPU duration* and texture-slot starvation are proxies with their own labels. Present call CPU duration includes any VSync wait, but does not independently isolate it. Present serials are local, not host sequence numbers.
* Audio buffer milliseconds are estimated; exact speaker playback time and underrun count are not available. XAudio2 engine glitch count and queue-full drops remain separate indicators.
* A real Xbox 360 and LAN are required to establish latency, FPS, VSync pattern, and instrumentation overhead. Host fixtures and XEX packaging do not establish those results.
