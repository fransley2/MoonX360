# MoonX360 Troubleshooting & Bug Reporting

---

## 1. Stream Stutters or Low FPS

### FPS drops to ~20–55 FPS while `Dropped` / `drops` stays at `0`
- **Cause:** On Windows hosts using WGC capture (Apollo, Sunshine, or Vibepollo), unfocused windows or idle desktop views may throttle capture admission until a game runs fullscreen or foreground capture engages.
- **How to check:** Press **`Back + LB + RB + X`** twice for the **Detailed** Performance Overlay and compare **Dec FPS** / **Decode** ms vs **Dropped**. If `Dropped = 0` and decode time is well under `16.6 ms`, the bottleneck is host capture pacing, not the Xbox 360.
- **Fix:** Run the game fullscreen on the captured display and disable idle desktop frame throttling in your host settings if available.

### Occasional frame drops on `High Quality (Experimental)` (`12 Mbps`) or `1080p30`
- At `12 Mbps` (`720p60`), complex motion spikes can briefly exceed `16.67 ms` on CPU with full reference-frame deblocking active.
- Switch to **`Smooth`** (`720p60`, `6 Mbps`) or **`Performance`** (`540p60`, `5 Mbps`) for consistent 60 FPS, or **`Standard`** (`720p30`, `6 Mbps`) for heavy games.
- **Note:** Only enable **`GPU post-deblock`** (`Stream → Video`) if your host explicitly sets `disable_deblocking_filter_idc=1`. On standard hosts (`IDC=0`), `GPU post-deblock` stays safely inactive.

### Stuttering or packet deficits on Ethernet or Wi-Fi
- On **Wi-Fi**, switch to the **`Wireless Stable`** preset (`854×480 @ 60 FPS`, `4 Mbps`, `768 B` packets, `VSync Off`).
- On **marginal wired LANs**, `v0.2.6-rc1` uses `1392`-byte packets automatically at `>= 4 Mbps`. If you observe FEC deficits or packet loss on your switch/router, set **Packet size** to **`1024 bytes`** under **Stream → Video → Stream compatibility**.

---

## 2. Controller Not Working in Games

- **Check Desktop Mode:** Press **`Back + R3`** to make sure **Desktop Mode** (`Mouse mode`) is **Off** — while Desktop Mode is active, controller inputs are intentionally neutral to prevent double input in Steam/desktop.
- **Host virtual gamepad driver:** On Windows, Sunshine/Apollo require **ViGEmBus** (or the host's virtual controller driver) to expose an Xbox 360 pad to PC games.
- **Multi-controller or focus:** Click inside the game window (using Desktop Mode briefly if needed) and verify the host recognizes virtual XInput slots.

---

## 3. Pairing or Connection Failures

- **Firewall ports:** Ensure your PC firewall allows **TCP `47984`, `47989`, `48010`** and **UDP `47998–48010`**.
- **H.264 required:** Enable **H.264 (AVC)** on your host. HEVC, AV1, and HDR are unsupported on Xbox 360.
- **PC IP changed via DHCP:** Open **Computers → Host → Connection test** to refresh the host IP without losing pairing certificates.
- **Legacy host message (`no saved server identity`):** Re-select or re-add the host in **Computers** and re-pair if prompted.

---

## 4. Audio Issues

- MoonX360 defaults to **Stereo** and automatically falls back to Stereo if your console audio setting does not support 5.1 LPCM.
- Keep **Stream → Audio → Xbox audio output** set to **Stereo** unless using HDMI 5.1 LPCM.

---

## 5. How to Report a Bug Safely

Open an issue at [GitHub Issues](https://github.com/fransley2/MoonX360/issues) and include:

1. **Xbox 360 model & exploit** (e.g., Slim Corona RGH, Fat Jasper JTAG, BadUpdate).
2. **MoonX360 version** (e.g., `v0.2.6-rc1`) and **SHA-256** if verified.
3. **Host software & version** (Apollo, Sunshine, etc.) + GPU/OS.
4. **Network type** (Wired Ethernet or Wi-Fi).
5. **Stream settings** (preset, resolution, FPS, bitrate, VSync, Fast Decode).
6. **Relevant logs** (enable **Settings → Advanced → Technical diagnostics → Logs and report** if needed).

> [!CAUTION]
> **Never upload** `client.pem`, `key.pem`, `hosts.tsv`, or your entire `moonlight/` folder to GitHub Issues. Those files contain your console's private pairing keys and local host entries.
