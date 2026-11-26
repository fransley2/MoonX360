# CRT & 4:3 Display Guide

MoonX360 includes dedicated support for **4:3 CRT televisions**, **VGA monitors**, and **SDTV** setups alongside experimental **240p** integration.

---

## 1. Standard 4:3 CRT Setup (`640×480`)

1. Set your Xbox 360 system display output in the Xbox Dashboard to **`480i`**, **`480p`**, or **`640×480` VGA** (`4:3`).
2. In MoonX360, go to **Settings → Interface → App display**:
   - Set **UI layout** to **`4:3`** (or **`Auto`** to follow console output) and **App resolution** to **`640×480 (4:3)`** if desired.
   - Adjust **UI safe area** (`0%–10%`) to keep menus clear of CRT overscan.
   - You can revert anytime using **Restore previous app display**.
3. Go to **Stream → Stream profile** and select **`CRT`** (`640×480 @ 60 FPS`, `3 Mbps`, Rec.601 color matrix).

---

## 2. CRT Picture Controls (`Stream → Video → CRT Picture`)

- **Scaling Filter:** `Auto`, `Point / Nearest`, `Bilinear`, or `Sharp-Bilinear` (custom single-pass D3D9 HLSL pixel shader).
- **CRT Preset:** `CRT Custom`, `CRT Standard 480`, `CRT Retro 4:3`, `CRT Integer Sharp`, `CRT Low Latency`.
- **Aspect Ratio:** `Fit to screen (4:3)`, `Pixel-to-pixel (1:1)`, `Crop to 4:3`, `16:9`, `Stretch`, `Custom`.
- **Integer Scaling:** Locks scaling to integer multiples where supported.
- **Screen Adjustment:** Fine-tune horizontal/vertical size (`80%–110%`), picture position (`-20 px` to `+20 px`), and game overscan (`0%–10%`) independently of the menu UI safe area.
- **CRT Interlace Clarity (Experimental):** Optional spatial interlace shader for `480i` CRT displays (`Off` by default).

---

## 3. CRT Retro (`640×240` / `240p`) Requirements

> [!IMPORTANT]
> **MoonX360 does not write raw Xenos video registers or bundle kernel hooks.** True `240p` output requires an external plugin.

To use the **`CRT Retro (240p)`** preset (`640×240 @ 60 FPS`, `3 Mbps`, `Point / Nearest` scaling, `VSync On`):

1. Configure your Xbox 360 console video output to **`480i` (`60 Hz`)** via Component, Composite, or RGB SCART.
2. Install an external compatible **`t240.xex`** DashLaunch plugin (such as [`xbox360-240p-plugin`](https://github.com/Tupapa2712/xbox360-240p-plugin)) in DashLaunch. MoonX360 communicates cleanly via the documented `S240` v3 in-memory beacon.
3. On **HDMI or HD resolutions (`720p`/`1080p`)**, MoonX360 detects the incompatible output mode, automatically blocks `240p`, and falls back safely to **`640×480` (`CRT`)**.
