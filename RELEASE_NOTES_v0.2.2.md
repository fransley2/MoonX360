# MoonX360 v0.2.2

## What's new

- Stream Profile now provides a clearer path to save, apply, edit, rename, and delete named profiles, with factory presets kept separate.
- Host discovery and reconnection better handle saved PC identity, changed addresses, and stale discovery results.
- Connection attempts can be cancelled while the client is probing, authenticating, or negotiating a stream.
- Home and Library improve cover loading and caching, recent-game access, and launch status.
- Session cleanup and Stop App handling are more reliable across reconnects and repeated launches.

## Fixes

- Fixed deleting a saved profile incorrectly reporting a storage failure on Xbox 360.
- Fixed Stop App failing with “Connection cancelled” after reconnecting or launching games repeatedly.
- Fixed discovery cancellation state carrying over into later host-control operations.
- Prevented callbacks from an older stream session from changing a newer session.
- Improved recovery when saving or changing profiles is interrupted.

## Recommended settings

- **Smooth** is the default: 1280×720, 60 FPS requested, 12 Mbps.
- **1080p (Experimental)** requests 1920×1080, 24 FPS, 8 Mbps. Xbox 360 decoding limits may cause poor performance.
- Lower resolution or frame rate if a stream stutters. Requested values do not guarantee the rate the console can decode or present.

## Known limitations

- Xbox 360 decodes H.264 on the CPU; HEVC, AV1, and HDR are not supported.
- The release build record does not claim physical-console testing. Live Sunshine, Apollo, and Vibepollo host compatibility has not been validated for this build; fixture coverage is not live-host validation.

## Files

- MoonX360-v0.2.2-dashboard.zip: production default.xex, English installation notes, and the applicable license notices.
- SHA256SUMS-v0.2.2.txt: checksums for the dashboard ZIP and XEX.
- GitHub also provides source archives for the tagged source.

The XEX is the normal Aurora/DashLaunch dashboard build. No ELF is included.
