# Aurora shortcut handoff: Xbox diagnostic run

The console has shown a black screen after Aurora starts a generated shortcut. The build and PC tests cannot identify whether the launcher failed or the principal MoonX360 XEX started and failed during Direct Launch. This run gathers that evidence before changing the handoff.

## Outputs

- `xdk/build/MoonX360/default.xex`: principal XEX with entry and Direct Launch stage log.
- `xdk/build/MoonX360Shortcut/default.xex`: normal launcher with persistent stage log.
- `xdk/build/MoonX360Shortcut-diagnostic/default.xex`: isolated diagnostic launcher, same launch flow and extra build marker.
- `xdk/build/MoonX360Shortcut-main_only/default.xex`: test launcher that validates the link and main XEX, then calls `XLaunchNewImage(mainPath, 0)` without `XSetLaunchData`. Never deploy this as the normal template.

The launcher writes `game:\moonx360-shortcut.log` beside the generated shortcut's `default.xex`. The principal XEX writes `game:\moonlight\direct-launch.log` in its own installation. Both files are truncated at entry and stages are flushed before a launch or error. The launcher also emits the same stages through `OutputDebugStringA`.

## Hardware sequence

1. Back up the installed main `default.xex`, its `MoonX360Shortcut\default.xex` template and one generated shortcut's `default.xex`. Preserve `moonlight` settings, pairing files, `moonx360.link` and the Aurora database.
2. Copy the new principal `xdk/build/MoonX360/default.xex` and normal template `xdk/build/MoonX360/MoonX360Shortcut/default.xex` to the existing installation. Copy `xdk/build/MoonX360Shortcut-diagnostic/default.xex` over **one generated shortcut** `default.xex`; leave its `moonx360.link` unchanged.
3. Delete or move that shortcut's old `moonx360-shortcut.log` and the main installation's old `moonlight\direct-launch.log`. Launch the shortcut from Aurora once. Before opening MoonX360 normally, collect both logs with Aurora File Manager or FTP. A normal launch would overwrite `direct-launch.log` and spoil the distinction.
4. If the shortcut log ends before `[16] BEFORE XLaunchNewImage`, use its `[FATAL] stage`, OS error, mount report and path. If it reaches `[16]` and no fresh main log exists, investigate the XDK title transition. If the main log exists, use the last `[MAIN]` stage to investigate startup or Direct Launch.
5. If the diagnostic run is inconclusive, replace only that generated shortcut's `default.xex` with `xdk/build/MoonX360Shortcut-main_only/default.xex`, remove both old logs and run it once. A normal MoonX360 Home means the path and `XLaunchNewImage` work without launch data. Failure there points to mount/path/title launch. Restore the normal shortcut XEX after the test.

No launch credentials are written to either log. The launcher logs the MX36 host UUID, App ID, profile token, display name and XEX path only after validation. The raw record size, magic, version and checksum comparison are logged before validation. The XDK declares `XSetLaunchData` as `DWORD WINAPI`, and `XLaunchNewImage` as `DECLSPEC_NORETURN VOID WINAPI`; the latter has no return status to record.

## Mount and Aurora detection audit

The shortcut first probes the saved drive alias, then creates a known `\??\` symbolic link only if the alias is inaccessible. It does not delete or replace existing aliases. It tests root accessibility again after `ObCreateSymbolicLink`, even on a conflict, and then tests the exact main XEX using `GetFileAttributesA`, `fopen`, size and `XEX2` header. The log includes the *expected* NT device path derived from the drive name; it does not query an existing alias's actual target. Launch still uses the saved drive path. Known mappings are `Hdd1:` to `\Device\Harddisk0\Partition1` and `UsbN:` to `\Device\MassN`.

Aurora auto-detection is separate from Direct Launch. Previously it checked only `<accessible root>:\Aurora\Aurora.xex`, used the XEX filename without opening it and did not search `Apps` or nested dashboard folders. It now checks `Aurora.xex` under `Aurora`, `Apps\Aurora` and `Homebrew\Aurora` for every accessible mount, then searches at most 320 folders to depth 4. The search is cancelable with B, skips known unrelated directories and requires a readable `XEX2` header. Each folder enumerates at most 192 candidate subdirectories; reaching that limit no longer discards the whole folder. A match is labeled a **candidate** because no stronger Aurora identity check is available. The setting stores the parent directory. Browse can select that directory or `Aurora.xex` directly; neither path is used to read the shortcut or start MoonX360.

On hardware, open **Advanced → Aurora Integration → Aurora Path → Auto-detect**. Confirm the found directory, or use Browse / Select Aurora.xex. Check the `Aurora Path Status` row. The detected paths and black-screen stage remain pending console validation.
