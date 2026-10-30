# MoonX360

MoonX360 is an experimental PC game-streaming client for homebrew-capable Xbox 360 consoles. It receives video, audio and application lists from a compatible GameStream-protocol PC server and sends controller input back to that PC. It does not connect directly to Xbox Cloud Gaming.

This project is derived from [Xenonlight by roderick-neuhoff](https://github.com/roderick-neuhoff/Xenonlight). It retains substantial upstream application and platform code and third-party libraries; a new name does not imply independent authorship. The original GPL v3 license and source notices are preserved. See [third-party notices](THIRD_PARTY_NOTICES.md).

The current source includes a controller-oriented Home / Library / Stream / Settings interface, MoonX360 branding, automatic host discovery and a saved-host cache, named stream profiles, per-game profile bindings, favorites/history, configurable performance statistics, and CPU/VMX decoder and GPU presentation work. Wake-on-LAN, controller mouse mode, remote keyboard handling and optional 5.1 audio are present in the dashboard implementation. Feature presence is not a claim of complete physical-console validation.

## Requirements

- An Xbox 360 able to run unsigned homebrew (normally RGH/JTAG with a suitable dashboard/Dashlaunch setup), an Xbox 360 controller and writable storage.
- A PC with a running GameStream-compatible streaming server, H.264 encoding, available games/applications and permission for capture and remote input.
- A reachable local network between the PC and console. Ethernet is the starting point for troubleshooting. The dashboard backend uses the console network stack, including configured Wi-Fi; the legacy XeLL backend uses wired networking.

Sunshine is the primary setup example. Upstream reports hardware pairing and 720p30 operation with Sunshine; this is an upstream report, not verification of this MoonX360 snapshot or a server-version compatibility matrix. No Apollo compatibility verification is supplied. See [setup](docs/SETUP.md) and [limitations](docs/LIMITATIONS.md).

## Install and run

Download the dashboard package from [Releases](https://github.com/fransley2/MoonX360/releases), or build it using [BUILD.md](docs/BUILD.md). Extract the package and copy `default.xex` to a writable folder such as `MoonX360/default.xex` on the console HDD or supported USB storage. Launch it from Aurora, FSD or XeXMenu. Keep the existing `moonlight` data folder when updating. Experimental prereleases are not proof of physical-console stability. Executables remain outside the source Git tree.

The fonts, branding and video/UI shaders used by the dashboard build are embedded from the supplied assets/source. Do not remove source assets when building. The runtime creates its own settings and pairing data; never add that folder to Git.

Open **Settings → PCs and connection** to select a discovered PC or use **+ Add host manually**. Pair using the displayed PIN, open **Library**, and select an application. Use **Stream** to choose streaming settings. Detailed controls, profile handling and troubleshooting are in [SETUP.md](docs/SETUP.md).

## Status and performance

H.264 decoding runs on the Xenon CPU using FFmpeg, including VMX CPU routines. Xenos handles video conversion, scaling and image processing; it does not decode H.264. The Xbox 360 has tight CPU, GPU and shared-memory constraints and depends on old platform tools and libraries.

Fresh defaults request **1280×720 at 60 FPS, 30 Mbps**, with **Fast Decode** enabled. These are requested stream settings, not measured decoded or presented performance. Preset names such as **1080p Smooth** are configuration names, not stability promises. There is no claim of stable 1080p60 or constant 60 FPS. Performance varies with the host encoder, image content, bitrate, network and console configuration. Higher bitrate can increase network and decoding load.

Fast Decode skips H.264 deblocking, trading cleaner edges for less CPU work and potentially more visible blocking. GPU image processing is a separate setting. See [limitations and known discrepancies](docs/LIMITATIONS.md), including stale in-app help text and unvalidated hardware combinations.

## Development

- [Build and test commands](docs/BUILD.md)
- [Dependency versions, source inventory and adaptations](docs/DEPENDENCIES.md)
- [Source publication scope and validation](docs/PUBLICATION.md)
- [Contributing](CONTRIBUTING.md)

The dashboard XEX and legacy XeLL/libxenon paths share application code but use different platform backends and dependency versions. Microsoft XDK tools, headers and libraries are not distributed. Obtain and configure them separately when building the dashboard version.
