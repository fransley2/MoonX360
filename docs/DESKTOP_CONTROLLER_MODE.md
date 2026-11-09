# Desktop Controller Mode

Feature branch: `feature/desktop-controller-mode`. Baseline commit: `efae2fc`.
This baseline preserves the pre-existing stabilization work, including its
decoder changes. The Desktop feature does not change decoding, audio, bitrate,
FEC, refresh policy, stream presets, or the bundled Moonlight library.

## Using the feature

During a stream, open **Back + L3 → Desktop Controller Mode**. Select Desktop
Mode On / Off, Desktop Toolbar, Keyboard, Windows OSK, Touch keyboard taskbar,
Switch window, or Windows Explorer. Toolbar and Keyboard explicitly select
Desktop Mode. OSK, taskbar access, Alt+Tab and Explorer can also be requested
without changing the current mode. Runtime mode changes last for that session.

Before streaming, use **Settings → Remote mouse and keyboard → Desktop
Controller Mode** to configure the initial mode, pointer controls, keyboard and
button mapping. Settings save through the existing settings persistence flow.
To select automatic Desktop launch, first load the selected PC's app library,
then choose its application in this menu. The binding stores **host UUID +
numeric app ID**, even if the application is renamed. An app called Desktop on
another PC or with another ID does not match. Only one explicit launch binding
is stored; changing server application IDs requires choosing the app again.

Desktop preferences are global INI options and are excluded from saved stream
profiles. Old configuration files default to gamepad input. Game profiles and
the existing legacy mouse sensitivity remain independent.

| Control | Default Desktop action |
| --- | --- |
| Left stick | Mouse, configurable deadzone, sensitivity and acceleration |
| Right stick | High resolution horizontal / vertical wheel |
| A | Left click; hold to drag |
| B / X | Right / middle click |
| Y | Selected keyboard |
| D-pad | One arrow key tap per press |
| LB / RB | Alt+Left / Alt+Right |
| LT / RT | Precision / proportional boost; LT wins if both are held |
| Start | Enter |
| Back alone | Escape on release |
| R3 alone | Desktop Toolbar |

The twelve button actions can be remapped. Assigning an occupied action swaps
its old binding, so no action becomes unreachable through a duplicate.
Back-bound actions execute on release; a click assigned to Back becomes a tap.
Continuous drag remains available on any other click binding. Stick axes,
triggers, R3 toolbar and the reserved chords stay reserved.

## Chord priority and input ownership

The configured two-second exit chord remains authoritative. Back+L3 opens the
existing Quick Menu. Back+R3 selects the existing legacy mouse mode, including
when leaving Desktop Mode. Back+LB+RB+X toggles the performance overlay once per
chord press and consumes the chord until neutral. Back participates in no
Desktop action until released alone. A reserved chord cannot send Escape,
browser navigation, middle click or Enter as a Desktop side effect.

Desktop Mode sends neutral gamepad state for every controller, avoiding Steam
or gamepad-enabled desktop applications receiving both mapped and gamepad
input. Game mode retains the existing polling, calibration, hotplug, rumble
and gamepad forwarding. Selecting legacy mouse restores that existing policy.
Mode changes, disconnect, Guide, Quick Menu, keyboard and toolbar entry release
held mouse buttons. Controller actions are gated until neutral on local UI
transitions. Physical keyboard forwarding uses the existing aggregate snapshot
policy and is suspended during local input capture; held physical keys must be
released before forwarding resumes. Generated shortcuts are bounded down/up
sequences with reverse modifier release and no repeat timer.

## Keyboard and toolbar

Windows OSK uses **Win+Ctrl+O**: Win down, Ctrl down, O down, O up, Ctrl up,
Win up. Explicit modifier masks and extended-key flags accompany the events.
The [Microsoft accessibility shortcut reference](https://support.microsoft.com/en-us/accessibility/windows/windows-keyboard-shortcuts-for-accessibility)
documents this OSK command. It toggles OSK; it does not report whether Windows
accepted or displayed it.

The XDK SDK declares `XShowKeyboardUI` in `include/xbox/xbox.h`. MoonX360's
existing `plat_edit_text()` in `xdk/src/main_xdk.cpp` expressly rejects streaming
and otherwise waits for overlapped completion while presenting its own UI.
That path has no certified streaming presentation/input ownership. We preserve
the guard and use the local keyboard instead; this is a conservative integration
decision, not proof that the XAM API is universally unsafe during streaming.
No XAM call or waiting loop was added to the streaming thread.

The local keyboard has letters, uppercase, Portuguese accents (including ç,
ã, õ, á/à/â, é/ê, í, ó/ô, ú and ü), digits and common symbols. D-pad moves focus;
A confirms each character immediately through `LiSendUtf8TextEvent`. Space,
backspace, Enter, Caps, Page, Tab and Windows are explicit keys. Enter sends an
Enter key and closes the keyboard. B / Close cancels further input; already
confirmed characters remain on the PC. The UTF-8 byte strings are independent
of the XDK compiler's source codepage. Queue failure displays an error and
leaves OSK available. The bundled library has this API; successful queueing is
not a host acknowledgement of text insertion. The existing library owns UTF-8
packet synchronization on its send thread, outside decode/audio callbacks.

Touch keyboard access is optional and intentionally limited: enable Windows'
touch keyboard taskbar icon, then select Taskbar / Touch keyboard taskbar.
MoonX360 sends **Win+B** to focus the notification area; use the Desktop mouse
to select the keyboard icon. This does not automatically invoke the touch
keyboard or guarantee that Windows exposes its icon. There is no TabTip.exe
launch, undocumented COM activation, Run command, installed host helper,
privilege elevation or security prompt bypass. Local UTF-8 and OSK remain the
basic keyboard paths.

The toolbar provides Back, Forward, Refresh (F5), Address/Search (Ctrl+L),
Keyboard, Home (Alt+Home), Windows, Tabs (Ctrl+Tab) and Fullscreen (F11).
These act on the focused Windows application; browser-specific commands depend
on its shortcuts. Toolbar Refresh is unrelated to Quick Menu Refresh Picture.
A executes; B closes. Closing omits all Desktop draw calls on the next sampled
frame, leaving no persistent overlay.

## Architecture and cost

- `src/desktop_options.h`: bounded preferences and conflict-free action swaps.
- `src/desktop_input.h`: input-thread state, edges, mouse accumulation, keyboard
  shortcuts and cleanup. No new threads, waits, per-poll allocations or I/O.
- `src/desktop_keyboard_layout.h`: UTF-8 labels shared with the renderer.
- `xdk/src/input_xdk.cpp`: integrates after reserved chords and local UI routing,
  before the existing gamepad sender. Publishes one atomic Desktop UI word.
- `src/config.h`, `src/config.c`, `src/app.c`: additive INI persistence, settings,
  controller mapping and explicit launch binding; saved profile schema unchanged.
- `src/quick_menu.h`, `xdk/src/gfx_stream_menu.cpp`: Desktop action page and
  retained toolbar/keyboard using the existing canvas, fonts, theme and fences.

The Desktop panel uses one 640×300 CPU canvas and at most two retained GPU
textures, allocated on first use. It uploads only on state changes. When both
textures are busy it retains the last complete image rather than waiting.
The existing renderer cleanup owns all three panels. No video upload code,
decoder code, new GPU effects or external dependencies are introduced.

## Validation and hardware acceptance

See the accompanying `TEST_RESULTS.md`, `audit.json`, SHA-256 and build logs in
`dist/MoonX360-Desktop-Controller-test-20261108/`. The package deliberately
contains no replacement INI, pairing keys or certificates.

Automated tests exercise the actual policy and actual XDK input loop with
simulated XInput and Moonlight send functions. Renderer tests cover bounded
allocations, unchanged-state upload suppression, fences and zero drawing after
closing. Host fixtures do not establish physical latency, CPU/GPU cost, correct
Windows focus, real text insertion or visual stream performance.

The local Sunshine HTTP server and Xbox XBDM were reachable during this run.
Only read-only availability checks were made; the candidate has not been
deployed or launched on Xbox. Apollo and Vibepollo have simulated protocol
coverage from the existing regression suite, with no live instances validated.

Before accepting this exact XEX on hardware, record its hash and test:

1. Sunshine Desktop selected by its stored host/ID, including a renamed app and
   a similarly named app on a different host. Confirm default game mode for games.
2. Slow cursor motion, deadzone, acceleration, LT precision, RT boost, diagonal
   motion and both scroll axes at the actual console poll rate.
3. Left/right/middle clicks, drag outside the initial window, disconnect and
   Guide while dragging. Confirm no stuck button on reconnect.
4. Edge: Back/Forward/F5, Ctrl+L with accented search text, tabs, Home and F11.
   Confirm the toolbar leaves no pixels after B or executing an action.
5. Explorer navigation, context menu, dragging a harmless test file, Enter and
   Escape. Test Alt+Tab and Win+E from the Desktop Quick Menu page.
6. OSK down/up behavior, Portuguese UTF-8, backspace, space, symbols, caps,
   Enter, cancel and an attached USB keyboard held across local UI transitions.
7. Enable the Windows touch keyboard taskbar icon and test its optional access.
   If unavailable, retain Local UTF-8 / OSK.
8. Steam and a controller game: Desktop has no duplicate gamepad actions;
   game mode restores normal gamepad input; Back+R3 restores legacy mouse.
9. Both exit chord settings, Back+L3, Back+R3 and Back+LB+RB+X, including staggered
   key releases, multiple controllers and hot-unplug.
10. Repeat on Apollo and Vibepollo when available. Compare video/audio behavior
    with the preserved baseline at identical stream settings; record controller
    responsiveness and frame/audio continuity separately from build success.
