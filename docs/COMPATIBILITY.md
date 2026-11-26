# MoonX360 Hardware & Host Compatibility

MoonX360 requires an Xbox 360 capable of running unsigned homebrew `.xex` executables and a host PC running Sunshine, Apollo, or a compatible GameStream server on the local network.

---

## 1. Xbox 360 Console & Exploit Compatibility

| Hardware / Environment | Status | Details & Evidence |
|---|---|---|
| **Xbox 360 Slim / E (Corona)** | **Expected / Developer-Tested Hardware Class** | Physical validation in `v0.2.5` and the `v0.2.6` performance campaign ran on a physical RGH Xbox 360 console via XBDM and live Apollo/Sunshine streaming (`1280×720@60`, wired LAN). |
| **Xbox 360 Slim (Trinity)** | **Expected Compatibility** | Uses standard XDK D3D9 / XAudio2 / XInput / XNet APIs; compatible with RGH dashboards (Aurora, FSD, XeXMenu). Community reports welcome. |
| **Xbox 360 Fat (Jasper, Falcon, Zephyr, Xenon)** | **Expected Compatibility (Untested in `v0.2.6`)** | Standard XDK executable (`default.xex`) with no motherboard-specific register hacks; however, Fat revisions were not physically bench-tested in `v0.2.6`. |
| **RGH / JTAG (Aurora, FSD, XeXMenu)** | **Tested by Developer** | Primary validated environment. Requires writable HDD (`Hdd1:`) or USB (`Usb0:`) storage for `moonlight/`. |
| **BadUpdate / XeUnshackle** | **Not Yet Tested** | MoonX360 uses standard retail-compatible XDK imports (`xam.xex`, `xboxkrnl.exe`, `has_xbdm=false`) and avoids raw kernel hooks, so it is expected to launch wherever homebrew XEX execution and writable storage are active, but has not been directly verified on BadUpdate. |

> [!NOTE]
> MoonX360 does **not** claim universal support across every motherboard revision or exploit loader without evidence. If you test MoonX360 on a Fat, Trinity, or BadUpdate console, please share your console revision, display output, and results in [GitHub Issues](https://github.com/fransley2/MoonX360/issues).

---

## 2. Video Outputs & Displays

| Output / Display | Status | Notes |
|---|---|---|
| **HDMI (`720p` / `1080p`)** | **Tested by Developer** | `1280×720` is the recommended native streaming target. `1080p` UI / `1080p30` streaming works over HDMI when the Xbox 360 dashboard is set to `1080p`. |
| **Component / VGA (`480p` / `720p` / `640×480` 4:3)** | **Expected Compatibility** | Supported via `Settings → Interface → App display` (`640×480 (4:3)` UI layout) and `Stream → Video → CRT Picture`. |
| **CRT / SDTV (`480i` 4:3)** | **Tested in `v0.2.5` / `CRT` Mode** | Uses Rec.601 color matrix, `4:3` scaling (`Auto`, `Point / Nearest`, `Bilinear`, `Sharp-Bilinear`), screen offset (`±20 px`), overscan, and safe area controls. |
| **CRT Retro `240p` (`640×240`)** | **Hardware-Guarded / Requires External Plugin** | Requires console set to `480i/60 Hz` **plus** an external compatible [`t240.xex`](https://github.com/Tupapa2712/xbox360-240p-plugin) DashLaunch plugin (`S240` v3 beacon). On HDMI/HD modes, MoonX360 blocks `240p` and safely falls back to `640×480`. |

---

## 3. Networking

| Connection | Status | Notes |
|---|---|---|
| **Wired Ethernet (`100 Mbps`)** | **Tested by Developer (Recommended)** | Required for stable `720p60` (`6–12 Mbps`). At `>= 4 Mbps`, `v0.2.6-rc1` automatically uses `1392`-byte UDP payloads for lower overhead. |
| **Wi-Fi (Xbox 360 Wireless Adapter / Internal)** | **Known Constraints** | Xbox 360 Wi-Fi suffers higher jitter and burst loss. Use the **`Wireless Stable`** preset (`854×480 @ 60 FPS`, `4 Mbps`, `768 B` packets, `VSync Off`). |

---

## 4. Host PC Software

| Host Software | Status | Notes |
|---|---|---|
| **Apollo** | **Tested by Developer** | Tested with H.264 + `CAVLC` + `8` slices. Includes `src/http.c` `Connection: close` fix for port `47984`. |
| **Sunshine** | **Tested / Supported** | Enable H.264 in Sunshine settings; install ViGEmBus on Windows for virtual Xbox 360 controller support. |
| **Vibepollo / GameStream Forks** | **Supported** | Compatible via Moonlight RTSP/HTTPS protocol. On hosts using WGC capture, keep games fullscreen for steady 60 FPS capture admission. |

---

## 5. Known Validation Limitations in `v0.2.6-rc1`

- Physical `v0.2.6` campaign runs (`3×120s` interleaved live tests) verified the complete `E5b + E7 + E8 + E9` optimization stack.
- The final clean production build (`Final-release/default.xex`, adding `frame_pacing_mode=1` default + `http.c` + Desktop Mode fixes) booted cleanly on physical Xbox 360 hardware, but its automated `120s` live stream + `3× reconnect` smoke check was blocked by a wedged `ApolloService` TLS socket on the test PC. See [PERFORMANCE.md](PERFORMANCE.md) for details.
