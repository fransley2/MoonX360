"""Read stopped-session reports supplied by the user; never access a console."""
from pathlib import Path
import json
import math
import re
import sys

def number(fields,key):
    try:
        value=float(fields[key])
        return value if math.isfinite(value) and value>=0 else None
    except (KeyError,ValueError,TypeError):return None

def parse_report(path):
    path=Path(path)
    fields=dict(line.split('=',1) for line in path.read_text(encoding='utf-8-sig',errors='replace').splitlines() if '=' in line)
    duration=number(fields,'stream_duration_us')
    def rate(key):
        count=number(fields,key)
        return count*1000000/duration if duration and count is not None else None
    def tail(metric,q):
        samples=number(fields,metric+'_samples')
        return number(fields,metric+'_p'+str(q)+'_us') if samples is not None and samples>=30 else None
    result={'source':str(path),'worker_map':fields.get('worker_map'),
            'slice_dispatch':fields.get('slice_dispatch'),'slice_dispatch_version':number(fields,'slice_dispatch_version'),
            'build_profile':fields.get('build_profile'),'video_requested_fps':fields.get('video'),
            'workers_requested':number(fields,'workers_requested'),'workers_active':number(fields,'workers_active'),
            'thread_layout':fields.get('thread_layout'),'bitrate_kbps':number(fields,'bitrate_kbps'),
            'duration_us':duration,'callback_fps':rate('received'),'decoded_fps':rate('decoded'),'shown_fps':rate('shown'),
            'fast_decode_config':number(fields,'fast_decode_config'),'recovery_count':number(fields,'recovery_count'),
            'full_frames':number(fields,'full_frames'),'reduced_filter_frames':number(fields,'fast_frames'),
            'callback_mean_us':number(fields,'callback_total_avg_us'),'callback_gap_mean_us':number(fields,'callback_gap_avg_us'),
            'frame_copy_mean_us':number(fields,'frame_copy_avg_us'),
            'network_received_fps':None,'decode_mean_us':number(fields,'decode_avg_us'),
            'decode_p95_us':tail('decode',95),'decode_p99_us':tail('decode',99),
            'present_wait_p95_us':tail('present_wait',95),'present_wait_p99_us':tail('present_wait',99),
            'queue_peak_us':number(fields,'queue_age_max_session_us'),'queue_peak_sampled_frames':number(fields,'network_queue_peak_sampled_1Hz'),
            'drops':number(fields,'dropped'),'decode_errors':number(fields,'decode_errors'),
            'frame_gaps':number(fields,'frame_gaps_observed'),'starvation':number(fields,'gpu_slot_starvation'),
            'failovers':number(fields,'cached_failovers'),'physical_mode':fields.get('display'),
            'physical_hz':number(fields,'display_hz'),'interlaced':number(fields,'display_interlaced'),
            'app_backbuffer':fields.get('backbuffer'),'shader':fields.get('shader'),'pipeline':fields.get('pipeline'),
            'percentile_scope':fields.get('pipeline_percentiles'),'idr_received':None,'idr_requests':None,
            'packet_loss':None,'fec_recovered':None,'fec_unrecoverable':None,
            'audio_latency_ms':None,'input_latency_ms':None,'warnings':[]}
    if result['workers_requested']!=result['workers_active']:result['warnings'].append('requested/active worker count differs')
    if result['worker_map'] not in ('current','previous'):result['warnings'].append('missing/unknown build worker map')
    if not duration:result['warnings'].append('missing/invalid session duration; FPS unavailable')
    if result['decode_p99_us'] is None:result['warnings'].append('decode percentiles unavailable or fewer than 30 samples')
    if result['shader']!='FAST':result['warnings'].append('shader is not fixed FAST; compare only matching settings')
    if result['physical_hz'] is not None and not (abs(result['physical_hz']-59.94)<0.02 or abs(result['physical_hz']-60)<0.02):
        result['warnings'].append('physical refresh is outside the 59.94/60 Hz comparison')
    if number(fields,'capture_frames'):result['warnings'].append('recording was active; do not mix with non-recording runs')
    observations=path.parent/'observations.json'
    if observations.is_file():result['manual_observations']=json.loads(observations.read_text(encoding='utf-8-sig'))
    case_id=result.get('manual_observations',{}).get('case_id') or path.parent.name
    expected=re.match(r'P(480|720|1080)-(\d+)',case_id)
    if expected:
        video=re.match(r'\d+x(\d+)@60$',fields.get('video',''))
        if not video or video[1]!=expected[1] or result['bitrate_kbps']!=int(expected[2])*1000:
            result['warnings'].append('case ID and reported resolution/FPS/bitrate differ; possible stale report')
    result['report_freshness']='Copy immediately after stop; no per-run UTC identity exists in the backend report.'
    result['interpretation']='No performance verdict; rolling tails and CPU Present wait are not GPU execution or session-wide percentiles.'
    return result

def main():
    if len(sys.argv)!=3:raise SystemExit('Usage: py xdk/tests/collect_ab_results.py <local-results-folder> <summary.json>')
    root=Path(sys.argv[1]);reports=sorted(root.rglob('decoder-session.txt'))
    if not reports:raise SystemExit('No decoder-session.txt found in '+str(root.resolve()))
    output=Path(sys.argv[2]);output.write_text(json.dumps([parse_report(p) for p in reports],indent=2,ensure_ascii=False),encoding='utf-8')
    print('Parsed',len(reports),'reports into',output,'; no defaults changed.')

if __name__=='__main__':main()
