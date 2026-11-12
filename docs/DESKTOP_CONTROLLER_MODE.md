# Desktop Mode

The unified Desktop Mode replaces the separate legacy mouse and Desktop entries.
The native renderer, GameStream transport, video/audio pipeline, stream profiles,
pairing, Aurora shortcut identity and Direct Launch ABI remain unchanged.

## Starting and stopping

Open **Back + L3** during a stream. **Desktop Mode** is the fourth Quick Menu row,
between Audio Volume and Performance Overlay. It shows On/Off. **A** toggles it
and returns to streaming; **X** opens the Desktop options. There is no duplicate
Desktop row under session controls.

**Back + R3** toggles the same mode directly. The configured two-second exit chord
always wins. Releasing a chord in stages must not send Escape, clicks or gamepad
buttons to the PC. Menu/Guide transitions and mode changes release held input and
wait for neutral controls before resuming.

Desktop Mode sends neutral gamepad state for every connected controller to avoid
duplicate Steam/game input. The activating controller owns the pointer and panels.
Automatic startup chooses a connected controller rather than assuming port zero.
If the owner disconnects, held clicks are released and another connected control
can take over after neutralization. If none is connected, no gamepad input leaks
through the hotplug grace samples. Returning to game mode restores gamepad input.

## Controls

| Control | Default action |
| --- | --- |
| Left stick | Mouse pointer |
| Right stick | Horizontal and vertical scrolling |
| A / B / X | Left / right / middle click; hold a click to drag |
| Y | Preferred keyboard; MoonX360 keyboard by default |
| R3 | Desktop actions |
| LT / RT | Precision / speed boost; precision takes priority |
| D-pad | Arrow key taps |
| LB / RB | Browser Back / Forward |
| Start | Enter |
| Back alone | Escape on release |

The twelve button actions can be remapped by swapping occupied actions. Reserved
chords, R3, sticks and triggers retain their functions. A click assigned to Back
is a tap on release; other click bindings can drag continuously.

The existing Compact / Text / Hidden indicator preference applies to Desktop.
Text mode includes the Y keyboard and R3 actions hints. Keyboard and toolbar
panels use the existing canvas/font/GPU fence path, with retained textures and
no per-poll allocations, waits or additional input threads.

## Desktop actions and keyboard

R3 offers Keyboard, Address/Search, Back, Forward, Switch window and More actions.
More actions contains Explorer, Refresh (F5), Home, Windows, Next tab, Fullscreen,
Windows keyboard and Notification area. B returns from More actions to the main
bar; B on the main bar closes it. Actions normally close the panel. Address/Search
sends Ctrl+L and opens the preferred keyboard. Refresh here means F5, separately
from the Quick Menu's Refresh Picture video recovery command.

The local keyboard supports letters, Portuguese accents, digits, symbols, space,
backspace, Enter, Caps and page selection. D-pad moves focus; A sends a character
immediately through the existing UTF-8 API. Enter sends Enter and closes the panel.
B closes without undoing text already sent. Queue errors are displayed explicitly.
Physical keyboard forwarding is suspended while local UI captures input; held
physical keys must be released before forwarding resumes.

Windows keyboard sends Win+Ctrl+O. Notification area sends Win+B and lets the user
select an enabled Windows keyboard icon with the pointer; it does not claim to
open the touch keyboard automatically. Window/browser actions depend on PC focus
and host support. No host helper, privileged action or streaming XAM keyboard loop
is introduced.

## Settings and existing installations

Open **Settings > Controller > Desktop Mode**. Preferences are grouped into:

- Behavior: initial mode and an optional app on the selected PC that starts Desktop
  automatically. Open that PC's Library before choosing the app. Identity uses the
  real host UUID and App ID, independently of the display name.
- Pointer and scrolling: sensitivity, deadzone, acceleration, precision and scroll
  speed, independent of gamepad calibration.
- Keyboard: MoonX360, Windows keyboard or Notification area. Existing choices are
  retained; new installations default to MoonX360.
- Control mapping: button actions and reserved-control help.
- Restore Desktop defaults: confirmation defaults to Cancel and resets only Desktop
  preferences and its automatic app binding.

Preferences remain global INI values, outside saved streaming profiles. Old INIs
with explicit mouse speed/acceleration inherit those values only when their
corresponding Desktop values are absent. Desktop values win regardless of INI
ordering. Loading does not rewrite configuration. Existing legacy mouse keys are
preserved for compatibility. Runtime Quick Menu toggles last for that session.

## Artwork reliability

The disk reader preserves valid artwork and its index on temporary allocation or
I/O errors and retries after one second. Missing files and confirmed invalid
payloads are distinct outcomes. A failed network refresh keeps existing artwork.
The selected image is prioritized, then the exact rendered library window, then
nearby images. Visible buffers are protected from eviction; borrowed draw commands
are retired before pixels are freed. Cache identity and disk formats are unchanged.

GPU images are staged and uploaded before replacing a resident texture. An image
failure gets a placeholder and a bounded retry; other images, text and navigation
continue through the native renderer. Shader/glyph failures retain the existing
complete software-frame fallback. Diagnostics distinguish disk, download, format,
decode and texture stages without exposing credentials.

## Validation

The local candidate package includes `docs/RELATORIO.md` and `docs/VALIDATION.json`.
Host fixtures, mock GPU checks and preview images are separate from real Xbox
validation. The exact candidate XEX hash and pending console checklist are recorded
in that package.
