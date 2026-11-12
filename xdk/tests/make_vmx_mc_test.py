"""Build host arithmetic/access fixtures from actual kernels and FFmpeg 1.2 C.

No host timings are emitted. This model does not execute VMX128 instructions.
"""
from pathlib import Path
import argparse
import json
import hashlib

ROOT=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser()
parser.add_argument('output',type=Path)
args=parser.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
ff=ROOT/'xdk/deps/XBMC-360/libraries/ffmpeg'
dsp=ROOT/'xdk/ffmpeg_xbox'
common='#include <stddef.h>\n#include <stdint.h>\n#include <assert.h>\n#include "vmx_ab.h"\n'

# Compile the original C template bodies, not an independently reimplemented
# H.264 oracle. The only configuration difference is desktop endianness.
(out/'libavutil').mkdir(exist_ok=True)
(out/'libavutil/avconfig.h').write_text('#define AV_HAVE_BIGENDIAN 0\n#define AV_HAVE_FAST_UNALIGNED 0\n#define AV_HAVE_INCOMPATIBLE_LIBAV_ABI 0\n')
header=(ff/'libavcodec/dsputil.h').read_text()
macro=header[header.index('#define CALL_2X_PIXELS'):header.index('/* motion estimation */')]
ref=common+'#define BIT_DEPTH 8\n#define pixeltmp int16_t\n'+macro
ref+='#include "'+(ff/'libavcodec/h264qpel_template.c').as_posix()+'"\n'
for size in (8,16):
    for op in ('put','avg'):ref+=f'#undef {op}_h264_qpel{size}_mc00_8_c\n'
ref+='#include "'+(ff/'libavcodec/h264chroma_template.c').as_posix()+'"\n'
ref+='void model_reference_tables(struct vmx_ab_tables *t) {\n'
for s,n in enumerate((16,8)):
    for op,label in enumerate(('put','avg')):
        for pos in range(16):ref+=f't->qpel[{s}][{op}][{pos}]={label}_h264_qpel{n}_mc{pos%4}{pos//4}_8_c;\n'
for s,n in enumerate((8,4)):
    for op,label in enumerate(('put','avg')):ref+=f't->chroma[{s}][{op}]={label}_h264_chroma_mc{n}_8_c;\n'
ref+='}\n';(out/'reference.cpp').write_text(ref)

source=(dsp/'h264_vmx.c').read_text()
# The model helper defines these names already: use native store bodies here.
moon=common+'#define store16 unused_idct_store16\n#define store_n unused_idct_store_n\n#include "vmx_mc_emulation.h"\n#undef store16\n#undef store_n\n'
moon+='typedef uintptr_t UINT_PTR;\ntypedef vmx_ab_qpel_fn qpel_mc_func;\ntypedef vmx_ab_chroma_fn h264_chroma_mc_func;\n'
moon+=source[source.index('/* ------------------------------------------------------------ stores */'):source.index('static __forceinline void clear_block16')]
moon+='void model_moon_tables(struct vmx_ab_tables *t){memcpy(t->qpel,vmx_qpel,sizeof(t->qpel));memcpy(t->chroma,vmx_chroma,sizeof(t->chroma));}\n'
(out/'moon.cpp').write_text(moon)

# Wrappers only establish instrumentation scopes. Each operation calls the
# unchanged generated C reference, extracted Moon kernel, or adapted kernel.
main=common+'#include <stdio.h>\n#include <string.h>\n#include <stdlib.h>\n'
main+='uintptr_t mc_source,mc_destination;int mc_stride,mc_width,mc_height,mc_variant;\n'
main+='unsigned long long mc_reads[3],mc_writes[3],mc_scope_calls[3];\n'
main+='void mc_scope(uint8_t*d,uint8_t*s,int stride,int width,int height,int variant){mc_source=(uintptr_t)s;mc_destination=(uintptr_t)d;mc_stride=stride;mc_width=width;mc_height=height;mc_variant=variant;mc_scope_calls[variant]++;}\n'
main+='void model_reference_tables(struct vmx_ab_tables*);void model_moon_tables(struct vmx_ab_tables*);\n'
main+='static struct vmx_ab_tables raw[3];\n'
for v in (1,2):
    for s,n in enumerate((16,8)):
        for op in range(2):
            for pos in range(16):
                main+=f'static void q{v}_{s}_{op}_{pos}(uint8_t*d,uint8_t*s,int stride){{mc_scope(d,s,stride,{n},{n},{v});raw[{v}].qpel[{s}][{op}][{pos}](d,s,stride);}}\n'
    for s,n in enumerate((8,4)):
        for op in range(2):main+=f'static void c{v}_{s}_{op}(uint8_t*d,uint8_t*s,int stride,int h,int x,int y){{mc_scope(d,s,stride,{n},h,{v});raw[{v}].chroma[{s}][{op}](d,s,stride,h,x,y);}}\n'
main+='extern "C" void ff_xbox_vmx_ab_tables(struct vmx_ab_tables*r,struct vmx_ab_tables*m){model_reference_tables(r);model_moon_tables(raw+1);\n'
for s in range(2):
    for op in range(2):
        for pos in range(16):main+=f'm->qpel[{s}][{op}][{pos}]=q1_{s}_{op}_{pos};\n'
        main+=f'm->chroma[{s}][{op}]=c1_{s}_{op};\n'
main+='}\nvoid raw_multiplex_qpel_tables(struct vmx_ab_tables*);void raw_multiplex_chroma_tables(struct vmx_ab_tables*);\n'
for kind in ('qpel','chroma'):
    main+=f'extern "C" void ff_multiplex_{kind}_tables(struct vmx_ab_tables*t){{raw_multiplex_{kind}_tables(raw+2);\n'
    for s in range(2):
        for op in range(2):
            if kind=='qpel':
                for pos in range(16):main+=f't->qpel[{s}][{op}][{pos}]=q2_{s}_{op}_{pos};\n'
            else:main+=f't->chroma[{s}][{op}]=c2_{s}_{op};\n'
    main+='}\n'
main+='static int output(void*,const char*s){return fputs(s,stdout)>=0;}\n'
main+='void model_negative_probe();\nint main(int argc,char**){if(argc>1){model_negative_probe();return 99;}vmx_ab_callbacks cb={};cb.integrity_only=1;cb.emit=output;int r=ff_xbox_vmx_ab_run(&cb);for(int v=1;v<3;v++)printf("# host_model_only,variant=%d,calls=%llu,vector_read_bytes=%llu,vector_store_bytes=%llu\\n",v,mc_scope_calls[v],mc_reads[v],mc_writes[v]);return r;}\n'
(out/'main.cpp').write_text(main)
negative=common+'#include "vmx_mc_emulation.h"\nvoid model_negative_probe(){__declspec(align(16)) uint8_t dst[96]={},src[96]={};mc_scope(dst+16,src+16,32,8,1,2);__vector4 unchanged=mc_load(dst+16);__stvx(unchanged,dst+16,0);}\n'
(out/'negative.cpp').write_text(negative)
for kind in ('qpel','chroma'):
    p=dsp/('multiplex_'+kind+'.c')
    text=f'#define ff_multiplex_{kind}_tables raw_multiplex_{kind}_tables\n#include "{p.as_posix()}"\n'
    # vmx_ab.h declares C linkage under the renamed name; match that in main.
    (out/('multiplex_'+kind+'.cpp')).write_text(text)
main=(out/'main.cpp').read_text().replace('void raw_multiplex_qpel_tables','extern "C" void raw_multiplex_qpel_tables').replace('void raw_multiplex_chroma_tables','extern "C" void raw_multiplex_chroma_tables')
(out/'main.cpp').write_text(main)
inputs=[dsp/p for p in ('h264_vmx.c','multiplex_qpel.c','multiplex_chroma.c','vmx_ab.c','vmx_ab_store.h')]
inputs += [ff/'libavcodec'/p for p in ('h264qpel_template.c','h264chroma_template.c','hpel_template.c','bit_depth_template.c')]
(out/'source-hashes.json').write_text(json.dumps({str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs},indent=2))
print('Generated actual C/kernel arithmetic fixtures in',out)
