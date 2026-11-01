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
