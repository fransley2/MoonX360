# MoonX360 — Frequently Asked Questions (FAQ)

[Español](FAQ_ES.md) · [Back to README](../README.md)

---

### Does my PC need to stay turned on?
**Yes.** MoonX360 is a streaming client. Your PC runs the game, encodes H.264 video and Opus audio, and sends them to your Xbox 360 over your local network.

### Does MoonX360 run PC games natively on Xbox 360?
**No.** The Xbox 360 cannot execute modern Windows x86/x64 games natively. MoonX360 receives compressed video/audio from your host PC and sends controller input back.

### Can I play Steam, Epic Games, or Xbox PC titles?
**Yes.** Any game or desktop application that your host PC (Sunshine, Apollo, or a compatible GameStream server) can launch or capture can be streamed to MoonX360—including Steam, Epic Games Store, Xbox PC Game Pass, GOG, emulators, and your Windows desktop.

### Does it work with a mobile phone instead of a PC?
**No** in standard use. MoonX360 requires a Moonlight/GameStream-compatible host (such as Sunshine or Apollo) running on a PC. Standard Moonlight mobile apps are clients, not hosts.

### Does it require Xbox Live?
**No.** MoonX360 runs entirely over your local network and works on offline or banned consoles without Xbox Live.

### Does it work over Wi-Fi?
**Yes, with caveats.** Wired Ethernet (`100 Mbps`) is strongly recommended for `720p60` (`6–12 Mbps`). Xbox 360 Wi-Fi experiences higher jitter and packet loss; if you use Wi-Fi, start with the **`Wireless Stable`** preset (`854×480 @ 60 FPS`, `4 Mbps`, `768 B` packets).

### Can I use a CRT television?
**Yes.** MoonX360 supports `4:3` CRT TVs, Component, and VGA setups (`CRT` preset `640×480 @ 60 FPS`, `3 Mbps`) with scaling filters, aspect modes, screen position/overscan controls, and a `640×480 (4:3)` UI layout. Experimental **`CRT Retro (240p)`** (`640×240`) requires `480i/60 Hz` console output plus an external compatible `t240.xex` DashLaunch plugin. See [CRT.md](CRT.md).

### Can the Xbox 360 display 1080p60 streams?
**No.** The Xbox 360 CPU decodes H.264 in software; `1280×720 @ 60 FPS` (`6–10 Mbps`) is the primary 60 FPS target. `1920×1080` is offered as **`1080p30 (Experimental)`** (`30 FPS`, `10 Mbps`) for desktop and low-motion viewing when the Xbox 360 dashboard is set to `1080p`.

### Can I stream over the internet?
MoonX360 is designed and validated for **local network (LAN)** streaming. Streaming over the internet or VPN is unsupported, adds latency, and exposes pairing/streaming ports if port-forwarded.

### Why does my stream stutter?
- Check the **Detailed** Performance Overlay (`Back + LB + RB + X`).
- If **`Dropped = 0`** and decode time is below `16.6 ms`, your Windows host (WGC capture) may be throttling an unfocused or non-fullscreen window—run the game fullscreen.
- If **decode time exceeds `16.6 ms`**, switch from `High Quality (Experimental)` (`12 Mbps`) to **`Smooth`** (`6 Mbps`), **`Performance`** (`540p60`), or **`Standard`** (`720p30`).
- On Wi-Fi, switch to **`Wireless Stable`**. On lossy wired LANs, try **`1024 bytes`** packet size in **Stream → Video → Stream compatibility**.

### Why is my controller not working?
- Make sure **Desktop Mode** (`Back + R3`) is **Off** during gameplay.
- On Windows, confirm **ViGEmBus** (virtual Xbox 360 controller driver) is installed for Sunshine/Apollo.
- Click inside the game window if it lost focus.

### Why does pairing fail?
- Ensure your host PC and Xbox 360 are on the same local subnet and allow **TCP `47984`, `47989`, `48010`** and **UDP `47998–48010`** in Windows Firewall.
- Verify **H.264** is enabled on the host.
- If your PC's IP changed, use **Computers → Host → Connection test** to update it.

---

## Preset Quick Reference

| Preset | Resolution | Target FPS | Bitrate | VSync |
|---|---|---:|---:|---|
| **Smooth** *(Default)* | `1280×720` | `60` | `6 Mbps` | On |
| **Performance** | `960×540` | `60` | `5 Mbps` | On |
| **High Quality (Experimental)** | `1280×720` | `60` | `12 Mbps` | On |
| **Standard** | `1280×720` | `30` | `6 Mbps` | On |
| **1080p30 (Experimental)** | `1920×1080` | `30` | `10 Mbps` | On |
| **Wireless Stable** | `854×480` | `60` | `4 Mbps` | Off |
| **CRT** | `640×480` | `60` | `3 Mbps` | Off |
| **CRT Retro (240p)** | `640×240` | `60` | `3 Mbps` | On |

See [TROUBLESHOOTING.md](TROUBLESHOOTING.md) for bug-reporting instructions and privacy reminders.
