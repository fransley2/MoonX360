# MoonX360 CRT Host Companion (Prototype)

Clean-room Windows host companion utility for pairing **Sunshine / Apollo**, **RetroArch**, optional **SwitchRes** binaries, and **MoonX360 CRT Retro 2.0**.

> Status: **Experimental Host Prototype** — tested via automated unit tests (`test_crt_host_companion.py`) with mocked Win32 calls. All state-changing subcommands default to `--dry-run`.

## Why This Exists

Separates host display/emulator configuration from Xbox 360 output capabilities using an explicit 4-layer model:

1. **Game Resolution (`A`)**: Internal game/emulator resolution.
2. **Capture Resolution (`B`)**: Windows desktop / DXGI capture size (`Sunshine` continues to own DXGI Desktop Duplication + NVENC/AMF/x264 encoding).
3. **Stream Resolution (`C`)**: H.264 stream negotiated with MoonX360.
4. **Xbox Output Resolution (`D`)**: Physical console video mode (`640x480` standard CRT or `640x240` + `t240.xex`).

## Safety Features

- `--dry-run` is active unless `--apply` is explicitly passed.
- Always calls `ChangeDisplaySettingsExW(..., CDS_TEST)` before applying a mode.
- Does **not** pass `CDS_UPDATEREGISTRY`, so a console/PC reboot reverts temporary modes.
- Saves the prior display mode to `crt_host_snapshot.json` before switching and registers an `atexit` handler plus `restore` command.
- Does **not** bundle any SwitchRes or third-party source code; `switchres` subcommand only invokes an externally installed user binary if present.

## Quick Usage

```powershell
# Inspect attached Win32 displays and available 240p modes
python projeto\tools\crt_host_companion\crt_host_companion.py inspect --filter-height 240

# Validate 640x240 mode and preview Sunshine prep-cmd (dry-run)
python projeto\tools\crt_host_companion\crt_host_companion.py switch --profile crt_retro_640x240
python projeto\tools\crt_host_companion\crt_host_companion.py sunshine-prep --profile crt_retro_640x240

# Run unit tests
python projeto\tools\crt_host_companion\test_crt_host_companion.py
```
