# Aurora Direct Launch Shortcuts

MoonX360 can generate standalone Aurora entries for individual PC games so you can launch a streamed game directly from your Aurora cover grid.

---

## 1. Files Required on Your Xbox 360

When using the release ZIP (`MoonX360-v0.2.6-rc1.zip`), keep these files together in your MoonX360 folder (for example, `Hdd1:\Apps\MoonX360\`):

- `default.xex` — Main MoonX360 dashboard executable.
- `MoonX360Shortcut\default.xex` — Template shortcut launcher (Title ID `4D583353`).
- `AuroraScripts\MoonX360Sync\` (`Main.lua`, `icon.png`) — Optional Aurora script to sync shortcut titles and descriptions.
- `AuroraScripts\MoonX360Probe\` (`Main.lua`, `icon.png`) — Optional Aurora diagnostic probe script.

If building from source, run `python xdk\build.py shortcut_xex` in the same build environment as `default.xex` to generate `MoonX360Shortcut\default.xex`.

---

## 2. Creating & Using a Direct Shortcut

1. Open MoonX360, pair your host PC in **Computers**, and confirm the game appears in **Library**.
2. In **Settings → Advanced → Aurora Integration**, verify your **MoonX360 main XEX path** (default `Hdd1:\Apps\MoonX360\default.xex`) and **Shortcuts folder** (default `Hdd1:\Apps\MoonX360\Shortcuts\`).
3. Open **Library**, highlight a game, press **`X`** (**Game Details**), and select **Create Aurora shortcut**.
4. Add `Hdd1:\Apps\MoonX360\Shortcuts\` to Aurora's **Content Scan Paths** and scan.
5. (Optional) Copy `AuroraScripts\MoonX360Sync` into Aurora's `User\Scripts\Utility\` folder and run it to update Aurora titles and descriptions from `moonx360.meta`.

> [!NOTE]
> Keep `default.xex` and its `moonlight\` data folder at their configured paths. If you move MoonX360, update the path under **Settings → Advanced → Aurora Integration**.
