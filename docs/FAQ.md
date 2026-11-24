# MoonX360 — Frequently Asked Questions (FAQ)

[Español](FAQ_ES.md)

Answers to the most common questions from GitHub, Reddit, and YouTube.

[Latest stable release](https://github.com/fransley2/MoonX360/releases/latest) · [Setup guide](SETUP.md)

---

## General Questions

### What exactly is MoonX360?

MoonX360 is a Moonlight-based streaming client for modified Xbox 360 consoles.

It lets you play PC games on your Xbox 360 by streaming video and audio from your computer while sending controller inputs back to it.

You need a modified Xbox 360 and a PC running Sunshine, Apollo, or another compatible GameStream host with H.264 encoding enabled.

### Is this like Xbox Cloud Gaming (xCloud)?

Xbox Cloud Gaming runs games on Microsoft's remote servers. MoonX360 uses your own PC as the host.

You don't need an Xbox Cloud Gaming subscription, but you do need a computer capable of running the games.

### Does my PC need to stay turned on?

**Yes.** Your PC runs the game, encodes the video, and sends it to your Xbox 360.

If the PC shuts down, the stream stops.

### Does MoonX360 make my Xbox 360 more powerful?

The console's hardware stays the same. MoonX360 lets the Xbox 360 display and control a game running on another computer.

### Why not just connect my PC directly to the TV?

MoonX360 is useful when your Xbox 360 is already connected to a different TV or CRT. You can use the console and its controller to play without moving your computer or running a video cable to that display.

---

## Games and Compatibility

### Can I stream any PC game, or only Steam games?

MoonX360 can stream games and apps that your compatible host can launch or capture.

- Steam
- Epic Games Store
- Xbox PC Game Pass
- GOG
- Other PC launchers
- Emulators
- Your Windows desktop

Some games may require additional controller or capture configuration.

### Can I play GTA 6 or other modern games?

If a game has a compatible PC version and your computer can run it, MoonX360 may be able to stream it. Performance depends on the PC and the Xbox 360's video decoder.

### Can I stream PS4 games using shadPS4?

Potentially, yes.

If shadPS4 runs a game properly on your PC and your streaming host can capture it, MoonX360 can receive that video stream.

Game compatibility and emulator performance depend on shadPS4 and your PC.

### Do I need to download games to my PC?

Yes, for locally installed PC games.

Install each game using its usual installer or launcher.

### Do Steam or Epic Games achievements still work?

Yes, provided the game and its launcher support achievements and the usual requirements are met.

The game still runs on your PC, using your normal account.

### Can I play online multiplayer?

Yes, if the PC game supports online multiplayer.

The PC handles the game and its online connection. MoonX360 only handles streaming and remote input.

MoonX360 works without Xbox Live.

---

## PC and Network Requirements

### Do I need a powerful PC?

Your PC needs to be capable of running the game you want to play and encoding its video.

A weaker PC may work well for lightweight games, older titles, or emulators.

Streaming does not improve your PC's gaming performance.

### Do I need an internet connection?

For local streaming, your PC and Xbox 360 need a working local network connection.

An active internet connection is not inherently required for the video stream itself, but downloading software, authenticating with launchers, and playing online games may require internet access.

### Can I use Wi-Fi?

Yes, but wired Ethernet is recommended for better stability.

The Xbox 360's wireless connection can struggle with packet loss, interference, and inconsistent latency.

For Wi-Fi, try the **Wireless Stable** profile first.

### Can I connect my Xbox 360 directly to my PC using Ethernet?

Yes.

A direct Ethernet connection or Windows Internet Connection Sharing setup can work, provided both devices have valid IP addresses and can communicate.

When adding the PC manually, enter the IP address of the PC's Ethernet interface.

### What settings should I use?

Use these presets as starting points.

| Profile            | Resolution | FPS target | Bitrate |
| ------------------ | ---------- | ---------- | ------- |
| Smooth (default)   | 1280×720   | 60         | 6 Mbps  |
| Maximum Quality    | 1280×720   | 60         | 10 Mbps |
| Standard           | 1280×720   | 30         | 6 Mbps  |
| Performance        | 960×540    | 60         | 3 Mbps  |
| Wireless Stable    | 854×480    | 60         | 2 Mbps  |
| CRT                | 640×480    | 60         | 3 Mbps  |
| 1080p (Experimental) | 1920×1080  | 30         | 8 Mbps  |

Lower the bitrate or FPS if you experience stuttering.

The requested FPS does not guarantee that every frame will be decoded and presented on time.

---

## Installation

### Do I need a modified Xbox 360?

**Yes.**

MoonX360 currently requires an Xbox 360 capable of running homebrew XEX applications, such as an RGH or JTAG console.

### Can I install MoonX360 on Aurora?

Yes.

Download `default.xex` from the latest stable release, copy it to a writable folder on your console's HDD or USB storage, and launch it using Aurora, FSD, or XeXMenu.

You do not need to replace your existing dashboard.

### Where is the MoonX360 dashboard download?

The dashboard is included in **`default.xex`**.

You don't need a separate dashboard executable.

[Download `default.xex`](https://github.com/fransley2/MoonX360/releases/latest)

### Which release files should I download?

For a stable installation, download the standalone `default.xex` and `SHA256SUMS.txt` from the [latest stable release](https://github.com/fransley2/MoonX360/releases/latest). Compare the SHA-256 hash of `default.xex` with its entry in `SHA256SUMS.txt` before copying it to the console. If you also see a ZIP labeled as a pre-release, use the standalone files for a stable installation.

### Does MoonX360 need Xbox Live?

No.

It doesn't use Xbox Live for streaming or gamepad input.

### What about a banned Xbox 360?

MoonX360 doesn't depend on Xbox Live authentication. An Xbox Live ban alone doesn't prevent local homebrew streaming, provided your console can run the application and connect to the host.

---

## Controllers, Mouse and Keyboard

### Does my Xbox 360 controller work with PC games?

Yes, with a properly configured streaming host and virtual controller backend.

The Xbox 360 reads your controller inputs and sends them to the host over the network.

Your PC then receives those inputs through the host's virtual controller system.

### Why isn't my controller detected in games?

Sunshine or your compatible host must create a virtual gamepad on the PC. If a game sees no controller, check the host first.

1. Enable gamepad input in Sunshine or your compatible host.
2. Verify that the virtual gamepad driver required by your host version and configuration is installed and working. Some Sunshine configurations use ViGEmBus. Check the [Sunshine gamepad settings](https://docs.lizardbyte.dev/projects/sunshine/latest/md_docs_2configuration.html#gamepad) for the backend your installation uses.
3. Select Xbox 360/XInput emulation if your host offers it.
4. Reconnect, then open `joy.cpl` on the PC while the stream is active. Check whether the virtual gamepad appears and responds to button presses.

### Does the Xbox 360 act as a wireless controller receiver for my PC?

The controller connects to the Xbox 360. MoonX360 sends button presses over the network, and your streaming host creates a virtual gamepad on the PC.

### Can I control the Windows desktop?

Yes.

MoonX360 includes Desktop Mode with mouse emulation and an on-screen Smart Keyboard.

Open the Quick Menu to toggle **Mouse mode** and access the keyboard during a stream.

### How do I open the Quick Menu?

While streaming, press **Back + L3** (press the left analog stick).

The Quick Menu lets you resume the stream, request a picture refresh, open the Performance Overlay, or disconnect and return to MoonX360.

---

## CRT and Retro Gaming

### Does MoonX360 support 4:3 CRT televisions?

Yes. The **CRT** preset streams at 640×480, and **CRT Picture** has controls for aspect ratio, scaling filters, screen position, and UI safe area. For a 480p-capable display, select 480p in the Xbox 360 video settings if your cable and display support it. The stream preset controls the incoming resolution.

### Can I use 240p?

Experimental 240p support is available for compatible setups.

The **CRT Retro (240p)** preset requires 480i/60 Hz SD output and a compatible external `t240.xex` plugin (v2.0 or later) loaded via DashLaunch. When 240p is unavailable, use the standard **CRT** preset.

### Does MoonX360 support custom resolutions such as 2560×240?

The documented experimental 240p mode is **640×240**. A custom resolution on the PC controls the captured image. The Xbox 360 and its video configuration determine the output signal.

### What bitrate should I use with a CRT?

Start with the CRT preset at **640×480 / 60 FPS / 3 Mbps**.

You can experiment with 5 Mbps or higher if your console maintains stable decoding and frame pacing.

Increasing bitrate doesn't always improve the experience on Xbox 360 hardware.

---

## Performance and Troubleshooting

### Is there input lag?

Yes, like any game streaming system, MoonX360 introduces some additional latency.

Actual latency depends on capture, encoding, network conditions, decoding, and display presentation.

Ethernet, lower bitrates, and appropriate streaming settings can help.

### Why does my stream stutter?

Possible causes include:

- Bitrate too high for the decoder.
- Network packet loss or jitter.
- Host capture or encoding delays.
- Video decode overload.
- Frame pacing or presentation issues.

Try a lower bitrate or the Standard 720p30 profile, then check the Performance Overlay.

### Why do I see blocking or corrupted colors?

Some H.264 artifacts can be caused by decoding errors or corrupted reference frames.

MoonX360 includes improvements to normative H.264 deblocking and video recovery, but some visual issues may still occur.

Try the latest version and a lower bitrate. If the problem continues, report your host, resolution, FPS, bitrate, and Fast Decode setting.

### Why does Steam open on my PC instead of my Xbox?

That's expected.

Steam runs on the PC. MoonX360 displays the captured video on your Xbox 360.

If Steam opens but nothing appears on the Xbox, verify the correct display and capture configuration on your host.

### Why is the video stretched, cropped, or not fullscreen?

Check these settings.

- Windows host display resolution.
- Sunshine or compatible host capture display.
- Xbox 360 output aspect ratio.
- MoonX360 CRT Picture settings.
- Whether the streaming resolution matches your intended display aspect ratio.

If you're using a virtual display, verify that the host is capturing the correct display.

### Why does my stream freeze on the first frame while audio and controls still work?

The problem may involve video packets, decoder initialization, or frame presentation.

Try a lower-resolution profile, reconnect, and compare with another compatible streaming client.

If the issue persists, collect Performance Overlay information and describe whether it also happens with other clients.

### What does "This legacy host has no saved server identity" mean?

MoonX360 doesn't have the saved identity required for that host entry.

Reconnect at the PC's known address or select it again in **My Computers** so MoonX360 can update the saved entry. If MoonX360 shows **Pair with host**, follow the PIN prompt.

When updating MoonX360, preserve your `moonlight/` configuration folder and pairing certificates.

---

## Android and Cloud Gaming

### Can I use my Android phone instead of a PC?

The standard MoonX360 setup expects a compatible GameStream host such as Sunshine.

Moonlight on Android receives a stream from a compatible host.

Android-to-Xbox screen mirroring would require a separate compatible host implementation.

### Is there Moonlight for Android or iPhone?

Yes.

[Moonlight](https://moonlight-stream.org/) has Android and iOS clients for streaming from a PC to a phone or tablet.

### Does MoonX360 support GeForce NOW or Xbox Cloud Gaming directly?

MoonX360 streams from your own PC. Direct integration with those services is unavailable.

Cloud services accessed through a PC may introduce additional latency, compatibility limitations, and service restrictions.

---

## Updates and Support

### How do I update MoonX360?

1. Download `default.xex` and verify its checksum as described in [Installation](#installation).
2. Back up your existing files.
3. Replace the previous executable.
4. Keep your existing `moonlight/` folder to preserve saved hosts, profiles, and pairing data.

### Where can I report bugs?

[Open a GitHub issue](https://github.com/fransley2/MoonX360/issues) and include the following details.

- Xbox 360 model.
- MoonX360 version.
- Host software and version.
- Wi-Fi or Ethernet.
- Resolution, FPS and bitrate.
- Fast Decode setting.
- Description of the problem.
- Logs, screenshots or video if possible.

### Is MoonX360 still being developed?

Yes. MoonX360 is an experimental homebrew project under active development.

Performance improvements and new features are being tested, but some limitations remain due to the Xbox 360's hardware.

### Is MoonX360 based on Xenonlight?

Yes. MoonX360 builds on [Xenonlight](https://github.com/roderick-neuhoff/Xenonlight) and [Moonlight](https://moonlight-stream.org/). See the [README](../README.md#credits--license) for credits and licensing.

---

*MoonX360 is an independent homebrew project and is not affiliated with Microsoft, Xbox, Valve, or NVIDIA.*
