<p align="center">
  <img src="docs/images/banner.png" width="960" alt="MoonX360">
</p>

# MoonX360

MoonX360 is a native Xbox 360 client for streaming PC games and apps from Sunshine or a compatible GameStream host.

[![Release](https://img.shields.io/github/v/release/fransley2/MoonX360)](https://github.com/fransley2/MoonX360/releases/latest)
[![License](https://img.shields.io/badge/license-GPLv3-blue)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Xbox%20360-107c10)](#requirements)

[Download](https://github.com/fransley2/MoonX360/releases) · [Installation](#download--installation) · [Build from source](#building-from-source)

## Features

- Controller-first Home, Library, Computers, and Settings, with cover artwork, favorites, and recent games.
- Host discovery, manual host entry, PIN pairing, and Wake-on-LAN for supported hosts.
- H.264 software decoding with FFmpeg, GPU video presentation, and saved stream profiles with adjustable resolution and bitrate.
- In-stream Quick Menu, manual picture refresh, optional periodic picture refresh, and a configurable Performance Overlay.
- Desktop Mode with controller mouse input and an on-screen keyboard. Library search offers local suggestions in English, Brazilian Portuguese, and Spanish.
- A 4:3 CRT profile with controls for scaling, aspect ratio, and screen position. CRT Retro (240p) is experimental.

## Download & installation

### Requirements

- An RGH or JTAG Xbox 360 with DashLaunch configured to run homebrew XEX files.
- A PC running Sunshine or a compatible GameStream host on a network reachable by the console. Enable H.264 encoding on the PC.
- A writable app folder on the console HDD or USB storage.

1. Download **default.xex** and its SHA-256 checksum from [GitHub Releases](https://github.com/fransley2/MoonX360/releases). Follow any version-specific installation instructions on the release page.
2. Place the executable at a path such as **Hdd1:/Apps/MoonX360/default.xex** and launch it from Aurora.
3. Select a discovered PC or add one under **Computers**. Choose **Pair with host** and enter the displayed PIN on the PC.
4. Select a game in **Library** and press **A**. Press **Back + L3** to open the Quick Menu during a stream. Hold **LB + RB + L3 + R3** to disconnect with the default shortcut.

Keep the **moonlight** folder beside the executable when updating. It stores settings, saved hosts, and pairing identity. Back up the folder before returning to an older version.

## Building from source

Build on Windows with Python 3 and a separately obtained Xbox 360 SDK 21256.3 with the Jul12 compiler. The source tree includes the adapted FFmpeg, Moonlight common C, Mbed TLS, and Opus inputs.

See the [build guide](docs/BUILD.md) for PowerShell commands and the [dependency inventory](docs/DEPENDENCIES.md) for provenance and licensing.

## Limitations

- Streaming performance depends on host encoding, the network, and the console's H.264 decoding load. 1080p is experimental. HEVC, AV1, and HDR are unsupported.
- CRT Retro requests 640 × 240 and requires a compatible external **t240.xex** plugin and 480i/60 Hz console output. The CRT profile requests 640 × 480 and preserves the console output mode.
- Release notes record the validation performed for each published executable.

## Credits & license

MoonX360 builds on [Xenonlight](https://github.com/roderick-neuhoff/Xenonlight) and [Moonlight](https://moonlight-stream.org/). The application is licensed under [GPLv3](LICENSE). See the [third-party notices](THIRD_PARTY_NOTICES.md) for dependency licenses and attribution.

[Project logo](docs/images/logo.png) · [Icon](docs/images/icon.png) · [Social preview](docs/images/social-preview.png)
