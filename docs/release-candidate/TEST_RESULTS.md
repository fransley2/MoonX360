# PC validation results — 2026-11-08

Physical status: **PENDING PHYSICAL XBOX 360 VALIDATION**. No console or live host was contacted.

The complete per-test commands, errors, components and timing are in `evidence/host-results.json` and `evidence/pc-results.json`. Timings describe test execution on the desktop, not Xbox performance.

## Final desktop suites

| Check | Environment | Result | Errors |
|---|---|---|---|
| host | Windows desktop; MSVC/XDK fxc or Python; simulated Xbox APIs | PASS — HOST VALIDATED | None |
| python | Windows desktop; MSVC/XDK fxc or Python; simulated Xbox APIs | PASS — HOST VALIDATED | None |
| expansion | Windows desktop; MSVC/XDK fxc or Python; simulated Xbox APIs | PASS — HOST VALIDATED | None |
| aurora | Windows desktop; MSVC/XDK fxc or Python; simulated Xbox APIs | PASS — HOST VALIDATED | None |
| artwork | Windows desktop; MSVC/XDK fxc or Python; simulated Xbox APIs | PASS — HOST VALIDATED | None |
| remote-feature-fixtures | Windows desktop; MSVC/XDK fxc or Python; simulated Xbox APIs | PASS — HOST VALIDATED | None |
| ui-shaders | Windows desktop; MSVC/XDK fxc or Python; simulated Xbox APIs | PASS — HOST VALIDATED | None |
| temporal-shaders | Windows desktop; MSVC/XDK fxc or Python; simulated Xbox APIs | PASS — HOST VALIDATED | None |
| legacy-brand-fixtures | Windows desktop; MSVC/XDK fxc or Python; simulated Xbox APIs | PASS — HOST VALIDATED | None |
| static analysis | Desktop MSVC /analyze; configuration/persistence only | PASS — HOST VALIDATED | None |
| git diff --check | Git working tree | PASS — HOST VALIDATED | None |

## Individual host fixtures

| Test | Environment | Result | Components |
|---|---|---|---|
| app-restart | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | host-tests\app-restart.cpp |
| focus-motion | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | xdk\tests\test_focus_motion.cpp, src\ui\canvas.c, src\ui\font.c, src\ui\draw.c, src\ui\ui.c, host-tests\fonts.obj |
| profile-keyboard | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | host-tests\profile-keyboard.cpp, src\config.c |
| user-profile-menu | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | host-tests\user-profile-menu.cpp, src\config.c |
| user-profiles | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | xdk\tests\test_user_profiles.cpp, src\config.c |
| home-history | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | xdk\tests\test_home_history.cpp |
| session-metrics | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | host-tests\session-metrics.cpp |
| ui-features | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | host-tests\ui-features.cpp, src\ui\canvas.c, src\ui\font.c, src\ui\draw.c, host-tests\fonts.obj |
| loading | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | host-tests\loading.cpp |
| feature-menu | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | host-tests\feature-menu.cpp |
| adaptive-lifecycle | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | host-tests\adaptive-lifecycle.cpp, src\config.c |
| stability-features | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | xdk\tests\test_stability_features.cpp |
| home-art | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | xdk\tests\test_home_art.cpp |
| dashboard-navigation | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | host-tests\dashboard-navigation.cpp, src\config.c |
| launch-profile | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | host-tests\launch-profile.cpp, src\config.c |
| settings-control | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | host-tests\settings-control.cpp, src\config.c |
| discovery | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | host-tests\discovery.cpp, src\config.c, src\mdns.c, src\host_cache.c |
| mdns | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | xdk\tests\test_mdns.cpp, src\mdns.c, src\host_cache.c |
| cached-video | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | host-tests\cached-video.cpp |
| lifecycle-recovery | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | host-tests\lifecycle-recovery.cpp |
| gpu-ui | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | host-tests\gpu-ui.cpp, src\ui\canvas.c, src\ui\font.c, src\ui\draw.c, src\ui\ui.c, host-tests\fonts.obj |
| cover-cache | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | host-tests\cover-cache.cpp |
| connection-task | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | host-tests\connection-task.cpp |
| controller-hotplug | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | host-tests\controller-hotplug.cpp |
| product-state | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | xdk\tests\test_product_state.cpp |
| product-flow | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | host-tests\product-flow.cpp, src\config.c |
| submit-timing | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | host-tests\submit-timing.cpp |
| render-pacing | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | host-tests\render-pacing.cpp |
| video-report | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | host-tests\video-report.cpp |
| gpu-policy | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | xdk\tests\test_gpu_policy.cpp |
| h264-diagnostic | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | xdk\tests\test_h264_diagnostic.cpp |
| h264-reference-policy | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | xdk\tests\test_h264_reference_policy.cpp |
| video-watchdog | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | xdk\tests\test_video_watchdog.cpp |
| display | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | host-tests\display.cpp |
| session-profile | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | host-tests\session-profile.cpp |
| settings-preview | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | host-tests\settings-preview.cpp, src\config.c, src\ui\canvas.c, src\ui\font.c, src\ui\draw.c, src\ui\ui.c, host-tests\fonts.obj |
| latency | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | host-tests\latency.cpp |
| deadline | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | xdk\tests\test_decode_deadline.cpp |
| pthread-atomic | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | host-tests\pthread_atomic.cpp |
| loopfilter | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | host-tests\loopfilter.c |
| idct | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | host-tests\idct.c |
| benchmark | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | host-tests\benchmark.cpp |
| texture | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | host-tests\texture.cpp |
| teardown | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | host-tests\teardown.cpp |
| release | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | host-tests\release.cpp |
| log | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | host-tests\log.cpp |
| metrics | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | xdk\tests\test_metrics_log.cpp |
| stream | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | xdk\tests\test_stream_checks.c |
| input | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | xdk\tests\test_input_session.cpp |
| settings | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | xdk\tests\test_settings_layout.cpp |
| config | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | xdk\tests\test_config.cpp, src\config.c |
| timing | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | xdk\tests\test_timing.cpp |
| overlay | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | host-tests\overlay.cpp, src\ui\canvas.c, src\ui\font.c, src\ui\draw.c, src\ui\ui.c, host-tests\fonts.obj |
| home | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | xdk\tests\test_home_ui.cpp, src\ui\canvas.c, src\ui\font.c, src\ui\draw.c, src\ui\ui.c, host-tests\fonts.obj |
| fast-decode | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | host-tests\fast-decode.cpp |
| wireless-crt | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | xdk\tests\test_wireless_crt.cpp, src\config.c |
| video-smoothing | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | xdk\tests\test_video_smoothing.cpp, src\config.c |
| switch-app | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | host-tests\switch-app.cpp |
| app-identity | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | host-tests\app-identity.cpp |
| host-ui | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | xdk\tests\test_host_ui.cpp, src\ui\canvas.c, src\ui\font.c, src\ui\draw.c, src\ui\ui.c, host-tests\fonts.obj |
| host-protocol | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | host-tests\host-protocol.cpp |
| host-recovery | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | host-tests\host-recovery.cpp, src\host_cache.c |
| host-compat | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | xdk\tests\test_host_compat.cpp, src\host_cache.c |
| session-callback | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | host-tests\session-callback.cpp |
| session-control | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | host-tests\session-control.cpp |
| launch-flow | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | host-tests\launch-flow.cpp |
| launch-snapshot | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | host-tests\launch-snapshot.cpp |
| launch-handoff | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | host-tests\launch-handoff.cpp |
| launch-audio | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | host-tests\launch-audio.cpp |
| launch-settings-menu | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | host-tests\launch-settings-menu.cpp, src\config.c |
| cover-http | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | host-tests\cover-http.cpp |
| storage-backend | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | host-tests\storage-backend.cpp |
| storage-diagnostics | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | xdk\tests\test_storage_diagnostics.cpp |
| launch-settings | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | xdk\tests\test_launch_settings.cpp, src\config.c |
| launch-cover | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | xdk\tests\test_launch_cover.cpp |
| performance | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | xdk\tests\test_performance.cpp |
| performance-runtime | Desktop MSVC / simulated APIs | PASS — HOST VALIDATED | host-tests\performance-runtime.cpp |

## Unavailable and deferred checks

- NOT TESTED — TOOL UNAVAILABLE: GCC portable runner (`g++`/`gcc` are absent); equivalent desktop coverage uses MSVC.
- NOT TESTED — TOOL UNAVAILABLE: whole-program Xenon analyzer / Xbox GPU emulator. Desktop `/analyze` is limited to configuration/persistence.
- PENDING — PHYSICAL XBOX REQUIRED: exact-XEX launch, VMX execution/bit-exactness on Xenon, CPU/GPU/audio/input timing, real UDP/FEC/network behavior, live host compatibility, CRT/display output, corruption/stutter comparison and endurance.

All candidate-critical reproducible failures have been resolved. Earlier development-run failures remain in baseline/retry logs; they are not represented as passing runs.

The historical 2026-10 rebrand pixel reference differs from the pre-existing native dashboard and is not a stabilization reference. The release check uses the protected pre-edit snapshot and requires exact equality of every message/startup pixel. Historical comparison remains available without the baseline environment override.

No removed assertions or narrowed pixel masks were used to pass the release UI check. Stale fixture dependencies and permission expectations were updated to the actual current contracts.
