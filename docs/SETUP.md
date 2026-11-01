# Setup

MoonX360 runs as a dashboard XEX on an RGH/JTAG Xbox 360 with DashLaunch configured for homebrew. Connect the console and PC to the same reachable network. On the PC, run Sunshine or another compatible GameStream host with H.264 encoding and the desired games configured.

## Install on the Xbox 360

1. Extract the dashboard ZIP from the GitHub release.
2. Copy default.xex to a writable folder on the console HDD or supported USB storage, for example Hdd1:\Apps\MoonX360\default.xex.
3. Refresh the game list in Aurora and launch MoonX360.
4. Keep the moonlight data folder next to default.xex during updates to preserve settings, saved hosts, and pairing identity.

## Find and pair a PC

MoonX360 can discover GameStream hosts on the local network. If the PC does not appear, add its current IP address manually from the Hosts screen. Select the PC, open Host actions, and choose Pair with host. Enter the displayed PIN in the host's pairing interface.

Open Library, select a configured game, and press A to start. Use the on-screen controls to return to Home or stop a stream.

## Starting settings

Smooth is the default: 1280×720, 60 FPS requested, 12 Mbps. If the Xbox 360 cannot decode a game smoothly, try a lower resolution or frame rate. The 1080p (Experimental) preset requests 1920×1080 at 24 FPS and 8 Mbps; it may perform poorly.

Keep the host and console on the same reachable LAN, and allow the host's documented GameStream ports through the PC firewall. Do not expose the service to the public Internet for local play.

For build instructions and SDK requirements, see [BUILD.md](BUILD.md). For current platform constraints, see [LIMITATIONS.md](LIMITATIONS.md).
