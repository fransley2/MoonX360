"""Host regression tests using g++/gcc. Does not build XEX or validate Xbox APIs."""
from pathlib import Path
import subprocess,sys,importlib.util
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'host-tests';OUT.mkdir(exist_ok=True)
def run(args,cwd=ROOT):
    r=subprocess.run([str(a) for a in args],cwd=cwd,capture_output=True,text=True)
    if r.returncode:raise RuntimeError(' '.join(map(str,args))+'\n'+r.stdout+r.stderr)
    if r.stdout.strip():print(r.stdout.strip(),flush=True)
flags=['-std=c++11','-O2','-I'+str(ROOT/'test-support'),'-D_strdup=strdup','-D_stricmp=strcasecmp','-D_strnicmp=strncasecmp','-D_snprintf=snprintf']
# Import only to generate embedded font bytes; no Microsoft tools executed.
spec=importlib.util.spec_from_file_location('build',ROOT/'xdk/build.py');build=importlib.util.module_from_spec(spec);spec.loader.exec_module(build)
fonts=build.generate_fonts();run(['gcc','-c',fonts,'-o',OUT/'fonts.o'])
ui=[ROOT/'src/ui'/x for x in ('canvas.c','font.c','draw.c','ui.c')]+[OUT/'fonts.o']
jobs=[('user-profiles',[ROOT/'xdk/tests/test_user_profiles.cpp',ROOT/'src/config.c']),('home-history',[ROOT/'xdk/tests/test_home_history.cpp']),('config',[ROOT/'xdk/tests/test_config.cpp',ROOT/'src/config.c']),
('settings',[ROOT/'xdk/tests/test_settings_layout.cpp']),('input',[ROOT/'xdk/tests/test_input_session.cpp']),
('deadline',[ROOT/'xdk/tests/test_decode_deadline.cpp']),('stability-features',[ROOT/'xdk/tests/test_stability_features.cpp']),
('gpu-policy',[ROOT/'xdk/tests/test_gpu_policy.cpp']),('stream',[ROOT/'xdk/tests/test_stream_checks.c']),
('metrics',[ROOT/'xdk/tests/test_metrics_log.cpp']),('product-state',[ROOT/'xdk/tests/test_product_state.cpp']),
('home-art',[ROOT/'xdk/tests/test_home_art.cpp']),('mdns',[ROOT/'xdk/tests/test_mdns.cpp',ROOT/'src/mdns.c',ROOT/'src/host_cache.c']),
('home',[ROOT/'xdk/tests/test_home_ui.cpp',*ui])]
for gen,name,extra in [('make_overlay_test.py','overlay',ui),('make_dashboard_navigation_test.py','dashboard-navigation',[ROOT/'src/config.c']),('make_profile_keyboard_test.py','profile-keyboard',[ROOT/'src/config.c']),('make_user_profiles_test.py','user-profile-menu',[ROOT/'src/config.c']),
('make_session_metrics_test.py','session-metrics',[]),
('make_ui_features_test.py','ui-features',[*ui[:3],OUT/'fonts.o']),('make_loading_test.py','loading',[]),
('make_feature_menu_test.py','feature-menu',[]),('make_adaptive_lifecycle_test.py','adaptive-lifecycle',[ROOT/'src/config.c']),
('make_settings_control_test.py','settings-control',[ROOT/'src/config.c']),
('make_launch_profile_test.py','launch-profile',[ROOT/'src/config.c']),
('make_session_profile_test.py','session-profile',[]),('make_display_test.py','display',[]),
('make_controller_hotplug_test.py','controller-hotplug',[]),
('make_product_flow_test.py','product-flow',[ROOT/'src/config.c']),
('make_cached_video_test.py','cached-video',[]),('make_texture_upload_test.py','texture',[]),
('make_lifecycle_recovery_test.py','lifecycle-recovery',[]),
('make_connection_task_test.py','connection-task',[]),
('make_gpu_ui_test.py','gpu-ui',ui)]:
    target=OUT/(name+'.cpp');run([sys.executable,ROOT/'xdk/tests'/gen,target]);jobs.append((name,[target,*extra]))
for name,sources in jobs:
    print('TEST '+name,flush=True)
    run(['g++',*flags,*(['-include',str(ROOT/'test-support/host_alloc.h')] if name=='texture' else []),*sources,'-o',OUT/name]);run([OUT/name],OUT)
run(['g++',*flags,'-fsyntax-only',ROOT/'src/app.c',ROOT/'src/config.c',ROOT/'src/ui/ui.c'])
print('PASS: %d host fixtures and shared-source syntax checks (test-only crypto declarations)'%len(jobs))
