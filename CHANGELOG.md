# Changelog

All notable changes to **MoonX360** are documented in this file.

## [v0.2.6] — 2026-11-27

### Single-File Distribution & Native Signed Auto-Update
- **Single-File Permanent Launcher (`default.xex`)**: `default.xex` (`0x4D583336`, independent of FFmpeg/D3D9) embeds the SHA-256-verified production `moonx360.xex` payload (`MOONXAPP`), extracts it once into `updates\runtime\moonx360.xex`, remounts `\??\game:` to the installation root on both HDD and USB, releases launcher memory across `XLaunchNewImage`, preserves Aurora Direct Launch `XSetLaunchData`, and produces zero duplicate entries in Aurora's library scan.
- **Cryptographically Signed OTA Client (`src/update/`)**: Mandatory Ed25519 manifest signature verification via Monocypher 4.0.3 (`BSD-2-Clause OR CC0-1.0`), streaming SHA-256 verification via mbedTLS 2.16.12, validated HTTPS TLS 1.2 (`MBEDTLS_SSL_VERIFY_REQUIRED` + embedded CA roots + `xdk_socket_wan` `0x5801`), bounded chunked `Range` downloads, redirect allowlists, and monotonic `build_id` anti-rollback/replay protection.
- **Recoverable Journal, Automatic Rollback & App Updates UI**: Tracks state in `updates/journal.json` (`staged`, `swapping`, `trial`, `good`, `rollback`, `manual_rollback`), automatically restores `moonx360.previous.bin` and `moonlight.ini.pre-update` if a trial fails healthy boot confirmation, preserves `moonlight/` configs and pairing certificates, supports `LB + RB + Back` startup recovery and in-app rollback, **Stable** / **Beta** channels, auto-hiding Home update badge (`8s`), localized release notes (EN / PT-BR / ES), and resilient offline startup.
- **Release & Signing Tooling (`tools/` & `docs/`)**: Added `version.json`, `src/version_info.h`, `generate_update_key.py`, `generate_update_manifest.py`, `sign_update_manifest.py`, `generate_tls_roots.py`, `package_release.py`, `build_update_test_kit.py`, [docs/UPDATES.md](docs/UPDATES.md), and [docs/RELEASE.md](docs/RELEASE.md).

### Highlights & Performance
- **Contiguous YUV Texture Upload (`E5b`, `XDK_YUV_CONTIGUOUS_PLANES=1`)**: Added `__dcbt` prefetch, `__dcbz128` cache-zeroing, and contiguous `XMemCpy` plane transfers for `CPU_CACHED_TEXTURE`, reducing YUV upload time by ~20.5% (`1.14 → 0.91 ms` avg, `1.22 → 1.01 ms` P95) and lowering peak full-session decode spikes (`17.67 → 16.66 ms` max).
- **Input Polling Affinity on Xenon HW0 (`E7`, `XDK_INPUT_HW_THREAD=0`)**: Moved the `1 ms` low-latency controller polling loop onto hardware thread `0`, eliminating decode/UI thread contention and reducing 120s input scheduling jitter P95 by ~80.1% (`11.55 → 2.30 ms` P95).
- **Wired Ethernet 1392 B UDP Threshold (`E8`, `XDK_WIRED_1392_MIN_KBPS=4000`)**: Automatically uses `1392`-byte LAN packets on wired Ethernet links at `>= 4 Mbps`, while preserving `768`-byte RTP packets for `Wireless Stable` and `1024 B` default elsewhere.
- **Latest-Frame-Wins Presentation Default (`E9`, `FRAME_PACING_LATEST_WINS`)**: Enabled `frame_pacing_mode=1` by default in `config_defaults()` and `config_apply_preset()`, reducing Admission-to-VBlank P95 by ~12.9% (`32.03 → 27.89 ms`) with `0` dropped frames across three 120s validation runs.
- **H.264 Loop-Filter & Chroma VMX1D Consistency (`v026-rc2`)**: Added `v026-opt1-edge-nolhs`, `v026-opt4-chroma-lf-nolhs`, and `v026-chroma-optA-vmx1d` on top of the 10-patch `batch16` pipeline (`XDK_SLICE_DISPATCH=legacy`), lowering steady-state 45s decode P95 from `14.95 ms` to `14.37 ms`.

### Features & Fixes
- **Vibepollo / Apollo HTTPS Detection Fix (`src/http.c`)**: Removed the `Connection: keep-alive` socket pool and TLS ticket cache so `/serverinfo`, `/applist`, `/launch`, and `/cancel` send `Connection: close` and immediately close TLS sockets cleanly, preventing wedged port `47984` connections.
- **Xbox Series Dashboard Redesign (`e9f8f2d`..`3b45b35`)**: Redesigned Home, Library, Computers, Settings, and sliding Quick Menu with smooth focus motion, unified Xbox Design System tokens, spinning ring loaders, standardized category icons, and normalized `2:3` portrait game covers.
- **Smooth Desktop Mode Analog Navigation (`src/desktop_input.h`, `xdk/src/enet_xbox.c`)**: Added radial deadzone (`desktop_stick_vector`), progressive low-deflection curve, `125 Hz` (`8 ms`) subpixel motion accumulation with immediate pre-click/stop flushes, `int64_t` motion integration, and a `1536`-byte stack buffer in `enet_socket_send()` avoiding per-packet heap allocations.
- **Smart Virtual Keyboard (`src/ui/smart_keyboard.h`)**: Added compact `3×10` in-app search keyboard and full 5-row PC virtual keyboard for Desktop Mode with offline English, Brazilian Portuguese, and Spanish word suggestions.
- **Native PC Guide Button (`src/input_guide.h`)**: Added `Start + LB` (`350 ms` hold) to emit Moonlight `SPECIAL_FLAG` without triggering the Xbox 360 system Guide.
- **File Explorer 2.0 (`src/file_explorer_*.h`, `xdk/src/file_explorer_xdk.cpp`)**: Added built-in storage browser under `Settings → Advanced → File Explorer` with Details/Compact/Large Icons views, bounded image/text/`XEX2` previews, and protected-path file management.
- **CRT Retro 2.0 & Rebalanced Presets (`src/config.c`, `src/crt_resolutions.h`)**: Added Integer Scaling, H/V size, overscan, 5 CRT aspect/preset modes, and updated factory presets (`Smooth` `6 Mbps`, `Performance` `5 Mbps`, `High Quality` `12 Mbps`, `Standard` `6 Mbps`, `1080p30` `10 Mbps`, `Wireless Stable` `4 Mbps`, `CRT` `3 Mbps`, `CRT Retro` `3 Mbps`).

---

## [v0.2.5] — 2026-11-20

- Consolidated the 10-patch `batch16` parallel H.264 pipeline with wavefront normative cross-slice deblocking across 4 Xenon worker threads.
- Added initial Desktop Mode controller mouse emulation, `2:3` portrait cover art caching, `Back + L3` Quick Menu, `Basic / Detailed / Developer` Performance Overlay, and `CRT` (`640×480`) / `CRT Retro (240p)` (`640×240` with external `t240.xex`) support.
- Removed redundant pitch-padding zeroing in legacy texture upload and stripped build-host `__FILE__` / `RSDS` paths from the release XEX.

## [v0.2.5-experimental] — 2026-11-12 (Pre-release)

- Added experimental Desktop Mode and native Xenon VMX128 verification diagnostics.

## [v0.2.4] — 2026-11-08

- Fixed streaming stability, added custom PNG/JPEG home wallpapers, and improved host recovery and advisory streaming recommendations.

## [v0.2.3] — 2026-11-05

- Added `640×480` VGA / 4:3 UI layout support, per-game profiles, and streaming enhancements.

## [v0.2.2] — 2026-11-01

- Published curated dashboard source tree with mDNS host discovery, PIN pairing, and session history.

## [v0.2.0-alpha.1] — 2026-10-30 (Pre-release)

- Initial experimental MoonX360 dashboard build forked from Xenonlight.
