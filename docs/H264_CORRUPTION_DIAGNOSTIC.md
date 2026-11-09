# H.264 corruption investigation — 2026-11-05

## Evidence and scope

The reported CRT/VGA 640x480, 10 Mbps image shows persistent spatial corruption. A photograph alone cannot distinguish damaged H.264 references from a bad YUV upload or GPU sampling. No fresh packet capture, decoded frame, XBDM trace, or run of this build on the affected console was available. The visible symptom is **not** evidence that 10 Mbps is insufficient.

The receive path is `VideoStream.c` UDP → `RtpVideoQueue.c` FEC → `VideoDepacketizer.c` access units → `video_submit()` → bundled FFmpeg H.264 slice threads and DPB → planar YUV → `video_cached.cpp` or legacy staging → `gfx_xdk.cpp` texture upload and Present. `VideoDepacketizer.c` asks for an IDR after `DR_NEED_IDR`; its frame-loss path can use RFI when advertised. The Release build does not enable the source's `FEC_VALIDATION_MODE`, which is conditional on `LC_DEBUG`.

Confirmed source defect: the bundled H.264 decoder records damaged macroblocks, missing slices and missing references internally, but does not populate `AVFrame.decode_error_flags`. The multi-slice dispatcher discarded worker return values. `video_submit()` consequently published a frame whenever `avcodec_decode_video2()` returned a nonnegative value and `got` was set. A damaged reference can then be retained by the H.264 DPB and used by later P-frames. This is a proven acceptance path, **not** proof that it caused the photographed incident.

## Changes in the instrumented build

- FFmpeg records slice errors, missing references and incomplete macroblock coverage on the returned picture. Its slice dispatcher now observes every worker result. `AV_EF_CAREFUL` enables the old error coverage tracker; a decoder result can still be nonnegative when the picture is flagged.
- `video_submit()` rejects flagged frames before YUV upload, sets the IDR wait, and uses the existing Moonlight refresh request. Dependent P-frames are skipped while waiting. The previous displayed texture is left alone. The control thread spaces repeated IDR sends by at least 250 ms and remains interruptible on stream stop.
- `game:\moonlight\video-h264.ini` provides session-only switches. With no file, the existing FAST flag, worker count and RFI advertisement remain selected. `workers=1` retains the same advertised slice count, so the host slice request stays constant during the thread comparison.
- Optional `capture_yuv=1` writes one good decoded raw planar YUV420 picture to `game:\moonlight\video-decoded.yuv` with dimensions in `video-decoded.txt`. The capture is limited to 4 MiB. In the cached texture path the first upload also compares every active texture row with the decoder's YUV row before UnlockRect and logs `[H264 DIAG] cached YUV upload comparison`. This checks CPU-visible texture contents, not GPU scanout or cache coherency after UnlockRect.

Example diagnostic file (change one switch at a time and restart the stream):

```ini
fast_flag = 1
workers = default
rfi = 1
capture_yuv = 1
```

For the A/B runs use `fast_flag = 0`, then `workers = 1`, then `rfi = 0` in separate sessions. `rfi = 0` uses IDR recovery for lost frames. Compare with both `video-gpu.ini` pipeline choices (`auto` and `legacy`) under otherwise identical settings. Use the actual 640x480/30 and /60 at 10 Mbps and 720p/30 and /60 at 12 Mbps profiles. Include a static screen, small text changes, sudden motion, and repeated static/moving transitions. Let the image stay static long enough to check that the displayed surface remains stable without new frames.

Preserve each run's exact XEX hash and collect `last-log.txt`, `decoder-session.txt`, `gpu-cached-selftest.txt`, `last-video-stall.txt` if present, `video-decoded.*` if enabled, host encoder/network logs, and a camera recording of the output. Search the log for `[H264 CORRUPT]` and `[H264 DIAG]`. Reconstruct a 640x480 capture on a PC with `ffplay -f rawvideo -pixel_format yuv420p -video_size 640x480 video-decoded.yuv` (substitute the metadata dimensions). If the raw YUV is corrupted, investigate encoder/FEC/depacketization/decoder; if it is clean while the screen is corrupt, investigate texture coherency, shader or output. The current raw capture is the first accepted frame, so it may not coincide with a later visual failure; a targeted later capture would then be needed.

## Validation status

Enable the stats overlay when collecting `decoder-session.txt`; the app writes that report only for overlay-enabled sessions.

Before: source accepted nonnegative FFmpeg results with `got=1` without an H.264 corruption flag; the bundled decoder did not set that flag. After: flagged pictures are rejected in code and the refresh path is exercised by the existing protocol. The targeted host fixtures cover parser defaults/overrides, upload byte comparison and injected mismatch, slot/fence reuse, malformed pitches, watchdog, stream bounds and repeated teardown. FFmpeg and the Release XEX compile; `imagexex /dump` recognizes the XEX. No physical before/after comparison has been made. The root cause of the photographed corruption remains unproven until the exact instrumented XEX is run on the affected Xbox.
