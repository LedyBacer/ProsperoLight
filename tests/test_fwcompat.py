# ps5-native-app-boilerplate / ProsperoLight - Firmware startup experiment.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
"""Focused configuration, diagnostic persistence and matrix regression tests."""
from pathlib import Path
import importlib.util
import json
import shutil
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
def module(name,path):
 s=importlib.util.spec_from_file_location(name,ROOT/path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
class FWCompat(unittest.TestCase):
 def test_actual_preprocessor_configuration_and_production_defaults(self):
  compiler=shutil.which('clang++') or shutil.which('c++')
  variants=json.loads((ROOT/'tools/fwcompat/variants.json').read_text())
  self.assertEqual(len(variants),18)
  self.assertTrue(all(len(f"FWCompat {v['id']} 1234567")<=40 for v in variants))
  with tempfile.TemporaryDirectory() as d:
   source=Path(d)/'test.cpp';exe=Path(d)/'test'
   for v in [None,*variants]:
    values=[0,0,0,0,0,3,0,7,0,0] if v is None else [1,1-v['warmup'],1-v['preload'],1-v['cache'],v['force_1080p'],v.get('warmup_mask',3),v.get('agc_first',0),v.get('codec_mask',7),v.get('query_snapshots',0),v.get('compute_last',0)]
    names=['FWCOMPAT_DIAGNOSTICS','FWCOMPAT_SKIP_DECODER_WARMUP','FWCOMPAT_SKIP_VIDEO_MODULE_PRELOAD','FWCOMPAT_DISABLE_SHADER_CACHE','FWCOMPAT_FORCE_1080P','FWCOMPAT_WARMUP_MASK','FWCOMPAT_AGC_FIRST','FWCOMPAT_CODEC_MASK','FWCOMPAT_QUERY_SNAPSHOTS','FWCOMPAT_COMPUTE_LAST']
    source.write_text('#include "fwcompat.h"\n'+''.join(f'static_assert({n}=={x});\n' for n,x in zip(names,values))+'int main(){}\n')
    flags=[] if v is None else [f'-D{n}={x}' for n,x in zip(names,values)]
    subprocess.run([compiler,'-std=c++20','-I'+str(ROOT/'include'),*flags,str(source),'-o',str(exe)],check=True)
   bad=subprocess.run([compiler,'-std=c++20','-I'+str(ROOT/'include'),'-DFWCOMPAT_FORCE_1080P=1',str(source),'-o',str(exe)],capture_output=True)
   self.assertNotEqual(bad.returncode,0)
 def test_actual_warmup_query_subsets_order_and_errors(self):
  compiler=shutil.which('clang++') or shutil.which('c++')
  source=(ROOT/'src/moonlight_stream.cpp').read_text()
  start=source.index('extern "C" void moonlight_native_decoder_warmup(')
  function=source[start:source.index('\n}\n',start)+3]
  code=r'''#include "fwcompat.h"
#include <cassert>
#include <cstddef>
#define MOONLIGHT_VIDEO_CODEC_H264 1
#define MOONLIGHT_VIDEO_CODEC_HEVC 2
#define MOONLIGHT_STREAM_RESOLUTION_2160P 2160
#define DECODER_CPU_PRIORITY 777
namespace moonlight {constexpr unsigned kClassicDecoderCpuMask=123;}
struct videodec2_compute_memory_t {size_t size;};
struct videodec2_decoder_memory_t {size_t size;};
struct videodec2_decoder_config_t {
 size_t size;unsigned resource_type,codec_type,profile,max_level,max_width,max_height,
 max_dpb_frames,pipeline_depth,cpu_affinity,cpu_priority,optimize_progressive;
};
struct native_video_mode_t {unsigned codec_type,profile,max_level,max_width,max_height;};
static unsigned events[4],n,snapshots;
extern "C" void ps5_agc_fwcompat_query_snapshot(unsigned i) {assert(n && events[n-1]==i);++snapshots;}
static const native_video_mode_t *find_video_mode(int codec,int resolution,bool hdr) {
 assert(resolution==2160);static native_video_mode_t mode;
 mode={unsigned(codec),hdr?10u:8u,51,3840,2160};return &mode;
}
extern "C" void fwcompat_checkpoint(const char*,long long) {}
extern "C" void fwcompat_decoder_query(unsigned,int,int) {}
static int sceVideodec2QueryComputeMemoryInfo(videodec2_compute_memory_t *p) {
 assert(p->size==sizeof(*p));events[n++]=0;return 0;
}
static int sceVideodec2QueryDecoderMemoryInfo(videodec2_decoder_config_t *c,videodec2_decoder_memory_t *m) {
 assert(c->size==sizeof(*c) && m->size==sizeof(*m));
 assert(c->resource_type==1 && c->max_dpb_frames==4 && c->pipeline_depth==1);
 assert(c->max_width==3840 && c->max_height==2160 && c->max_level==51);
 assert(c->cpu_affinity==123 && c->cpu_priority==777 && c->optimize_progressive==1);
 unsigned i=c->codec_type==1?1:c->profile==8?2:3;events[n++]=i;
 return i==2?-77:0;
}
''' + function + r'''
int main() {
 int r[4]={-1,-1,-1,-1};moonlight_native_decoder_warmup(r);
 unsigned expected=0;
 if((FWCOMPAT_WARMUP_MASK&1) && !FWCOMPAT_COMPUTE_LAST) assert(events[expected++]==0);
 assert(r[0]==((FWCOMPAT_WARMUP_MASK&1)?0:-1));
 for(unsigned i=1;i<4;i++) {
  if((FWCOMPAT_WARMUP_MASK&2) && (FWCOMPAT_CODEC_MASK&(1u<<(i-1)))) {
   assert(events[expected++]==i);assert(r[i]==(i==2?-77:0));
  } else assert(r[i]==-1);
 }
 if((FWCOMPAT_WARMUP_MASK&1) && FWCOMPAT_COMPUTE_LAST) assert(events[expected++]==0);
 assert(n==expected && snapshots==(FWCOMPAT_QUERY_SNAPSHOTS?n:0));
 assert(bool(FWCOMPAT_LAUNCHER_ONLY)==(FWCOMPAT_WARMUP_MASK!=3 || FWCOMPAT_CODEC_MASK!=7 || FWCOMPAT_QUERY_SNAPSHOTS || FWCOMPAT_COMPUTE_LAST));
}
'''
  with tempfile.TemporaryDirectory() as d:
   p=Path(d);(p/'warmup.cpp').write_text(code)
   variants=json.loads((ROOT/'tools/fwcompat/variants.json').read_text())
   for v in [dict(warmup_mask=1),dict(warmup_mask=2),dict(warmup_mask=3),*variants[10:]]:
    mask=v.get('warmup_mask',3)
    extra=[f'-D{k}={v.get(field,default)}' for k,field,default in [('FWCOMPAT_CODEC_MASK','codec_mask',7),('FWCOMPAT_QUERY_SNAPSHOTS','query_snapshots',0),('FWCOMPAT_COMPUTE_LAST','compute_last',0),('FWCOMPAT_AGC_FIRST','agc_first',0)]]
    subprocess.run([compiler,'-std=c++20','-I'+str(ROOT/'include'),'-DFWCOMPAT_DIAGNOSTICS=1',f'-DFWCOMPAT_WARMUP_MASK={mask}',*extra,str(p/'warmup.cpp'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
 def test_matrix_groups_and_invalid_snapshot_configuration(self):
  import contextlib,io,sys
  source=(ROOT/'tools/build-fw-compat-matrix.sh').read_text()
  body=source[source.index('import json,sys\nwith open('):].split('\nPY',1)[0]
  old=sys.argv
  try:
   for group,expected in [('round3',['00-control','09-agc-first',*[v['id'] for v in json.loads((ROOT/'tools/fwcompat/variants.json').read_text())[10:]]]),('round2',['00-control','07-compute-only','08-codecs-only','09-agc-first']),('11-h264-only',['11-h264-only'])]:
    sys.argv=['select',group];out=io.StringIO()
    with contextlib.redirect_stdout(out):exec(body,{})
    self.assertEqual([line.split()[0] for line in out.getvalue().splitlines()],expected)
  finally:sys.argv=old
  compiler=shutil.which('clang++') or shutil.which('c++')
  for flags in (['-DFWCOMPAT_QUERY_SNAPSHOTS=1','-DFWCOMPAT_DIAGNOSTICS=1'],['-DFWCOMPAT_CODEC_MASK=1'],['-DFWCOMPAT_COMPUTE_LAST=1']):
   result=subprocess.run([compiler,'-x','c++','-std=c++20','-fsyntax-only','-I'+str(ROOT/'include'),*flags,'-'],input='#include "fwcompat.h"\n',text=True,capture_output=True)
   self.assertNotEqual(result.returncode,0)
 def test_synchronous_diagnostics_ignore_saved_debug_setting(self):
  compiler=shutil.which('clang++') or shutil.which('c++')
  with tempfile.TemporaryDirectory() as d:
   p=Path(d);src=p/'test.cpp';exe=p/'test'
   src.write_text('''#include "fwcompat.h"
#include <fcntl.h>
#include <unistd.h>
#include <cassert>
extern "C" int sceKernelDebugOutText(int,const char*) {return 0;}
int main(int,char **argv) {
 fwcompat_start();FWCOMPAT_POINT("early.begin",7);
 int fd=open(argv[1],O_CREAT|O_WRONLY,0600);fwcompat_attach(fd,argv[2]);
 int calls=0;int value=FWCOMPAT_CALL("probe",++calls);assert(value==1 && calls==1);
 FWCOMPAT_POINT("complete",3);fwcompat_gpu_failure();assert(fwcompat_gpu_failed());
 _exit(0); // No stdio flush or graceful destructors: checkpoint must already exist.
}''')
   subprocess.run([compiler,'-std=c++20','-pthread','-I'+str(ROOT/'include'),'-DFWCOMPAT_DIAGNOSTICS=1','-DFWCOMPAT_VARIANT_ID=01_no_warmup','-DPROSPEROLIGHT_PYROWAVE=1',str(src),str(ROOT/'src/fwcompat.cpp'),'-o',str(exe)],check=True)
   subprocess.run([str(exe),str(p/'log'),d],check=True)
   text=(p/'log').read_text();self.assertIn('early.begin',text);self.assertIn('variant=01-no-warmup',text);self.assertIn('complete',text)
   rows=[json.loads(line) for line in (p/'fwcompat-startup.jsonl').read_text().splitlines()]
   self.assertEqual(rows[0]['variant'],'01-no-warmup');self.assertEqual(rows[-1]['stage'],'complete')
 def test_module_skip_preserves_keyboard_mouse_and_process_lifetime(self):
  compiler=shutil.which('clang++') or shutil.which('c++')
  with tempfile.TemporaryDirectory() as d:
   p=Path(d);src=p/'test.cpp';exe=p/'test'
   src.write_text('''#include "fwcompat.h"
#include "native_modules.hpp"
#include <cassert>
unsigned loads=0;
extern "C" int sceSysmoduleLoadModule(unsigned id) {
 ++loads;assert(!FWCOMPAT_SKIP_VIDEO_MODULE_PRELOAD || id!=prosperolight::native_modules::video_decoder);return 0;
}
extern "C" void fwcompat_checkpoint(const char*,long long) {}
int main() {
 using namespace prosperolight::native_modules;
 PrepareBeforeStorage();PrepareBeforeStorage();
 assert(loads==(FWCOMPAT_SKIP_VIDEO_MODULE_PRELOAD?2:3));
 assert(Result(keyboard)==0 && Result(mouse)==0);
 assert(Result(video_decoder)==(FWCOMPAT_SKIP_VIDEO_MODULE_PRELOAD?-1:0));
}''')
   for skip in (0,1):
    subprocess.run([compiler,'-std=c++20','-I'+str(ROOT/'include'),'-DFWCOMPAT_DIAGNOSTICS=1',f'-DFWCOMPAT_SKIP_VIDEO_MODULE_PRELOAD={skip}',str(src),str(ROOT/'src/native_modules.cpp'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
 def test_ui_generated_only_and_calls_wrapped(self):
  m=module('fwui','tools/fwcompat-instrument-ui.py')
  text='if (!eglInitialize(display_, &major, &minor)) return false;'
  wrapped=m.instrument(text,('eglInitialize',),'egl')
  self.assertIn('FWCOMPAT_CALL("egl.eglInitialize", eglInitialize(display_, &major, &minor))',wrapped)
  source=(ROOT/'tools/build.sh').read_text();self.assertIn('compile_source="$generated"',source)
  self.assertIn('FWCOMPAT_DIAGNOSTICS:-0',source)
 def test_original_release_build_id_note_is_checked(self):
  import struct
  compare=module('codecompare','tools/fwcompat/compare-release-code.py')
  data=bytearray(256)
  struct.pack_into('<Q',data,32,64)
  struct.pack_into('<HH',data,54,56,1)
  struct.pack_into('<IIQQQQQQ',data,64,4,0,120,0,0,36,36,4)
  struct.pack_into('<III',data,120,4,20,3)
  data[132:136]=b'GNU\0'
  fingerprint='c42ff125149aa6fde1bbd8b08009eeee00000000'
  data[136:156]=bytes.fromhex(fingerprint)
  self.assertEqual(compare.build_id(data),fingerprint)
  data[132]=ord('X')
  with self.assertRaises(ValueError):compare.build_id(data)
 def test_sdk_source_patch_cannot_silently_target_enclosing_git_checkout(self):
  import difflib
  sdk=module('sdkbuild','tools/fwcompat/build-sdk.py')
  with tempfile.TemporaryDirectory() as d:
   parent=Path(d);subprocess.run(['git','init','-q',d],check=True)
   source=parent/'results/source';source.mkdir(parents=True)
   (source/'a.c').write_text('unsafe\n')
   patch=parent/'change.patch'
   patch.write_text(''.join(difflib.unified_diff(['unsafe\n'],['guarded\n'],fromfile='a/a.c',tofile='b/a.c')))
   sdk.apply_patch(source,patch)
   self.assertEqual((source/'a.c').read_text(),'guarded\n')
   self.assertFalse((parent/'a.c').exists())
 def test_sdk_guard_and_real_callsites(self):
  patch=(ROOT/'tools/fwcompat/opengl-1.0.2.patch').read_text()
  self.assertIn('+    if (!defaults) {',patch)
  self.assertIn('+        init_rc = fwcompat_agc_init(agc.init, 8);',patch)
  self.assertIn('fwcompat_agc_defaults(agc->get_defaults)',patch)
  self.assertIn('fwcompat_agc_defaults(agc.get_defaults)',patch)
  self.assertNotIn('get_defaults(8)',patch)
 def test_readiness_requires_render_swap_and_no_gpu_failure(self):
  s=(ROOT/'src/launcher/launcher_ps5.cpp').read_text()
  self.assertLess(s.index('error != GL_NO_ERROR || fwcompat_gpu_failed()'),s.index('RESULT=LAUNCHER_READY'))
  self.assertLess(s.index('if (!swapped)'),s.index('RESULT=LAUNCHER_READY'))
  self.assertIn('if (FWCOMPAT_DIAGNOSTICS)\n        return false;',s)
if __name__=='__main__':unittest.main()
