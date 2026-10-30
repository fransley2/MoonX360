# MoonX360

**Your PC games. Your Xbox 360. Your controller.**

MoonX360 brings PC game streaming to your Xbox 360 with an interface built for the couch. Browse your library with box art, jump back into recent games, and keep your favorite streaming setups ready for the next session—all from your controller.

Runs as **`default.xex`** from Aurora, FSD or XeXMenu on a homebrew-capable Xbox 360.

**[Download MoonX360](https://github.com/fransley2/MoonX360/releases)** · **[Setup guide](docs/SETUP.md)** · **[Report an issue](https://github.com/fransley2/MoonX360/issues)**

## Built around how you play

- **A controller-first experience.** Home, Library, Stream and Settings put your games and session options within easy reach, without needing a mouse to navigate the app.
- **Your library, organized.** Box art, favorites, recent games and Continue help you get back to what you were playing.
- **Profiles that fit your games.** Choose a factory preset or save up to eight named configurations. Apply different profiles to individual games instead of adjusting everything before each session.
- **Quick access while playing.** Open the compact or sidebar quick menu for volume, mute, mouse mode and statistics.
- **Easier PC management.** Discover hosts automatically, add one by IP, keep multiple PCs saved, pair by PIN and send Wake-on-LAN requests to a configured host.
- **More ways to control the session.** Use your gamepad normally or switch it to mouse mode. Keyboard forwarding, stick calibration and configurable disconnect shortcuts are also included.
- **Useful performance feedback.** Choose Basic, Detailed or Developer statistics to see frame rates, decode time, bitrate and queue information on the console.
- **Make it yours.** Pick backgrounds, accent colors, focus animation and interface styles. Menus are available in English, Spanish and Brazilian Portuguese.

## Get started

You need an RGH/JTAG Xbox 360 that can run unsigned homebrew, a controller, writable storage and a PC running a compatible GameStream-protocol server with H.264 encoding. Connect the PC and console to the same reachable network; Ethernet is a good starting point.

1. Download the dashboard ZIP from [Releases](https://github.com/fransley2/MoonX360/releases) and extract it.
2. Copy `default.xex` into a folder such as `MoonX360` on your console HDD or supported USB storage.
3. Launch it from Aurora, FSD or XeXMenu.
4. Press **Y** on Home to choose your PC, or select **+ Add host manually**.
5. Open **Host actions → Pair with host** and enter the displayed PIN in your PC server's pairing interface.
6. Open **Library**, choose a game and press **A**.

When updating, keep the existing `moonlight` folder next to the executable to preserve settings and pairing. See the [setup guide](docs/SETUP.md) for server configuration, audio and troubleshooting.

## Set it once. Keep playing.

The **Stream** tab brings together resolution, FPS, bitrate, image quality, audio and profiles.

Use **Saved profiles → Save current configuration** to keep a setup, then **Apply**, **Rename** or **Delete** it whenever needed. Per-game bindings let you choose a configuration for each title while keeping your global preferences separate.

**Fast decode** reduces CPU work by skipping H.264 deblocking, trading some image cleanliness for a lighter decoding workload. **Image quality** controls GPU processing separately. Stereo and optional 5.1 output are available, along with simultaneous audio playback on the PC.

## Handy shortcuts

| Action | Shortcut during streaming |
| --- | --- |
| Open the quick menu | **Back + left-stick click** |
| Toggle controller mouse mode | **Back + right-stick click** |
| Disconnect with the default shortcut | Hold **LB + RB + both stick clicks** for about two seconds |

In mouse mode, the left stick moves the pointer, the right stick scrolls, and **A/B** act as left/right click. You can change the disconnect shortcut under **Settings → Controllers and mouse**.

## Project status

**Experimental prerelease.** The current release passed build and desktop regression checks; physical-console validation remains pending for this published build.

Fresh defaults request **720p60 at 30 Mbps**. Actual performance depends on your PC encoder, game content, network and console settings. H.264 decoding runs on the CPU; the GPU handles image conversion, scaling and presentation. See [limitations and compatibility notes](docs/LIMITATIONS.md) before choosing your setup.

## Development

Want to build or contribute? Start with the [build instructions](docs/BUILD.md), [dependencies](docs/DEPENDENCIES.md) and [contribution guide](CONTRIBUTING.md). The Xbox 360 XDK must be configured externally and is not included.

## Credits and license

Derived from [Xenonlight](https://github.com/roderick-neuhoff/Xenonlight). Licensed under [GPLv3](LICENSE); third-party credits and licenses are preserved in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
