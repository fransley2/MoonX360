# MoonX360 Controls & Shortcuts

All controller bindings below are verified directly against `src/app.c`, `src/quick_menu.h`, `src/input_guide.h`, `src/desktop_input.h`, and `xdk/src/input_xdk.cpp`.

---

## 1. Dashboard & Library Navigation

| Screen | Button / Input | Action |
|---|---|---|
| **Global Navigation** | `LB` / `RB` | Switch top tabs (**Home** · **Library** · **Computers** · **Settings**) |
| **Home Dashboard** | `Left Stick` / `D-Pad` | Navigate featured card, recent games rail, and quick tiles |
| | `A` | Launch game / Open tile |
| | `X` | Open Game Details (per-game profile, favorite, launch cover, Aurora shortcut) |
| | `Y` | Search library |
| | `Start` | Host actions menu |
| | `B` | Return focus to top / Back |
| **My Library** | `A` | Start streaming selected game / app |
| | `X` | Open Game Details |
| | `Y` | Refresh app list and cover artwork from host PC |
| | `Start` | Open Library menu (Filter: Recent / Favorites / All Apps / Desktop, Host actions, Search) |
| | `B` | Back to Home |
| **Settings** | `Up` / `Down` | Select category or setting row |
| | `Left` / `Right` / `A` | Cycle value, toggle option, or open submenu |
| | `B` | Go back one menu level |

---

## 2. In-Stream Shortcuts & Overlays

| Action | Default Controller Combo | Notes |
|---|---|---|
| **Open Quick Menu** | `Back + L3` | Slide-out in-stream menu (Resume, Volume/Mute, Keyboard, Mouse/Desktop Mode, Performance Overlay, Refresh Picture, Disconnect, Close PC App) |
| **Toggle Mouse / Desktop Mode** | `Back + R3` | Switches controller between normal PC gamepad and Desktop Mouse Mode |
| **Cycle Performance Overlay** | `Back + LB + RB + X` | Cycles `Off` → `Basic` → `Detailed` → `Developer` |
| **Switch Developer Overlay Page** | `X` (in Quick Menu on Performance row) | Toggles Developer page 1 / page 2 |
| **Open Virtual Smart Keyboard** | `Y` (in Quick Menu) or Quick Menu → **Keyboard** | Opens the 5-row full PC Smart Keyboard over the stream |
| **Request Picture Refresh (IDR)** | Quick Menu → **Refresh picture** | Forces a fresh H.264 keyframe from the host |
| **PC Guide Button** | Hold `Start + LB` (`350 ms`) | Sends Moonlight `SPECIAL_FLAG` (Steam Big Picture / Xbox Game Bar) without opening Xbox 360 Guide; toggleable in **Settings → Controller → Shortcuts** |
| **Disconnect (Keep PC App Running)** | Hold `LB + RB + L3 + R3` (`2 s`) | Returns to MoonX360; alternative legacy combo `Back + Start + LB + RB` selectable in **Settings → Controller** |
| **Xbox 360 System Guide** | `Guide` button | Safely suspends Desktop Mode / mouse state while Xbox system blade is open |

---

## 3. Desktop Mode Controls

When Desktop Mode is active (`Back + R3` or Quick Menu):

| Input | Default Action |
|---|---|
| `Left Stick` | Move mouse cursor (radial deadzone + progressive low-speed curve) |
| `Right Stick` | Vertical & horizontal scroll |
| `LT` (hold) | Precision cursor slowdown |
| `RT` (hold) | Cursor speed boost |
| `A` | Left mouse click |
| `B` | Right mouse click |
| `X` | Middle mouse click (all mappings customizable in **Settings → Controller → Desktop Mode**) |
| `R3` | Open Desktop quick-action toolbar (Keyboard, Address bar, Alt+Tab, Explorer, F11, etc.) |
| **USB Keyboard** | Plugged-in Xbox 360 USB keyboards and Chatpads are forwarded directly when active |

---

## 4. Aurora Direct Launch

When launching a game via a generated `MoonX360Shortcut` entry in Aurora:
- MoonX360 launches straight into the target PC game using the stored host UUID, app ID, and bound profile.
- Disconnecting (`LB + RB + L3 + R3` or Quick Menu → **Disconnect**) returns cleanly to Aurora or MoonX360 depending on your configuration. See [AURORA_DIRECT_LAUNCH.md](AURORA_DIRECT_LAUNCH.md).
