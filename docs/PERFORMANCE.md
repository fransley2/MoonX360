# MoonX360 Performance Results & Methodology

This document records the measured Xbox 360 hardware performance of **MoonX360 `v0.2.6-rc1`** (`Final-release`, preserving accepted optimizations **E5b + E7 + E8 + E9** on top of the `v026-rc2` H.264 decoder build).

---

## 1. Test Environment & Benchmark Conditions

- **Console:** Physical Xbox 360 (`1280×720` native output, `VSync` enabled, wired Ethernet LAN)
- **Host:** Apollo GameStream-compatible host (`1280×720 @ 60 FPS`, `10 Mbps`)
- **Video Stream:** H.264, `CAVLC`, `8` slices, full reference-frame deblocking (`fast_decode=0`, `IDC=0`)
- **Session Protocol:** Three interleaved **120-second** live streaming sessions per configuration (`B0-final-r1..r3` vs `Final-r1..r3`)
- **Build Configuration (`Final-release`):**
  - `XDK_CONFIGURATION=Release`, `XDK_OPTIMIZATION=aggressive`, `XDK_WORKER_MAP=current`, `XDK_SLICE_DISPATCH=legacy`
  - `XDK_LOW_LATENCY_INPUT=1`, `XDK_INPUT_HW_THREAD=0` (`E7`)
  - `XDK_YUV_CONTIGUOUS_PLANES=1` (`E5b`)
  - `XDK_WIRED_1392_MIN_KBPS=4000` (`E8`)
  - `frame_pacing_mode=1` (`FRAME_PACING_LATEST_WINS`, `E9`)

---

## 2. Sampling Populations & Methodology Correction

During validation, an earlier `decoder-session.txt` summary showed an apparent `8.73 ms` `decode_p95_us` in `B0-final-r2`. Inspection confirmed this was a sampling-window artifact rather than a true session-wide drop:

- **`decoder-session.txt` `tail256` (`rolling_256_samples`)**: Covers only the final ~4.3 seconds of a session (mostly idle desktop frames at teardown).
- **Full-session 120s (`decoder-session.txt`)**: `decode_avg_us` and `decode_max_session_us` cover all ~7,125 frames of the 120-second run.
- **Steady-state 45s window (`performance-session.txt`, `h264_avcodec_call_*`, `n = 2,634–2,643`)**: Consistent rolling `Avg / P50 / P95 / P99 / Max` population across all 6 runs.

Both populations are reported separately below without conflation.

---

## 3. Corrected Before / After Measurements

| Metric | Baseline (`B0-final`, r1/r2/r3 → Mean) | Optimized (`Final`, r1/r2/r3 → Mean) | Delta |
|---|---|---|---:|
| **Wired Packet Size** | `1024 / 1024 / 1024 B` | **`1392 / 1392 / 1392 B`** | `+368 B/pkt` |
| **Full-120s Decode Avg (ms)** | `13.17 / 13.21 / 12.85` → `13.08` | `13.15 / 13.53 / 13.38` → `13.36` | `+0.28 ms` |
| **Full-120s Decode Max (ms)** | `17.86 / 17.49 / 17.66` → `17.67` | **`16.61 / 16.65 / 16.72` → `16.66`** | **−1.01 ms** |
| **45s Steady Decode Avg (ms)** | `13.11 / 14.25 / 12.29` → `13.22` | **`12.39 / 13.61 / 12.82` → `12.94`** | **−0.28 ms (−2.1%)** |
| **45s Steady Decode P95 (ms)** | `14.47 / 15.11 / 15.27` → `14.95` | **`14.70 / 14.22 / 14.20` → `14.37`** | **−0.58 ms (−3.9%)** |
| **45s Steady Decode P99 (ms)** | `14.69 / 15.33 / 15.42` → `15.15` | **`15.09 / 14.32 / 14.59` → `14.67`** | **−0.48 ms (−3.2%)** |
| **Tail-256 Decode P95 (ms)** | `14.73 / 8.73 (idle tail) / 13.74` | `14.52 / 15.15 / 14.84` → `14.84` | End-of-session 256-frame window |
| **YUV Upload Avg (ms)** | `1.13 / 1.15 / 1.15` → `1.14` | **`0.90 / 0.91 / 0.91` → `0.91`** | **−20.5%** |
| **YUV Upload P95 (ms)** | `1.20 / 1.23 / 1.23` → `1.22` | **`1.00 / 1.01 / 1.01` → `1.01`** | **−17.5%** |
| **Admit→VBlank P50 (ms)** | `28.04 / 33.66 / 33.70` → `31.80` | **`26.25 / 30.23 / 26.21` → `27.56`** | **−4.24 ms** |
| **Admit→VBlank P95 (ms)** | `28.44 / 33.83 / 33.82` → `32.03` | **`26.51 / 30.58 / 26.57` → `27.89`** | **−4.15 ms (−12.9%)** |
| **Presented FPS (incl. startup)** | `59.47 / 59.50 / 59.50` → `59.49` | `59.49 / 59.47 / 59.55` → `59.51` | `+0.02 FPS` |
| **Dropped Frames** | `0 / 2 / 0` (`2` total) | **`0 / 0 / 0` (`0` total)** | **−2 drops** |
| **Decode Errors / Audio Glitches** | `0 / 0` | **`0 / 0`** | Parity |
| **Input Scheduling Jitter P95 (µs)** | `10,713 / 11,964 / 11,963` → `11,547` | **`2,262 / 2,255 / 2,390` → `2,302`** | **−80.1%** |
| **Input Scheduling Jitter P99 (µs)** | `11,658 / 14,407 / 15,016` → `13,694` | **`2,777 / 2,506 / 2,737` → `2,673`** | **−80.5%** |
| **Audio Buffer Avg** | `2.00 / 4.93 / 2.08` → `3.00` | **`1.96 / 1.98 / 1.99` → `1.98`** | Tight & consistent |
| **FEC Failed Deficit Pkts** | `0 / 0 / 0` | `11` (`r1`) / `0` (`r2`) / `0` (`r3`) | See §5 |

---

## 4. Technical Explanation of Accepted Optimizations

1. **Contiguous YUV Upload (`E5b`, `XDK_YUV_CONTIGUOUS_PLANES=1`)** — Uses `__dcbt` prefetch, `__dcbz128` cache-line zeroing, and contiguous `XMemCpy` transfers for planar Y, U, and V buffers into `CPU_CACHED_TEXTURE` slots (`D3DFMT_LIN_L8`). This is **not** zero-copy: the CPU still copies decoded FFmpeg planes into GPU-readable cached textures, but doing so contiguously with cache hints cuts upload time by ~20.5% and reduces memory bus contention during decode spikes.
2. **Input Polling Affinity on Xenon HW0 (`E7`, `XDK_INPUT_HW_THREAD=0`)** — Moves the `1 ms` `XInputGetState` polling thread off the busy presentation/worker core onto HW0.
   - **Important distinction:** The `~80.1%` improvement (`11.55 ms → 2.30 ms` P95) measures **client-side input scheduling jitter** (polling/wake-up timer consistency under load), **not** total end-to-end input-to-display ("button-to-photon") latency. Total input-to-display latency also includes USB/wireless pad polling, network RTT, host simulation/rendering, encoding, H.264 decoding, and display scanout.
3. **Wired `1392 B` UDP Threshold (`E8`, `XDK_WIRED_1392_MIN_KBPS=4000`)** — When `XNetGetEthernetLinkStatus()` reports a wired Ethernet link (`CONSOLE_LINK_WIRED`), `packet_size == 1024`, and `bitrate_kbps >= 4000`, `console_link_effective_packet_size()` upgrades RTP packet size to `1392` bytes, reducing per-frame packet count and receive/FEC overhead. `Wireless Stable` (`768 B`) and lower bitrates remain unchanged.
4. **Latest-Frame-Wins Frame Pacing (`E9`, `FRAME_PACING_LATEST_WINS`)** — Selects the newest decoded frame before VBlank instead of draining an older queued frame, reducing Admission-to-VBlank P95 by ~12.9%.

---

## 5. Isolated FEC Deficit Note (`Final-r1`)

During `Final-r1`, `network-session.txt` and `decoder-session.txt` logged `incomplete_block_data_deficit=11` and `fec_failed_data_deficit=11` (with `rtp_oos_packets=0`, `frame_loss_notifications=0`, and `decode_errors=0`). `Final-r2` and `Final-r3` recorded `0` deficits. Because `1392`-byte packets reduce packet count per frame while increasing the payload lost per dropped datagram relative to `1024`-byte packets, marginal or lossy LAN paths should be monitored; users experiencing packet loss can manually set `packet_size` to `1024` bytes under **Stream → Video → Stream compatibility**.

---

## 6. Rejected & Archived Experiments

| Experiment | Configuration | Measured Outcome | Disposition |
|---|---|---|---|
| `E2-4slices` | `--slices 4` | Live 30s decode `14.32 / 15.89 ms` (avg/P95) vs `13.84 / 15.06 ms` B0; input jitter `10,821 / 12,016 µs` | **Rejected** (kept at 8 slices) |
| `E3-batched` | `XDK_SLICE_DISPATCH=batched` | Offline replay `13.40–13.52` vs `13.44 ms` (`cj`), `15.63–15.71` vs `15.71–15.90 ms` (`gta5`) | **Archived** (negligible delta; production uses `legacy`) |
| `E4-opt-yuv` | `XDK_OPTIMIZED_YUV_COPY=1` | Overlapped `E5b`; worse Admit→VBlank (`28.14 ms`) & input jitter (`12,055 µs`) | **Rejected** |
| `E5a-yuv-streaming` | `XMemCpyStreaming_Cached` | YUV upload regressed `+63%` (`1.89 / 1.95 ms` vs `1.16 / 1.23 ms`) | **Rejected** |
| `E6-input-fast-kb` | `XDK_INPUT_FAST_KB_SKIP=1` | Input jitter P95 `3,136 µs` (inferior to `E7`'s `2,291 µs`) | **Rejected** |

---

## 7. Current Limits & Post-Build Validation Status

- **CPU Decode Budget at 720p60:** With full reference-frame deblocking (`IDC=0`), steady-state 45s decode P95 (`14.37 ms`) fits within the `16.67 ms` 60 Hz budget (`0` drops in 3×120s validation), leaving limited headroom for bitrate spikes above `10–12 Mbps`.
- **Post-Rebuild Production Check:** `Final-release/default.xex` (`SHA-256: 55d9d66e...`) passed host unit/regression suites (`config`, `frame-pacing-experiments`) and booted cleanly on physical Xbox 360 hardware, returning to Aurora cleanly. Automated post-rebuild `120s` live streaming + `3× reconnect` checks were blocked by an open `ApolloService` TCP socket on port `47984` causing 4-second TLS timeouts; `v0.2.6-rc1` is therefore distributed as a **Pre-release** pending clean live reconnect confirmation.
