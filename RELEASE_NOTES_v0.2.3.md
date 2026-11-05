# MoonX360 v0.2.3

## Changes since v0.2.2

- Added a **Custom 640×480 stream mode** and a 640×480 app render target for VGA and other 4:3 displays.
- Added an independent **Interface layout** option: 16:9 remains the default; 4:3 fits the menu, stream HUD, mouse indicator, and Quick Menu to a 4:3 canvas. Choose 640×480 app resolution and 4:3 layout together to fill a 4:3 screen. Restart MoonX360 after changing either display option.
- Corrected menu projection, clipping, and launch cover placement at 4:3. Stream video retains its source aspect ratio, so a 16:9 stream still has bars on a 4:3 target.
- Refined the native Home and Library interface: compact Home navigation, real recent sessions and running-app context, portrait artwork, and optional custom or focused-art wallpaper. Search remains available from Library/Y.
- Added further session diagnostics and recovery safeguards, including guarded session callbacks and a video-stall report for troubleshooting. These are observable behavior and diagnostic changes, not a measured FPS or ping gain.
- Included the optional local Aurora shortcut template. Its Direct Launch and metadata workflow still needs separate hardware validation.
- Reduced one repeated storage initialization check during app startup. This is a code path improvement; no measured cold-start speedup is claimed.
- Expanded local diagnostics and regression coverage for configuration, interface layout, stream viewport, and presentation paths.

These changes make low-resolution VGA setups easier to use than v0.2.2 while retaining the existing 16:9 default. The app still requests the selected stream resolution and FPS from the PC; actual playback depends on the host, network, and Xbox 360 H.264 decoder.

## Install

Download `MoonX360-v0.2.3.zip`, extract it, then copy the **MoonX360** folder to the Xbox HDD or USB drive. Launch `MoonX360/default.xex` with Aurora. Keep the existing `MoonX360/moonlight` folder when updating so settings, hosts, saved profiles, and pairing remain available. The package includes the optional shortcut XEX template in its required relative location and license notices. It contains no SDK, credentials, or pairing data.

## Validation and limits

The user reported successful Xbox testing of the 4:3 changes. The exact tested XEX hash and the final packaged XEX hash are recorded in `BUILD_INFO.json`; they are distinct if the release label was rebuilt. Targeted host fixtures and XEX packaging checks passed. No quantified startup, FPS, network ping, or input-latency improvement is claimed. Physical testing of the optional Aurora shortcut template and every host/profile combination is not claimed.

MoonX360 runs PC games through a compatible GameStream host. It does not improve games installed on the Xbox. Xbox 360 CPU decoding supports H.264 here; 1080p remains experimental.
