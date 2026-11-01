# MoonX360 v0.2.2

MoonX360 streams PC games to an Xbox 360 through a controller-first interface with host discovery, PIN pairing, a game library, saved stream profiles, and on-console diagnostics.

## Requirements

- An RGH/JTAG Xbox 360 with DashLaunch configured to run homebrew XEX files.
- A PC on the same reachable network running Sunshine or a compatible GameStream host.
- H.264 encoding enabled on the PC. Ethernet is a good starting point.

## Install and start

1. Download and extract `MoonX360-v0.2.2-dashboard.zip` from the GitHub release.
2. Copy `default.xex` to a writable folder on the Xbox 360 HDD or supported USB storage, such as `Hdd1:/Apps/MoonX360/default.xex`.
3. In Aurora, add or refresh that folder in the game list, then launch MoonX360.
4. Select a discovered PC on Home, or add its address from the Hosts screen.
5. Choose **Pair with host**. Enter the PIN shown by MoonX360 in the host's pairing page.
6. Open **Library**, select a game, and press **A** to start streaming.

Keep the `moonlight` data folder beside `default.xex` when updating; it stores settings, saved hosts, and pairing identity.

## What MoonX360 does

The game runs on your PC. The host streams its video and audio to MoonX360, and MoonX360 sends your controller input back to the PC. It turns the Xbox 360 into a streaming client; it does not enhance graphics or frame rate for games installed on the Xbox 360.

MoonX360 works with games and apps exposed by a compatible GameStream host, not just Steam. Add a game or Steam Big Picture to the host's app list, or launch the host's Desktop entry. MoonX360 does not include a Steam catalog.

The PC does the game's rendering, but the Xbox 360 decodes H.264 video on its CPU. Performance depends on the PC and encoder, game, network, and the console's decoding limits.

Version 0.2.2 is a dashboard `default.xex` launched through Aurora/DashLaunch; it does not include a XeLL/ELF build. During a stream, hold the default **LB + RB + L3 + R3** shortcut for about two seconds to disconnect and return to MoonX360, where you can choose another app.

MoonX360 includes GameStream compatibility handling for Sunshine, Apollo, and Vibepollo, including optional host capability and permission responses. This release was not validated against live instances of each host.

## Recommended settings

**Smooth** is the default profile: 1280×720, 60 FPS requested, and 12 Mbps. The Xbox 360 decodes H.264 on its CPU, so actual frame rate depends on the game, PC encoder, network, and console. Lower resolution or frame rate if playback stutters.

**1080p (Experimental)** requests 1920×1080 at 24 FPS and 8 Mbps. It may perform poorly on the Xbox 360 and is not a guaranteed 24 FPS mode.

## Known limitations

- H.264 decoding is CPU-bound on Xbox 360; HEVC, AV1, and HDR are not supported.
- 1080p is experimental. Sustained frame rate, latency, and stability vary by console and stream.
- This release has not been validated against a live Sunshine, Apollo, or Vibepollo host. Host compatibility paths are covered by source/fixture work; that does not establish live-host or console streaming compatibility.
- No physical-console performance claim is made for this build (`hardware_tested` is false in its build record).

## Build and licensing

The repository contains the dashboard build scripts and patches. Building requires a separately obtained Xbox 360 SDK; Microsoft SDK files are not included. See [xdk/README.md](xdk/README.md). MoonX360 is GPLv3; see [LICENSE](LICENSE) and [third-party notices](THIRD_PARTY_NOTICES.md).

## v0.2.2 changes

See [release notes](RELEASE_NOTES_v0.2.2.md).
