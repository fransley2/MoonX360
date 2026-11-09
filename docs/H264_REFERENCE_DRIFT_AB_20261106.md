# H.264 reference drift A/B — 2026-11-06

## Physical evidence collected

Read-only XBDM at `192.168.15.10:730` reported `getexecstate = start` and a `default.xex` title module. `dirlist` confirmed `HDD:\Apps\MoonX360\moonlight`, matching the app's `game:\moonlight` storage root. Raw collection results and hashes are in `evidencias/h264-reference-drift-20261106-physical-logs/` and `evidencias/h264-reference-drift-20261106-recovery-files/`; the latter includes the unmodified remote directory listing. No pairing file or credential was fetched or changed.

`decoder-session.txt`, `performance-session.txt`, `present-trace.txt`, `last-session.txt`, and `last-log.txt` each returned XBDM `414- access denied` and were absent from that directory listing. The same happened for latency and cadence reports. Stage, connection timing, last video stall, cached GPU self-test, VMX self-test, and capture metadata were collected with their SHA-256 hashes in `collection.json`, but the collector cannot prove they belong to the reported 5-second-IDR reproduction. The last stall summary says `DECODER_STALLED` in a different session; it is not evidence of this corruption's cause.

The existing `capture-info.txt` identifies a 1280x720/60, 12 Mbps, eight-requested-slice recording (`capture.bin`, 10,030,240 bytes). Its `encoded_slices` is `NOT_ANALYZED`; it is not a 640x480/30 comparison bitstream. A bounded read-only `getfile` attempt for `capture.bin` ended when XBDM closed during transfer, so no identical access-unit replay or decoded-plane comparison was performed.

## Confirmed source behavior and hypothesis

In `video_xdk.cpp`, current `open_h264()` enables `CODEC_FLAG2_FAST` even when the user's Fast Decode option is off, unless `video-h264.ini` overrides it. When Fast Decode is on, `open_h264()` and `video_submit()` set `skip_loop_filter = AVDISCARD_ALL` for every frame. With Fast Decode off, deadline recovery can select `AVDISCARD_NONREF`, which preserves reference-frame filtering.

In the bundled `h264.c` slice-header path, `AVDISCARD_ALL` sets `h->deblocking_filter = 0`. Separately, when deblocking type 1 and multiple slice contexts are present, `CODEC_FLAG2_FAST` changes the filter to type 2, skipping across-slice filtering. With FAST clear, that branch falls back to one decode context for correct filtering. These are confirmed code paths. Whether the actual host bitstream enables type 1 deblocking, how many slices it encodes, and whether these paths explain the photographed corruption remain unmeasured. Missing normative filtering on reference pictures is a plausible source of prediction drift that an IDR would reset.

## Isolated Release builds

All four use the same verified aggressive-profile FFmpeg libraries and the same current working-tree code. They are diagnostic artifacts under `xdk/build/`, not a release or a console-tested fix. The regular 5-second refresh remains available and is not forced by these builds.

| Build | Policy | SHA-256 of `default.xex` |
| --- | --- | --- |
| A current | Current FAST flag, `AVDISCARD_ALL` if Fast Decode is on | `ADFE0BB4067C3D7C2EB5234E1B9C87E36A572BCF17F2C7E634D3F92051D19D66` |
| B reference safe | FAST flag clear; `AVDISCARD_DEFAULT` always; single-context fallback when the decoder requires it | `1510F40FDD8E1467DE21384711D3C7C00CBEABE955BE77CF5C38AFEC9BCDB60C` |
| C selective | FAST flag clear; `AVDISCARD_NONREF` only when Fast Decode or deadline recovery asks for reduced work; reference pictures retain normative filtering | `4AF93FAB43F26BC30B1D91ACC4253F57D5EE3355721105FDBB3A72281BB54D9D` |
| D current without RFI | A's decoder policy; do not advertise AVC reference-frame invalidation | `F8D77871AFE1E5718082615E829BFDF2DD1C340EFFF80A0465B8BCECCED63667` |

Each XEX is 6,336,512 bytes. `imagexex /dump` succeeded on all four; each has an XEX2 header, Title ID `4D583336`, and `xam.xex`/`xboxkrnl.exe` imports. Each build directory contains its own dump and SPA verification from `xdk/build.py`. No diagnostic XEX was installed or launched.

Build switches: `XDK_H264_REFERENCE_MODE=0|1|2` and `XDK_H264_NO_RFI=0|1`. Default values `0,0` preserve A behavior. The session-only `video-h264.ini` switches still work; use identical settings for A/B/C and record their values. `decoder-session.txt` now reports effective FAST/RFI, reference mode, actual IDRs received/decoded, and periodic refresh request count when the stats overlay is enabled.

RFI is conditional on both client and server support. `ControlStream.c` sends invalidation requests; `VideoDepacketizer.c` waits for the appropriate recovery frame. The FFmpeg decoder consumes the resulting H.264 stream rather than a separate invalidation API. Disabling the advertised capability also makes `SdpGenerator.c` request a maximum of one reference frame instead of the codec maximum, so A versus D is a *capability/encoder-configuration* A/B, not a pure control-message-only comparison. There is no evidence yet that RFI was invoked during the reported static-scene corruption.

## Required hardware run

Use the same host, unchanged host settings, 640x480/30, 10 Mbps H.264, same scene, and same Fast Decode and slice settings. Record the exact installed XEX hash and a camera view with elapsed time. Enable the stats overlay and diagnostic capture. For each run, stop the session and immediately collect reports into a new evidence directory before another run overwrites them. Compare A, B, and C first with periodic refresh disabled; allow at least several observed artifact cycles. Then repeat A with the existing 5-second refresh as a positive recovery control. Compare D against A separately. Never interpret FPS alone as correctness.

For each run record first visible artifact time, visual severity, actual IDR counts and intervals, decoded/presented FPS, decode average/p95/p99, callback timing, queue age, RTP loss/FEC, GPU submission and Present CPU time, pipeline, and actual active worker count. A camera recording is needed because Present completion does not prove visible scanout. If B is clean and A drifts, repeat with the same bitstream and check C before choosing a default. If decoded YUV matches a reference but the display corrupts, compare cached versus Legacy upload with the same decoder policy and inspect fences, slot ownership and pitches before renderer edits.

For decoded-plane proof, preserve one bounded `MLC1` capture of the actual 640x480 reproduction and copy it locally from the console using a method that can transfer its full size. Replay those exact access units through a PC reference H.264 decoder and each Xbox build. Record SPS/PPS, encoded slice count, deblocking IDC/offsets, reference counts and IDR positions. Compare per-frame Y/U/V hashes first, then raw planes at the first divergent frame and successive P-frames; check whether a later IDR restores equality. Existing first-frame-only `video-decoded.yuv` cannot by itself locate corruption that emerges later. Separate live streams are not identical-bitstream comparisons.

Sunshine documents `minimum_fps_target=0` as a minimum of half the requested FPS for static content, with duplicate frames potentially sent. Keep host settings unchanged in the decoder A/B. Then run a separate host A/B with the target set to the requested 30 FPS, restoring the original host setting afterward. Duplicate content is not evidence of a corrupted H.264 reference. See [Sunshine configuration](https://docs.lizardbyte.dev/projects/sunshine/latest/md_docs_2configuration.html#minimum-fps-target).

## Validation boundary

The source policy fixtures and XDK Release builds passed; physical reference correctness, performance, and disappearance of corruption without periodic IDRs are still unknown. Do not promote B or C or publish a fix until an exact-XEX Xbox A/B supplies those observations.
