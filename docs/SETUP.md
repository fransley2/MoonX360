# Set up MoonX360

1. **Prepare your PC:** Install [Apollo](https://github.com/ClassicOldSong/Apollo) or [Sunshine](https://github.com/LizardByte/Sunshine), enable **H.264** streaming, and install **ViGEmBus** on Windows if prompted for virtual Xbox 360 controller support.
2. **Install MoonX360:** Download the latest package from [GitHub Releases](https://github.com/fransley2/MoonX360/releases), verify `SHA256SUMS.txt`, extract the `MoonX360` folder (`default.xex` plus optional `MoonX360Shortcut\` and `AuroraScripts\`) to `Hdd1:\Apps\MoonX360\` or USB storage, and launch `default.xex` via Aurora, FSD, or XeXMenu.
3. **Pair and play:** Open **Computers**, select your discovered PC (or add its LAN IP manually), choose **Pair with host**, enter the 4-digit PIN on the PC host Web UI, then open **Library** and press **A** to stream.

**Updating:** Keep your existing `moonlight\` folder next to `default.xex` to preserve saved hosts (`hosts.tsv`), settings (`moonlight.ini`), profiles, and pairing certificates (`client.pem` / `key.pem`). Never share `moonlight\` or pairing keys publicly.

See also: [Controls](CONTROLS.md) · [CRT Setup](CRT.md) · [Troubleshooting](TROUBLESHOOTING.md) · [Aurora Shortcuts](AURORA_DIRECT_LAUNCH.md) · [Build from Source](BUILD.md)
