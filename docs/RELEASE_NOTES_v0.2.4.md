# MoonX360 v0.2.4

MoonX360 streams games and apps from a compatible PC host to an Xbox 360. This release brings the current native dashboard, streaming controls and recovery work together in one package.

## What's new

- Home, Library, Computers and Settings navigation, with recent games, favorites and cached host artwork.
- A controller-operated wallpaper browser for PNG and JPEG images. The supplied `home-wallpaper.png` is included in `Wallpapers/`.
- A revised Quick Menu: press **Back + L3** during a stream to resume, request a picture refresh, adjust volume, toggle mouse mode or performance information, view session details, disconnect, or close the host app with confirmation.
- Switch to another host app through the existing library flow, with cancellation, pairing and host-state checks.
- Aurora direct game shortcuts, a launcher template, metadata/artwork support and optional MoonX360Sync/MoonX360Probe utilities.
- Video refresh/recovery controls, optional refresh smoothing, wireless/CRT options and expanded session diagnostics. These options do not establish a measured performance improvement.
- Custom 640x480 streaming and independent 4:3 interface/output support remain available.

## Corrections and compatibility

- Improved session cleanup, stale callback isolation, reconnect and app-switch handling.
- Bounded video recovery and decoder diagnostics, with checks for damaged pictures and stalls.
- Improved saved-profile compatibility, artwork cache handling, storage recovery and controller navigation.
- Fast Decode is **On/Off** in this release. On retains the legacy FAST/deblocking tradeoff; Off favors accurate reconstruction. Format-5 value 2 loads as On. Changes apply on the next connection. Smooth/new-install defaults select On. This release does not ship the earlier three-mode stabilization experiment.
- Profiles are saved in checksummed format 5; older formats remain readable. Back up `moonlight` before updating. Older binaries may require the backed-up profiles when rolling back.

## Installation and Wallpaper

1. Back up the existing XEX and `moonlight` folder.
2. Extract `MoonX360-v0.2.4.zip`. Copy the contents of its `MoonX360/` folder to the writable MoonX360 installation on your console. Preserve your existing `moonlight` data.
3. Launch `default.xex` from Aurora, FSD or XeXMenu.
4. Open **Settings > Interface**, open **Customize Home** (or press **X**), then choose **Choose wallpaper**. Select `Wallpapers/home-wallpaper.png` from the installed folder. The app saves its own copy and selects Custom wallpaper.
5. Alternatively, copy the supplied image to `moonlight/home-wallpaper.png` and select **Custom wallpaper**. Do not replace your configuration or pairing files.

PNG/JPEG images must be at most 2048x2048 and 8 MiB. The supplied image is 1280x720. Keep the launcher template in `MoonX360Shortcut/default.xex`; it is separate from the main app. See `docs/AURORA_DIRECT_LAUNCH.md` for shortcut setup.

## Validation and limitations

Desktop regression suites, XDK compilation, XEX imports/title resources, package integrity and SHA-256 checks are recorded in `BUILD_INFO.json` and `docs/VALIDATION.json`. The exact packaged XEX has **not been physically validated on Xbox 360**. Live host, controller, GPU/audio, network and endurance testing remain pending.

H.264 decoding runs on the Xbox 360 CPU; GPU processing handles image conversion/scaling/presentation. HEVC, AV1 and HDR are unsupported. 1080p, refresh smoothing and image-recovery options remain experimental. Requested FPS does not guarantee achieved FPS. No claim is made that this release eliminates the reported corruption or stutter.

MoonX360 builds on [Xenonlight](https://github.com/roderick-neuhoff/Xenonlight) and Moonlight technology. GPL and third-party notices accompany the package and source. Microsoft SDK components are not included.
