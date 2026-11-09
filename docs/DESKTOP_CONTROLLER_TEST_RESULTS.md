# Desktop Controller Mode test results — 2026-11-08

Baseline commit: `efae2fc`; feature branch: `feature/desktop-controller-mode`.

Test XEX: `dist/MoonX360-Desktop-Controller-test-20261108/default.xex`

SHA-256: `0bff554447627a30b2b4bdb8717d6e2d2a0f0777cccf27c26a4cfb3af68ffb2f`

## Completed on the Windows host

- Full `python xdk/tests/run_host_tests.py --keep-going`: **80/80 passed**,
  plus all **3 build-cache tests**. Includes two new Desktop fixtures and existing
  input, hotplug, keyboard/config/profile, lifecycle, protocol and video regressions.
- `python xdk/tests/run_expansion_tests.py`: all fixtures passed, including actual
  Desktop settings/app-ID selection and mapping, Quick Menu, retained renderer,
  300 unchanged Desktop frames without allocations/uploads, no draw after close,
  refresh and storage regression checks.
- Actual XDK input-loop simulation: Desktop drag, local UTF-8, overlay, Quick Menu,
  Desktop-to-game, Back+R3 to legacy mouse, Guide release and no gamepad button/axis
  leakage during Desktop. This is not physical XInput validation.
- Keyboard policy: OSK down/up ordering and balanced modifiers; arrows/browser
  shortcuts do not repeat while held; accented UTF-8, symbols, caps, space,
  backspace, Enter/cancel and queue-error UI paths; remapping remains a permutation.
- Actual canvas previews rendered and inspected for toolbar, Portuguese accents
  and Quick Menu. Final preview rerun uses the final renderer source.
- XDK **Release / aggressive**, existing default diagnostic flags: Moonlight
  **32 sources, 0 failed**; application **25 sources, 0 failed**; link/XEX pass.
- Compile logs retain **141 existing warnings**, matching the prior
  `switch-app-release-20261108` logs: 133 renderer C4244, 4 video C4800,
  4 Moonlight/dependency warnings. **0 added warnings** after explicit UI casts.
  Existing host-fixture warnings remain distinct from XDK warnings.
- Final XEX2 header, Title ID **4D583336**, `xam.xex` / `xboxkrnl.exe` imports,
  `imagexex /dump`, and byte-identical embedded dashboard SPA verified.
- FFmpeg libraries staged from the verified matching prior Release build; manifest
  and all three library hashes validated by the existing XDK build script.
- `git diff --check` passed. Decoder/audio/dependency/build-policy sources match
  the baseline. Existing `xdk/build/default.xex` remains unchanged, SHA-256
  `96a9ea9631ccc66fd0972cfc18c546dd42c0ad73d281c05e266d5a3bcecd4cfb`.

## Live availability and limitations

Sunshine process and HTTP server on `127.0.0.1:47989` responded to a read-only
serverinfo request (wire appversion `7.1.431.-1`). The request was unpaired;
this is availability evidence, not live input or streaming interoperability.
Xbox XBDM at `192.168.15.10:730` was reachable. No candidate deployment or launch,
physical controller operation, Edge/Explorer/Steam interaction, Xbox video/audio
performance, real OSK/text insertion or touch keyboard activation was validated.
Apollo and Vibepollo have existing simulated protocol coverage; no live instances
were tested. All those hardware/host acceptance items remain pending.

The optional touch path focuses the taskbar via Win+B and requires the enabled
Windows keyboard icon and user selection. It does not automatically open the
touch keyboard. XAM remains excluded from streaming; the existing supported
presentation path rejects it. Basic input needs no host helper.

See [Desktop Controller Mode](DESKTOP_CONTROLLER_MODE.md) for controls, configuration,
implementation files, design evidence and the exact-XEX hardware acceptance checklist.
The artifact folder contains raw test/build logs, per-fixture JSON, source hashes,
FFmpeg provenance, XEX inspection and previews. These are automated/build evidence.
