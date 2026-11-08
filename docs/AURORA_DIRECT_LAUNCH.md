# Aurora direct launch: existing flow and design record

Update, 2026-11-03: the user confirmed Direct Launch and multiple shortcuts work on physical hardware. The new per-shortcut Aurora metadata and persistent artwork changes are documented in [ARTWORK_CACHE_AURORA_METADATA.md](ARTWORK_CACHE_AURORA_METADATA.md); their Aurora presentation and cache behavior still require a separate console test. Older pending statements below describe the earlier development stage.

## Existing flow (inspected before implementation)

- The dashboard XEX enters `xdk/src/main_xdk.cpp::main`. It initializes storage, graphics, UI, networking and video, then calls `src/app.c::app_main`.
- `app_main` reads `game:\\moonlight\\moonlight.ini`, `hosts.tsv`, session/favorite/profile bindings, and the client identity; normal startup enters `main_menu`.
- Hosts use the saved Sunshine UUID (`host_uuid`/`cached_host.uuid`) as identity. `refresh_task` first probes the saved address, then performs bounded mDNS discovery and validates the UUID before accepting a host.
- The library obtains `/applist` through `gs_apps`. Application IDs are integers from Sunshine. `stream_session`/`stream_once` launch with `gs_launch(app_id)`, then `LiStartConnection`; the shared session teardown runs on every exit.
- Profiles are factory presets or named saved profiles in `profiles-a.tsv`/`profiles-b.tsv`; per-game bindings use `game-bindings.tsv` and `factory:`/`user:` tokens.
- Settings navigation comes from `src/settings_layout.h` and the menus in `src/app.c`. App details are drawn by `src/ui/ui.c::ui_draw_app_details`; controller B/Back is handled by `wait_button` and the existing stream quick menu.
- SDK storage is relative to the principal XEX's `game:` mapping. `xdk/build.py`, `xdk/xex.xml`, and `xdk/MoonX360.xlast` produce and label `default.xex`; the root Makefile is for XeLL.

## Aurora identity and cache boundary

XboxUnity's Aurora Database Cleaner script checks each indexed executable path and removes missing `ContentItems` rows. Aurora's assets use Title ID plus a database content ID. This supports a manual cleaner/rescan fallback, but does not establish a safe in-process API or prove that identical XEXs in separate directories receive separate visible entries. No MoonX360 code will modify Aurora's database or delete its global cache. Per-game visibility and any Title ID requirement remain hardware validation items.

Sources: [Aurora Database Cleaner](https://github.com/XboxUnity/AuroraScripts/blob/master/UtilityScripts/DBCleaner/Main.lua), [Aurora asset documentation](https://github.com/jrobiche/xbox360-aurora-developer-documentation/blob/main/docs/aurora-0.7b.2r1655-assets/README.md).

## Protocol and deployment

- `moonx360.link` is a 468-byte packed v1 record with `MX36` magic, exact size, Sunshine host UUID, positive App ID, optional profile token, display title, installed main XEX path, reserved zero flags and FNV-1a checksum. The launcher rejects malformed, unsupported and oversized files. It contains no pairing material.
- The small launcher has dedicated homebrew Title ID `4D583353`; the main MoonX360 keeps `4D583336`. Every generated launcher is byte-identical. Aurora's database stores both path and Title ID, but **separate visible entries for identical XEXs have not been tested on hardware**. The generic XEX metadata says `MoonX360 Shortcut`; use Aurora's title editor or the optional `xdk/aurora/MoonX360Sync/Main.lua` utility after scanning to apply link display names. The utility uses Aurora's documented `Content.SetTitle` API, only for the dedicated shortcut Title ID; its behavior is also pending Aurora hardware validation. It does not remove stale rows.
- Copy the contents of `xdk/build/MoonX360/` to the installed MoonX360 folder. It includes the principal XEX and `MoonX360Shortcut/default.xex` template. Preserve the installed `moonlight` storage folder and pairing files. The app copies this small template when creating each shortcut.
- In MoonX360, open **Settings → Advanced → Aurora Integration**, enable integration, verify **MoonX360 XEX Path** and **Shortcuts Folder**, then create a shortcut from a game detail or use Create All. Auto-detect handles common installations; Browse selects nonstandard locations with the controller. Keyboard path entry remains an advanced fallback.
- Add the Shortcuts folder as an Aurora scan path, with enough scan depth to include each game subfolder, then rescan. For optional title syncing, install `Main.lua` under the actual Aurora `User\Scripts\Utility\MoonX360Sync\` folder and run it after Aurora indexes the shortcuts. It only changes matching shortcut titles.
- A Debug app/XEX build uses `/Od`, `/Zi` and `/DEBUG` with the retail XDK graphics libraries. It defines `MOONX_DEBUG_BUILD` instead of `_DEBUG`, because the installed SDK's debug D3D library requires unresolved devkit-only PIX/debug exports. The existing third-party and FFmpeg libraries are reused for the isolated Debug link.

## Installation path selection (2026-11-03)

The reported `MoonX360 main XEX path is unavailable` came from `aurora_shortcut_create` testing the configured `Hdd1:\Apps\MoonX360\default.xex` with `GetFileAttributesA`. That is only a default; it need not identify the running installation. The repository has no copy of the console's saved `moonlight.ini`, so the exact saved path and mount state on that Xbox remain unverified. `game:\default.xex` can open the running XEX, but `game:` is remapped for the shortcut launcher and cannot be stored in `moonx360.link` as the destination.

On enable, the main app now checks the saved path, a sibling of the configured shortcuts folder, and a bounded set of common `Hdd1:` / `UsbN:` installation paths. It compares a candidate with `game:\default.xex` using file identity when available and file content as a FATX fallback. It stores the discovered device path and, when the shortcuts folder still has its factory default, derives a sibling `Shortcuts` folder. There is no deep disk scan. A nonstandard installation path is selected through the controller browser.

Open **Settings > Advanced > Aurora Integration**. Select **MoonX360 XEX Path** to Browse, Auto-detect, Edit manually, or Reset. **Shortcuts Folder** and **Aurora Location** use folder browsing; X selects the current folder, Y returns to roots, and Create Folder invokes the keyboard only for a new name. Only accessible mount roots appear. The XEX browser shows folders and `.xex` files, and accepts only the current installation's readable `default.xex` with an `XEX2` header. The status rows report whether the configured XEX can be opened and whether the shortcuts folder exists. A missing XEX during shortcut creation offers direct browsing and retry.

Console recheck: copy the updated Release package, preserve `game:\moonlight`, enable integration, confirm that the detected device path points to the actual installed XEX, browse manually if detection cannot prove it, create one shortcut, and launch it from Aurora. Repeat with a USB installation, a removed USB device, an invalid XEX and a missing shortcuts folder. PC fixtures and XDK builds do not establish those console outcomes.

### Device mounts for the controller browser

On the first Xbox trial, Browse displayed `No storage devices` even though MoonX360 could read `game:`. The browser only probed `Hdd1:`/`UsbN:` DOS aliases and did not create them in the title namespace. The XDK `xboxkrnl.lib` exports `ObCreateSymbolicLink`; the Xbox 360 homebrew [NXE2GOD mount implementation](https://raw.githubusercontent.com/Swizzy/XDK_Projects/master/NXE2GOD/mount.c) uses `\??\` aliases for the hard disk and USB devices. MoonX360 now creates only known missing aliases, never deletes or replaces an existing one, and shows a root only after an actual filesystem probe succeeds. The small Shortcut Launcher establishes its own mapping before opening the saved main-XEX path, since a title-local alias cannot be assumed to survive a title launch. `IntMu` and other unknown aliases remain visible only if the console already exposes them. This behavior still needs a console recheck with the updated XEXs.

## Xbox 360 validation checklist

1. **A:** In Aurora, select a named shortcut for Hollow Knight. Confirm it opens the small launcher, hands off to MoonX360 and streams that exact App ID without showing Home or fetching the app list/covers. Check Aurora listing identity and title handling.
2. **B:** Turn the saved host off. Confirm a bounded search, an error screen and A Home / B Dashboard controls.
3. **C:** Remove a saved profile referenced by a shortcut. Confirm the log says Smooth fallback and the stream uses the current Smooth preset.
4. **D:** Delete an indexed shortcut. Confirm only its `default.xex` and `moonx360.link` are removed, the index count falls, and unrelated files/folders stay.
5. **E:** If Aurora still lists the removed item, confirm MoonX360 reports the possible stale cache and use Aurora's Database Cleaner or rescan. No global database/cache deletion is performed.
6. **F:** Start the principal MoonX360 XEX normally. Confirm existing Home, library, pairing, profile, stream, Quick Menu and exit behavior remain intact.

Build checks and PC fixtures cannot establish any of these console results. The generic shortcut Title ID and optional Aurora title utility must be validated on the intended Aurora version before claiming the desired independent named library entries.
