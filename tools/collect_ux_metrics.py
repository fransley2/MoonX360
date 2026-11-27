"""Summarize real MoonX360 diagnostic logs. Missing observations remain null.
Quick Menu timing ends at CPU draw submission, not GPU completion or scanout.
"""
import argparse,json,re,math
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('logs',nargs='+',type=Path);p.add_argument('--out',required=True,type=Path);a=p.parse_args()
windows={};counters={};pending={}
def add(name,value):windows.setdefault(name,[]).append(value)
for file in a.logs:
 pending.clear()
 raw=file.read_bytes();text=raw.decode('utf-16' if raw[:2] in (b'\xff\xfe',b'\xfe\xff') else 'utf-8-sig',errors='replace')
 for line in text.splitlines():
  fields={k:int(v) for k,v in re.findall(r'(\w+)=(-?\d+)',line)}
  if '[UX] quick input_ms=' in line:pending[fields['state']]=fields['input_ms']
  if '[UX] quick submit_ms=' in line and fields.get('state') in pending:
   delay=(fields['submit_ms']-pending.pop(fields['state'])) & 0xffffffff
   if delay<60000:add('quick_input_to_submit_ms',delay)
  for prefix,name in [('[UX] preflight','preflight'),('[UX] library','library'),('[UX] teardown','teardown'),('[ART] app=','artwork')]:
   if prefix in line:
    for k,v in fields.items():
     if k.endswith('_ms') and (k!='decode_ms' or fields.get('network')):add(name+'_'+k,v)
  if '[ART] stage=gpu' in line and fields.get('width') in (150,192):add('cover_gpu_cpu_upload_us',fields['upload_us'])
  if '[ART] stage=gallery-exit' in line:counters.update(fields)
  m=re.search(r'\[NET\] launch done (\d+) ms',line)
  if m:add('host_launch_ms',int(m[1]))
required=['library_list_ms','library_first_cover_ms','preflight_total_ms','host_launch_ms','artwork_decode_ms','cover_gpu_cpu_upload_us','quick_input_to_submit_ms','teardown_input_ms','teardown_transport_ms','teardown_total_ms']
def summary(v):
 if not v:return None
 v=sorted(v);return {'count':len(v),'p50':v[math.ceil(len(v)*.50)-1],'p95':v[math.ceil(len(v)*.95)-1],'max':v[-1]}
result={'sources':[str(f) for f in a.logs],'limits':'Observed application timings only. CPU submission is not scanout. Missing data is null. First packet/frame/present events remain in the existing performance report.','metrics':{k:summary(windows.get(k)) for k in sorted(set(required)|set(windows))},'gallery_cumulative_counters':counters}
a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(result,indent=2));print(a.out)
