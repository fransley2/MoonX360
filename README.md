# MoonX360

**[Moonlight](https://github.com/moonlight-stream) game streaming for the Xbox 360.** Play games from a compatible PC streaming server on an RGH/JTAG Xbox 360, with a controller-friendly interface, box art, PIN pairing and on-console statistics.

MoonX360 is derived from **[Xenonlight by roderick-neuhoff](https://github.com/roderick-neuhoff/Xenonlight)**. It builds on the original application, platform code and third-party libraries, with MoonX360 branding, a Home / Library / Stream / Settings interface, saved stream profiles and additional presentation, input and diagnostic work.

> **Status: experimental.** The dashboard prerelease builds successfully and has passed desktop regression checks. This published snapshot has not been tested on a physical Xbox 360 during release preparation.
>
> Upstream Xenonlight reports pairing and 720p30 streaming with Sunshine on real hardware. That is an upstream report, not a verified performance result for this MoonX360 build. Other server and encoder combinations remain unverified here.

## Two versions

| | Dashboard version (`default.xex`) | Legacy XeLL version (`xenon.elf`) |
| --- | --- | --- |
| Starts from | Aurora / FSD / XeXMenu | XeLL, taking over the console |
| Network backend | System network stack; Ethernet and configured Wi-Fi | libxenon; wired Ethernet |
| Controllers | XInput; wired and wireless controllers | libxenon USB support; use a wired controller |
| Settings and pairing | `moonlight` folder next to the XEX | `moonlight` folder on a mounted FAT device; deterministic console identity |
| Built with | External Xbox 360 XDK | Open-source libxenon toolchain |
| Current distribution | Experimental dashboard prerelease | Source only; not built or tested for this release |

Both share application code in `src/app.c`, `src/ui/` and `src/gamestream.c`, with different platform backends and dependency versions. **Use the dashboard version for the current release.** The table describes the implemented backends; it is not a hardware compatibility matrix.

## Download

Get the dashboard package from **[Releases](https://github.com/fransley2/MoonX360/releases)**:

1. Download and extract `MoonX360-v0.2.0-alpha.1-dashboard.zip`.
2. Copy `default.xex` to a writable folder on your console HDD or supported USB storage, such as `MoonX360\default.xex`.
3. Launch it from Aurora, FSD or XeXMenu. Your console must allow unsigned homebrew, normally through RGH/JTAG and a suitable Dashlaunch setup.
4. When updating, keep the existing `moonlight` folder so settings and pairing survive.

The release also includes matching source, license notices and SHA-256 checksums. Fonts, branding and dashboard shaders are embedded; no separate runtime asset download is required.

## Requirements

- A homebrew-capable Xbox 360, a controller and writable storage.
- A PC running a compatible GameStream-protocol server, with H.264 encoding, screen capture, configured applications and remote input support.
- A reachable LAN connection between the PC and console. Ethernet is the starting point for setup and troubleshooting.

[Sunshine](https://github.com/LizardByte/Sunshine) is the setup example used in the detailed guide. No server-version compatibility matrix is available for this build, and Apollo compatibility has not been verified. MoonX360 streams from a PC; it does not connect directly to Xbox Cloud Gaming.

## Using it

1. **Choose a PC:** press **Y** on **Home**, or open **Settings → PCs and connection → Saved and discovered PCs**. Select a discovered PC, or choose **+ Add host manually** to enter its IPv4 address.
2. **Pair:** open **Host actions → Pair with host**. Enter the PIN shown by the Xbox in your server's pairing interface, then check that the host is marked **Paired**.
3. **Play:** open **Library**, choose a game or application and press **A** to launch or resume it. **Home** also offers recent items and **Continue** when available.
4. **Set up the stream:** open **Stream** for **Stream Profile**, **Stream adjustments**, **Audio** and **Saved profiles**.
5. **Disconnect:** hold **LB + RB + both stick clicks** for about two seconds. **Settings → Controllers and mouse → Disconnect shortcut** can select **Back + Start + LB + RB** instead. Disconnecting can leave the game running on the PC; **Host actions → Stop app** explicitly stops it.
6. **Use the controller as a mouse:** during dashboard streaming, press **Back + right-stick click**. The left stick moves the pointer, the right stick scrolls, and **A/B** are left/right click. Press the shortcut again to return to gamepad input.
7. **Open the quick menu:** press **Back + left-stick click** for stream actions, volume/mute, mouse mode and statistics. Choose **Compact** or **Sidebar** under **Settings → Appearance and display → Streaming interface**.
8. **Wake the PC:** use **Host actions → Wake PC** after configuring **Host MAC** and Wake-on-LAN in the PC's firmware, network adapter and power settings. Actual wake behavior depends on the PC and network.

The dashboard code also includes keyboard forwarding and optional **5.1** output under **Stream → Audio → Xbox audio output**. These require compatible host, device and output configurations; this release has no physical-device validation for them.

See **[SETUP.md](docs/SETUP.md)** for network setup, pairing, controls, saving and troubleshooting.

## Settings and performance

Video is decoded in software on the **Xenon CPU**, using FFmpeg and selected VMX CPU routines. The **Xenos GPU converts, scales and presents the decoded image**. It does not decode H.264.

- **Fresh defaults:** Custom, 1280×720 at 60 FPS, 30 Mbps, Fast Decode on and V-Sync on. These are requested stream settings, not guaranteed decoded or presented frame rates. Existing saved configurations retain their values.
- **Fast decode:** skips H.264 deblocking to reduce CPU work, at the cost of more visible block edges. With it off, overload recovery can still temporarily reduce filtering on non-reference frames.
- **Image quality:** GPU image processing is separate from Fast Decode. **Auto Quality** can try the lighter **Fast** mode after submission-time measurements; **Quality** retains smoothing.
- **V-Sync:** synchronizes presentation with the display, trading tearing against latency.
- **Resolution, FPS and bitrate:** increasing them can increase network and decoding work. If playback struggles, reduce one setting at a time and compare the on-console measurements.

Factory profiles request the following values:

| Stream Profile | Resolution | FPS | Mbps |
| --- | --- | ---: | ---: |
| Performance | 854×480 | 60 | 25 |
| Standard | 1280×720 | 30 | 12 |
| Smooth | 1280×720 | 60 | 18 |
| 1080p Smooth | 1920×1080 | 60 | 20 |
| Maximum Quality | 1280×720 | 60 | 35 |
| Custom | Manual | Manual | Manual |

Performance, Smooth and 1080p Smooth use effective Fast Decode and the Fast GPU image policy. Preset names describe configurations, not measured stability or universal recommendations. Some existing in-app help still describes Standard/30 FPS as the fresh default; the current defaults are listed above.

**Saved profiles:** open **Stream → Saved profiles → Save current configuration**. Select the resulting profile to **Apply**, **Rename** or **Delete** it. Up to eight named profiles are supported. They save stream, decoder, image and audio options; host, language, calibration and diagnostics stay global. Per-game profile bindings are available from application details.

**Statistics:** open **Settings → History and performance → On-screen statistics** and choose **Off**, **Basic**, **Detailed** or **Developer**. Higher levels add decode budgets, queues, RTT, received/decoded/shown frame rates and pipeline timings. Statistics have overhead, and skipped displays do not by themselves establish packet loss.

The Xbox 360 has limited CPU, GPU and shared-memory resources. Performance depends on the host encoder, content, network and console configuration. **Stable 1080p60 and constant 60 FPS are not established for this release.** See [known limitations](docs/LIMITATIONS.md) and [validation details](docs/PUBLICATION.md).

## Settings and pairing storage

The dashboard version stores its data in **`game:\moonlight`**, next to the launched executable:

- `moonlight.ini`: preferences.
- `profiles-a.tsv` / `profiles-b.tsv`: saved stream profiles.
- `hosts.tsv`: saved PCs and host metadata.
- `sessions.tsv`, `favorites.tsv`, `game-bindings.tsv`: history, favorites and per-game profiles.
- `client.pem` / `key.pem`: private pairing identity.

Settings are saved when leaving settings; check any save-error message. Keep this folder when updating and keep identity files private. Named profiles use alternating validated files; other state uses verified replacement where implemented, but power-loss atomicity on FATX has not been established.

## Building

### Dashboard version

Requires Windows, Python 3 and a separately configured Xbox 360 XDK. The build targets SDK 21256.3 with its Jul12 compiler. Redistributable adapted dependency sources are included; **Microsoft SDK tools, headers and libraries are not**.

From the repository root:

```powershell
$env:XEDK = 'C:\path\to\external\Xbox360SDK'
python xdk/scripts/build_ffmpeg.py
python xdk/build.py
```

Output: **`xdk/build/default.xex`**. The default build policy is `aggressive`; the name is not a measured performance claim. See **[BUILD.md](docs/BUILD.md)** for tool versions, isolated build policies and host-test commands.

### Legacy XeLL version

Requires Linux/WSL and an external libxenon cross toolchain. Once the toolchain is installed:

```sh
bash scripts/build-deps.sh
make
```

Output: **`xenon.elf`**. Copy it to the root of a FAT32 USB device and boot XeLL. This path uses different dependencies and includes unpinned inputs; it was not built or console-tested for the current release. Full toolchain instructions and these limits are in [BUILD.md](docs/BUILD.md#legacy-xelllibxenon-backend).

## How it works

| Piece | Source | What it does |
| --- | --- | --- |
| Application and UI | `src/app.c`, `src/ui/` | Controller navigation, library, stream settings, profiles and shared drawing |
| Host protocol | `src/gamestream.c`, `src/http.c` | Server information, PIN pairing, application lists and launch/stop requests |
| Discovery and saved PCs | `src/mdns.c`, `src/mdns_net.c`, `src/host_cache.c` | mDNS discovery and host-cache handling |
| Settings and profiles | `src/config.c`, `src/config.h` | Preferences, factory presets and validated named-profile storage |
| Dashboard H.264 decoder | `xdk/src/video_xdk.cpp`, `xdk/ffmpeg_xbox/` | CPU decoding, slice workers, VMX routines and measurements |
| Dashboard graphics | `xdk/src/gfx_xdk.cpp`, `gfx_ui.cpp`, `video_cached.cpp` | GPU conversion/scaling, UI rendering and fenced video-texture slots |
| Dashboard audio and input | `xdk/src/audio_xdk.cpp`, `input_xdk.cpp` | Opus/XAudio2 output, XInput, remote input and quick-menu handling |
| XeLL compatibility | `compat/`, `hwpthread/` | libxenon socket/thread adapters and FFmpeg hardware-thread support |
| XeLL platform | `src/video.c`, `gpu.c`, `audio.c`, `input.c` | Legacy decoder, display, audio and controller backend |

The dashboard uses Direct3D, XAudio2, XInput and the console network stack. The XeLL backend uses libxenon and its own compatibility layers. Platform-specific thread layouts and behavior should not be assumed interchangeable.

## Known limitations

- H.264 video only; no implemented HEVC/AV1 or GPU H.264 decoder.
- No direct Xbox Cloud Gaming access; a compatible PC host is required.
- No verified server-version, encoder or device compatibility matrix for this snapshot.
- Wake-on-LAN, multicast discovery, Wi-Fi, 5.1, remote input, Guide transitions and long-session recovery need testing on the actual console and LAN.
- Renaming or deleting a named profile requires reassigning affected per-game bindings.
- Crash checkpoints are not backtraces; sudden failures can lose RAM logs.
- Legacy tools, old dependencies and incomplete dependency Git provenance remain maintenance constraints.

For connection failures, black screens, stutter, audio or controller problems, start with [Troubleshooting](docs/SETUP.md#troubleshooting). Report issues with the build, server/encoder, requested settings and observed behavior; redact personal addresses, logs and pairing data before sharing.

## License and credits

**GPLv3**, with existing copyright and third-party notices preserved; see [LICENSE](LICENSE) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

MoonX360 is a derivative of **Xenonlight**, not an independent rewrite. `src/gamestream.c` is ported from **moonlight-embedded's libgamestream**. The legacy shaders in `src/gpu.c` come from **Ced2911's MPlayer port for libxenon (GPLv2+)**. Inter is under the **SIL Open Font License**, and stb offers **MIT/public-domain alternatives**. Moonlight, FFmpeg, mbedTLS, Opus, ENet and nanors retain their respective licenses and notices.

For development, see [BUILD.md](docs/BUILD.md), [DEPENDENCIES.md](docs/DEPENDENCIES.md) and [CONTRIBUTING.md](CONTRIBUTING.md).
