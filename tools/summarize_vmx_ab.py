"""Summarize native VMX A/B CSVs, or emit a truthful unmeasured result template.

Only complete Xbox-native runs are accepted for timings. Host arithmetic logs
must not be passed as measurements. Raw batch-mean times are primary.
"""
import argparse
import csv
import json
import statistics
import math
from pathlib import Path


def routines():
    for kind in ('qpel','chroma'):
        for size,width in enumerate((16,8) if kind=='qpel' else (8,4)):
            for avg in range(2):
                for pos in range(16 if kind=='qpel' else 64):
                    ident=(size*2+avg)*16+pos if kind=='qpel' else 64+(size*2+avg)*64+pos
                    yield {'id':ident,'routine':kind,'width':width,'op':'AVG' if avg else 'PUT',
                           'x':pos%(4 if kind=='qpel' else 8),'y':pos//(4 if kind=='qpel' else 8)}


def read_native(path):
    text=path.read_text(encoding='utf-8-sig')
    lines=text.splitlines()
    if '# environment=Xbox360_native;' not in text or '# host_model_only' in text:
        raise ValueError(str(path)+': requires native Xbox evidence, not host emulation')
    if '# run_complete=1,result=0;' not in text or '# integrity_complete=1' not in text:
        raise ValueError(str(path)+': incomplete, cancelled, I/O-failed or mismatching run')
    checks=[row for row in lines if row.startswith('CHECK,')]
    expected_checks={f'CHECK,{ident},{variant},PASS' for ident in range(320) for variant in range(3)}
    if len(checks)!=960 or set(checks)!=expected_checks:
        raise ValueError(str(path)+': incomplete correctness matrix')
    header=next(row for row in lines if row.startswith('kind,id,variant,'))
    measurements=list(csv.DictReader([header]+[row for row in lines if row.startswith('TIME,')]))
    if len(measurements)!=7680:
        raise ValueError(str(path)+': incomplete timing matrix')
    key=lambda r:(int(r['id']),int(r['variant_id']),int(r['stride']),int(r['src_alignment']),int(r['dst_alignment']))
    indexed={key(row):row for row in measurements}
    if len(indexed)!=len(measurements):raise ValueError(str(path)+': duplicated timing cells')
    spec={row['id']:row for row in routines()}
    expected=set()
    for ident,routine in spec.items():
        shifted_dst=(8 if routine['routine']=='chroma' else routine['width'])&15
        for variant in range(3):
            for stride in (64,80,96,128):
                expected.add((ident,variant,stride,0,0));expected.add((ident,variant,stride,7,shifted_dst))
    if set(indexed)!=expected:raise ValueError(str(path)+': wrong timing dimensions or alignments')
    for item in measurements:
        times=[float(item[key]) for key in ('p50_raw_ns','p95_raw_ns','p99_raw_ns')]
        if not all(math.isfinite(value) and value>0 for value in times) or times!=sorted(times):
            raise ValueError(str(path)+': invalid raw timing percentiles')
        if int(item['samples'])!=31 or int(item['ops_per_sample'])!=64:
            raise ValueError(str(path)+': mismatched sample budget')
    return measurements


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('reports',nargs='*',type=Path)
    p.add_argument('--output',required=True,type=Path)
    args=p.parse_args()
    data=[read_native(path) for path in args.reports]
    table=[]
    for routine in routines():
        row=dict(routine,hardware_runs=len(data),c_p50_ns=None,moon_p50_ns=None,multiplex_p50_ns=None,
                 c_p99_ns=None,moon_p99_ns=None,multiplex_p99_ns=None,gain_vs_moon_pct=None,
                 decision='KEEP_CURRENT_PENDING_HARDWARE' if not data else 'EXPERIMENTAL_NEEDS_MATCHED_STREAMING')
        if data:
            values={v:[] for v in range(3)}
            for run in data:
                for item in run:
                    if int(item['id'])==row['id']:values[int(item['variant_id'])].append(item)
            for variant,name in enumerate(('c','moon','multiplex')):
                row[name+'_p50_ns']=statistics.median(float(item['p50_raw_ns']) for item in values[variant])
                row[name+'_p99_ns']=max(float(item['p99_raw_ns']) for item in values[variant])
            moon=row['moon_p50_ns'];multi=row['multiplex_p50_ns']
            row['gain_vs_moon_pct']=(moon-multi)*100/moon if moon else None
            # Each stride/alignment/run must improve; aggregation cannot hide
            # a loss in one cell or a worse tail. Never auto-install a routine.
            superior=len(data)>=3
            for run in data:
                cells={(int(item['variant_id']),int(item['stride']),int(item['src_alignment']),int(item['dst_alignment'])):item
                       for item in run if int(item['id'])==row['id']}
                for key,item in cells.items():
                    if key[0]!=2:continue
                    original=cells[(1,)+key[1:]]
                    if float(item['p99_raw_ns'])>float(original['p99_raw_ns']):row['decision']='KEEP_CURRENT_TAIL_LOSS';superior=False;break
                    if float(item['p50_raw_ns'])>=float(original['p50_raw_ns']):superior=False
                if row['decision']=='KEEP_CURRENT_TAIL_LOSS':break
            if superior:row['decision']='MICROBENCH_CANDIDATE_PENDING_STREAMING_AND_NOISE_REVIEW'
        table.append(row)
    args.output.mkdir(parents=True,exist_ok=True)
    with (args.output/'routine-results.csv').open('w',newline='',encoding='utf-8') as file:
        writer=csv.DictWriter(file,fieldnames=list(table[0]));writer.writeheader();writer.writerows(table)
    (args.output/'results.json').write_text(json.dumps({'hardware_runs':len(data),'reports':[str(p) for p in args.reports],
        'measurement':'raw CPU QPC, 31 batch means of 64 operations; not single-block tails, GPU time or scanout',
        'streaming_candidate_installed':False,'routines':table},indent=2)+'\n')
    print('Wrote',len(table),'routine rows;',len(data),'native runs; no streaming decision automatically applied.')


if __name__=='__main__':main()
