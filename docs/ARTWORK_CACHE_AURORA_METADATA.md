# MoonX360 artwork and Aurora shortcut metadata

## Identity and formats

- The Direct Launch executable remains `4D583353`, and `moonx360.link` remains the exact 468-byte v1 protocol. Existing shortcuts keep launching without migration.
- `moonx360.meta` is an optional, 504-byte v1 sidecar in each shortcut folder. It has a separate display title, description, source, optional manually selected `metadataTitleId`, host UUID, App ID and FNV checksum. A missing or invalid sidecar falls back to the v1 link title. It contains no pairing data.
- `metadataTitleId` never changes the executable Title ID. It defaults to zero and is accepted only from the user's manual eight-digit hex entry. MoonX360 does not guess commercial Title IDs by name.
- The offline app catalog and image cache use the saved host UUID and Sunshine App ID. A changed App ID gets a new image key, even when an optional app UUID or display name stays the same.

## MoonX360 cache

- The existing async artwork worker serves the twelve-entry RAM cache first, then reads validated native ARGB thumbnails from `moonlight/cache/artwork/`, then fetches Sunshine artwork. It does not wait for the network to draw a cached image. The app catalog persists the app list so a saved UUID host can show its library with the PC offline.
- Native thumbnails remain in their existing compressed `XCVR003` format; no image conversion was introduced. Each file header carries host/App ID, dimensions, size, checksum, save time and access serial. Earlier flat `cover-00.bin` through `cover-31.bin` files migrate on first cache scan.
- New files are staged, flushed, read back and checked before promotion. An existing cover moves to a same-folder backup and is restored if promotion fails. Startup recovers interrupted replacements. A failed refresh keeps the old decoded image and logs `[Artwork] refresh failed, keeping cached artwork`.
- The disk cache has 256 slots and defaults to 128 MiB. Advanced > Artwork Cache offers on/off, 32/64/128 MiB, refresh, confirmed clear and diagnostics. Clear touches only the artwork cache slots; it leaves the offline app catalog, settings, pairing, profiles and shortcuts alone. Changing the size trims least recently used entries in the current session.
- With a cached catalog, opening Library does not resolve the PC first. Press Y to refresh the PC and app list. Cover refresh proceeds automatically after the host is online; offline cached covers remain visible.

## Aurora sync

- The Release install tree includes `MoonX360/AuroraScripts/MoonX360Sync/Main.lua` and `icon.png`. Set **Aurora Location**, then use **Settings > Advanced > Aurora Integration > Install Aurora Metadata Sync** to copy both files into the active Aurora installation's `User/Scripts/Utility/MoonX360Sync/`. For manual installation, copy both files there; do not leave them only under MoonX360. Refresh the Aurora Scripts list with X or restart Aurora, scan the Shortcuts folder, then run **MoonX360 Shortcut Metadata**. The utility reports scan paths, matching Title ID items, recognized links and titles updated; restart Aurora if the game list remains stale. It matches each `ContentItems` row by `ScanPathId`, mount and `Directory`, reads that folder's link/sidecar and calls `Content.SetTitle(item.Id, ...)` and `Content.SetDescription(item.Id, ...)`. It preserves an existing real Aurora description when MoonX360 only has generic fallback text.
- If the utility does not appear after refresh/restart, copy the separate `AuroraScripts/MoonX360Probe/` folder into the same `User/Scripts/Utility/` directory. The probe only shows `Script.GetBasePath()` when launched. If neither utility appears, confirm that the `Aurora.xex` in that installation is the one currently running and that `Main.lua` is not named `Main.lua.txt`.
- Each shortcut can edit its Aurora display title, description and optional metadata Title ID, or refresh its sidecar without recreating the XEX or launch link. Aurora Integration also offers Refresh All Aurora Metadata. Run the Lua utility again to apply changes to Aurora's database.
- [Aurora's documented API](https://github.com/XboxUnity/AuroraScripts/blob/master/README.md) gives `Content.SetAsset(imagePath, assetType, screenshotIndex)` no `contentId` parameter. This release does not call it or write RXEA/GameData assets because doing so without a verified per-item API and console trial could assign one cover to every `4D583353` shortcut. `metadataTitleId` is stored for an eventual confirmed lookup, not used for automatic network download. Individual Aurora artwork is pending.

## Console validation

1. Launch MoonX360 on an HDD installation, open a saved host library, let two covers download, close MoonX360, disconnect the PC/network, reopen Library and verify both covers appear from disk.
2. Restore the PC, change a cover at the host, open Library and verify old artwork appears first; press Y to refresh, then verify the replacement occurs only after a valid download. Interrupt a download and verify the old cover persists.
3. Create two shortcuts, scan Aurora, run MoonX360Sync and verify each ContentItem gets the right title/description. Confirm the shortcut XEX Title ID remains `4D583353` and Direct Launch still works.
4. Test a manual `metadataTitleId` and None. Verify neither changes the XEX identity or the metadata of a real installed game.

Host fixtures and Release XEX builds do not prove console storage timing or Aurora UI behavior.
