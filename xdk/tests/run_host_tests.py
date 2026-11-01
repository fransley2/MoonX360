"""Run SDK-independent regression fixtures with desktop MSVC on Windows."""
import importlib.util
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
OUT = Path(os.environ.get('MOONX_TEST_OUT', ROOT / 'host-tests')).resolve()
OUT.mkdir(parents=True, exist_ok=True)
vcvars = Path(os.environ.get('VS_VCVARS64', r'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat'))
result = subprocess.run(f'cmd.exe /d /s /c ""{vcvars}" >nul && set"', capture_output=True, text=True, check=True)
env = dict(os.environ)
for line in result.stdout.splitlines():
    if '=' in line and not line.startswith('='):
        key, value = line.split('=', 1)
        env[key.upper()] = value
cl = next(str(Path(p) / 'cl.exe') for p in env['PATH'].split(';') if (Path(p) / 'cl.exe').exists())

def run(args, cwd=ROOT):
    r = subprocess.run([str(a) for a in args], cwd=cwd, env=env, capture_output=True, text=True, errors='replace')
    if r.stdout.strip(): print(r.stdout.strip())
    if r.stderr.strip(): print(r.stderr.strip())
    if r.returncode: raise SystemExit(r.returncode)

for generator, name in [('make_session_callback_test.py','session-callback.cpp'),('make_session_control_test.py','session-control.cpp'),('make_launch_flow_test.py','launch-flow.cpp'),('make_app_identity_test.py','app-identity.cpp'),('make_host_protocol_test.py','host-protocol.cpp'),('make_host_recovery_test.py','host-recovery.cpp'),('make_launch_snapshot_test.py','launch-snapshot.cpp'),('make_cover_http_test.py','cover-http.cpp'),('make_storage_backend_test.py','storage-backend.cpp'),('make_launch_settings_menu_test.py','launch-settings-menu.cpp'),('make_launch_handoff_test.py','launch-handoff.cpp'),('make_launch_audio_test.py','launch-audio.cpp'),('make_performance_runtime_test.py','performance-runtime.cpp'),('make_app_restart_test.py','app-restart.cpp'),('make_profile_keyboard_test.py','profile-keyboard.cpp'),('make_user_profiles_test.py','user-profile-menu.cpp'),('make_session_metrics_test.py','session-metrics.cpp'),('make_ui_features_test.py','ui-features.cpp'),('make_loading_test.py','loading.cpp'),('make_feature_menu_test.py','feature-menu.cpp'),('make_adaptive_lifecycle_test.py','adaptive-lifecycle.cpp'),('make_dashboard_navigation_test.py', 'dashboard-navigation.cpp'),('make_launch_profile_test.py', 'launch-profile.cpp'),('make_settings_control_test.py', 'settings-control.cpp'),('make_discovery_test.py', 'discovery.cpp'),('make_texture_upload_test.py', 'texture.cpp'),
                        ('make_lifecycle_recovery_test.py', 'lifecycle-recovery.cpp'),
                        ('make_gpu_ui_test.py', 'gpu-ui.cpp'),
                        ('make_cover_cache_test.py', 'cover-cache.cpp'),
                        ('make_connection_task_test.py', 'connection-task.cpp'),
                        ('make_controller_hotplug_test.py', 'controller-hotplug.cpp'),
                        ('make_product_flow_test.py', 'product-flow.cpp'),
                        ('make_submit_timing_test.py', 'submit-timing.cpp'),
                        ('make_render_pacing_test.py', 'render-pacing.cpp'),
                        ('make_video_report_test.py', 'video-report.cpp'),
                        ('make_latency_test.py', 'latency.cpp'),
                        ('make_cached_video_test.py', 'cached-video.cpp'),
                        ('make_display_test.py', 'display.cpp'),
                        ('make_session_profile_test.py', 'session-profile.cpp'),
                        ('make_settings_preview.py', 'settings-preview.cpp'),
                        ('make_pthread_atomic_test.py', 'pthread_atomic.cpp'),
                        ('make_loopfilter_reference_test.py', 'loopfilter.c'),
                        ('make_benchmark_test.py', 'benchmark.cpp'),
                        ('make_overlay_test.py', 'overlay.cpp'),
                        ('make_video_teardown_test.py', 'teardown.cpp'),
                        ('make_log_platform_test.py', 'log.cpp'),
                        ('make_ui_release_test.py', 'release.cpp')]:
    run([sys.executable, ROOT / 'xdk/tests' / generator, OUT / name])
run([sys.executable, ROOT / 'xdk/tests/make_idct_reference_test.py',
     ROOT / 'xdk/deps/XBMC-360/libraries/ffmpeg', OUT / 'idct.c'])
spec = importlib.util.spec_from_file_location('build', ROOT / 'xdk/build.py')
build = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build)
fonts = build.generate_fonts()
flags = ['/nologo', '/O2', '/W3', '/EHsc', '/D_CRT_SECURE_NO_WARNINGS', '/Dstrtok_r=strtok_s',
         '/I' + str(ROOT / 'host-tests'), '/I' + str(ROOT / 'xdk/deps/moonlight-common-c/src'), '/FI' + str(ROOT / 'src/performance.h')]
run([cl, *flags, '/TC', '/c', fonts, '/Fofonts.obj'], OUT)
ui = [ROOT / 'src/ui' / name for name in ('canvas.c', 'font.c', 'draw.c', 'ui.c')]
jobs = [('app-restart',[OUT/'app-restart.cpp']),('focus-motion',[ROOT/'xdk/tests/test_focus_motion.cpp',*ui,OUT/'fonts.obj']),('profile-keyboard',[OUT/'profile-keyboard.cpp',ROOT/'src/config.c']),('user-profile-menu',[OUT/'user-profile-menu.cpp',ROOT/'src/config.c']),('user-profiles',[ROOT/'xdk/tests/test_user_profiles.cpp',ROOT/'src/config.c']),('home-history',[ROOT/'xdk/tests/test_home_history.cpp']),('session-metrics',[OUT/'session-metrics.cpp']),('ui-features',[OUT/'ui-features.cpp',*ui[:3],OUT/'fonts.obj']),('loading',[OUT/'loading.cpp']),('feature-menu',[OUT/'feature-menu.cpp']),('adaptive-lifecycle',[OUT/'adaptive-lifecycle.cpp',ROOT/'src/config.c']),('stability-features',[ROOT / 'xdk/tests/test_stability_features.cpp']),('home-art', [ROOT / 'xdk/tests/test_home_art.cpp']),('dashboard-navigation', [OUT / 'dashboard-navigation.cpp', ROOT / 'src/config.c']),('launch-profile', [OUT / 'launch-profile.cpp', ROOT / 'src/config.c']),
        ('settings-control', [OUT / 'settings-control.cpp', ROOT / 'src/config.c']),
        ('discovery', [OUT / 'discovery.cpp', ROOT / 'src/config.c', ROOT / 'src/mdns.c', ROOT / 'src/host_cache.c']),
        ('mdns', [ROOT / 'xdk/tests/test_mdns.cpp', ROOT / 'src/mdns.c', ROOT / 'src/host_cache.c']),
        ('cached-video', [OUT / 'cached-video.cpp']),
        ('lifecycle-recovery', [OUT / 'lifecycle-recovery.cpp']),
        ('gpu-ui', [OUT / 'gpu-ui.cpp', *ui, OUT / 'fonts.obj']),
        ('cover-cache', [OUT / 'cover-cache.cpp']),
        ('connection-task', [OUT / 'connection-task.cpp']),
        ('controller-hotplug', [OUT / 'controller-hotplug.cpp']),
        ('product-state', [ROOT / 'xdk/tests/test_product_state.cpp']),
        ('product-flow', [OUT / 'product-flow.cpp', ROOT / 'src/config.c']),
        ('submit-timing', [OUT / 'submit-timing.cpp']),
        ('render-pacing', [OUT / 'render-pacing.cpp']),
        ('video-report', [OUT / 'video-report.cpp']),
        ('gpu-policy', [ROOT / 'xdk/tests/test_gpu_policy.cpp']),
        ('display', [OUT / 'display.cpp']),
        ('session-profile', [OUT / 'session-profile.cpp']),
        ('settings-preview', [OUT / 'settings-preview.cpp', ROOT / 'src/config.c', *ui, OUT / 'fonts.obj']),
        ('latency', [OUT / 'latency.cpp']),
        ('deadline', [ROOT / 'xdk/tests/test_decode_deadline.cpp']),
        ('pthread-atomic', [OUT / 'pthread_atomic.cpp']),
        ('loopfilter', [OUT / 'loopfilter.c']), ('idct', [OUT / 'idct.c']),
        ('benchmark', [OUT / 'benchmark.cpp']),
        ('texture', [OUT / 'texture.cpp']), ('teardown', [OUT / 'teardown.cpp']),
        ('release', [OUT / 'release.cpp']), ('log', [OUT / 'log.cpp']),
        ('metrics', [ROOT / 'xdk/tests/test_metrics_log.cpp']),
        ('stream', [ROOT / 'xdk/tests/test_stream_checks.c']),
        ('input', [ROOT / 'xdk/tests/test_input_session.cpp']),
        ('settings', [ROOT / 'xdk/tests/test_settings_layout.cpp']),
        ('config', [ROOT / 'xdk/tests/test_config.cpp', ROOT / 'src/config.c']),
        ('overlay', [OUT / 'overlay.cpp', *ui, OUT / 'fonts.obj']),
        ('home', [ROOT / 'xdk/tests/test_home_ui.cpp', *ui, OUT / 'fonts.obj'])]
selected=set(sys.argv[1:])
jobs.append(('app-identity',[OUT/'app-identity.cpp']))
jobs.append(('host-ui',[ROOT/'xdk/tests/test_host_ui.cpp',*ui,OUT/'fonts.obj']))
jobs.append(('host-protocol',[OUT/'host-protocol.cpp']))
jobs.append(('host-recovery',[OUT/'host-recovery.cpp',ROOT/'src/host_cache.c']))
jobs.append(('host-compat',[ROOT/'xdk/tests/test_host_compat.cpp',ROOT/'src/host_cache.c']))
jobs.append(('session-callback',[OUT/'session-callback.cpp']))
jobs.append(('session-control',[OUT/'session-control.cpp']))
jobs.append(('launch-flow',[OUT/'launch-flow.cpp']))
jobs.append(('launch-snapshot',[OUT/'launch-snapshot.cpp']))
jobs.append(('launch-handoff',[OUT/'launch-handoff.cpp']))
jobs.append(('launch-audio',[OUT/'launch-audio.cpp']))
jobs.append(('launch-settings-menu',[OUT/'launch-settings-menu.cpp',ROOT/'src/config.c']))
jobs.append(('cover-http',[OUT/'cover-http.cpp']))
jobs.append(('storage-backend',[OUT/'storage-backend.cpp']))
jobs.append(('storage-diagnostics',[ROOT/'xdk/tests/test_storage_diagnostics.cpp']))
jobs.append(('launch-settings',[ROOT/'xdk/tests/test_launch_settings.cpp',ROOT/'src/config.c']))
jobs.append(('launch-cover',[ROOT/'xdk/tests/test_launch_cover.cpp']))
jobs.append(('performance',[ROOT/'xdk/tests/test_performance.cpp']))
jobs.append(('performance-runtime',[OUT/'performance-runtime.cpp']))
for name, sources in jobs:
    if selected and name not in selected:continue
    print('TEST:', name, flush=True)
    test_flags=[f for f in flags if not (name=='performance-runtime' and f.startswith('/FI'))]
    if name=='cover-cache':test_flags=test_flags+['/std:c++17']
    run([cl, *test_flags, *[('/Tp' + str(p)) if Path(p).suffix in ('.c', '.cpp') else str(p) for p in sources], '/Fe' + name + '.exe'], OUT)
    run([OUT / (name + '.exe')], OUT)
if not selected:run([sys.executable, ROOT / 'xdk/tests/test_build_cache.py'])
print('PASS: selected host regression fixtures' if selected else 'PASS: all host regression fixtures')
