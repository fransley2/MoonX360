# H.264 reference-safe parallel diagnostic — 2026-11-06

## Evidence and scope

The physical observations are qualitative: A and D visibly corrupt; B and C stay clean but are unplayably slow at 640×480/30, 10 Mbps, with periodic refresh off. There is no fresh B/C capture or measured decoder/presented FPS. The reachable XBDM console (`192.168.15.10:730`) still denies the five requested session log files with `414 access denied`; the read-only snapshot under `evidencias/h264-reference-parallel-20261106-before/` is preserved. Its older capture metadata does not identify the B/C test stream.

The bundled `h264.c` maps raw `disable_deblocking_filter_idc` 0 to internal filter mode 1 (including cross-slice boundaries), raw 1 to mode 0 (off), and raw 2 to mode 2 (within-slice). With multiple contexts, mode 1 plus `CODEC_FLAG2_FAST` clear forces `max_contexts=1`. With FAST set, it changes mode 1 to mode 2, omitting cross-slice filtering. This explains a plausible correctness/performance tradeoff, but does not prove the hardware stream had raw IDC 0 or multiple slices. CABAC/CAVLC and actual worker participation are likewise unmeasured.

## E build strategy

`h264-ref-E-compatible-parallel-20261106` retains C's reference-safe policy: FAST clear, normative reference filtering, and optional nonreference filtering reduction only under the existing C setting. It does not rewrite a slice header or override the decoder's serial safety fallback. A stream encoded with raw IDC 2 can use the existing slice worker pool and preserve the encoder's within-slice filtering; raw IDC 0 continues to decode serially when required. This means E is a diagnostic build, not a demonstrated speedup on the current host.

The isolated Release artifact is `xdk/build/h264-ref-E-compatible-parallel-20261106/default.xex`, 6,336,512 bytes, SHA-256 `638C34841ACFE01F3ECBEAA2AF77AF4F037B36BFB526BFF2BDDD7D6ACD37BC85`. Its post-build `imagexex /dump` reports Title ID `4D583336`, `xam.xex`, and `xboxkrnl.exe`. All host regression fixtures passed. A/B/C/D artifacts remain in their existing separate build directories. E has not been launched on the Xbox.

The new `h264-parallel-session.txt` and last-64-frame `h264-parallel-trace.txt` are written at stream stop when Stats Overlay is on. They include raw IDC counts, effective filter counts, slices, CABAC slices, reference slices, requested/effective contexts, serial/parallel dispatches, distinct worker participants, peak simultaneous workers, decode average and rolling p95/p99, and completed/decoded/presented FPS. A configured thread count alone is not parallelism evidence. `idr` in the trace reflects the depacketizer's frame type. Collect reports immediately after each session because a subsequent session can overwrite them.

The offline benchmark also writes `decoder-frame-hashes.txt` with per-frame visible Y/U/V hashes from its C full-filter baseline. This only establishes a comparison target when replaying the exact `capture.bin` access units. The current capture mechanism is bounded to 600 AUs and 40 MiB and may not cover a two-minute drift; use the live two-minute camera test for that.

## Host bitstream experiment

x264's slice-header code selects raw IDC 2 for sliced threads when its deblocking condition is true; it can select raw IDC 1 when that condition is false. Independently sliced reconstruction must follow whichever filter the encoder actually used. FFmpeg exposes libx264 slice threading through `-thread_type slice` and `-x264-params`. First generate an **offline** candidate with the desired geometry/profile and inspect its actual headers, for example:

```powershell
ffmpeg -i source.mp4 -c:v libx264 -pix_fmt yuv420p -preset superfast -tune zerolatency -thread_type slice -threads 4 -x264-params 'sliced-threads=1:slices=4' -f h264 candidate.h264
ffmpeg -i candidate.h264 -c:v copy -bsf:v trace_headers -f null NUL
```

Verify actual SPS/PPS, slice count, raw IDC, deblocking offsets, CABAC/CAVLC, reference count, and IDR cadence. Sunshine documents software encoding options but a live x264 parameter path and Xbox-compatible latency must be checked on the actual host. Hardware encoder support for raw IDC 2 must be established from emitted headers; setting an unrelated FFmpeg option is insufficient. Keep the host configuration unchanged for the initial E versus C Xbox comparison. Any host experiment must be separately labeled and restored afterward. Never patch an already encoded slice header.

## Identical-bitstream correctness and workload

Preserve and hash a fresh `MLC1` capture from the relevant physical stream. Export its unchanged AUs with `python xdk/scripts/mlc1_to_annexb.py capture.bin capture.h264`. Use a PC reference decoder to emit raw `yuv420p` and inspect headers, for example `ffmpeg -f h264 -i capture.h264 -pix_fmt yuv420p -f rawvideo reference.yuv`. Replay the same original `capture.bin` through E's offline decoder benchmark; collect `decoder-benchmark.txt` and `decoder-frame-hashes.txt`. Compare with `python xdk/scripts/compare_benchmark_yuv.py decoder-frame-hashes.txt reference.yuv`. Match dimensions, frame count, order, and capture fingerprint first. A mismatch requires raw plane extraction at the first divergent frame and subsequent P frames; hashes alone cannot quantify pixel differences. A second unrelated live session is not an identical-bitstream comparison.

The benchmark already compares C full-filter, full VMX loop filter, and the available selectively enabled VMX groups on one capture, with native mean/p95/p99/max and internal output hashes. The older 1080p replay favored some VMX candidates on average but had worse tail latency for full VMX in one configuration. That prior result uses a different bitstream and does not justify an F build. Build F only after the new reference-safe capture demonstrates equal Y/U/V output and a usable tail-latency win on the console. Analyze CABAC, actual slices, queue age, and worker overlap before attributing all slowdown to filtering. Parallel reconstruction plus delayed cross-slice boundary filtering for raw IDC 0 would require substantial changes to this legacy decoder's neighbor state, synchronization, reference readiness, and worker ownership; no unsafe shortcut is included.

## Physical protocol

Record exact XEX SHA-256 and the host encoder identity. With Stats Overlay on and periodic refresh off, run C then E on unchanged host settings at 640×480/30/10 Mbps, 640×480/60/10 Mbps, and 720p/30/12 Mbps. Hold a static scene for at least two minutes, then exercise motion. Record visible artifacts/time, actual IDRs, displayed and decoded FPS, decode average/p95/p99, callback time, queue age trend, RTP loss/FEC, input/audio, GPU Present/fences, and clean teardown. Stop and collect the new reports plus existing decoder/performance/present/stall logs after each run. Do not infer scanout correctness solely from successful decode or Present calls. Only after unchanged-host results, test a verified compatible bitstream separately. A real run and a matching decoded-plane comparison are required before calling E correct or faster.

No F XEX exists yet. No release has been published.
