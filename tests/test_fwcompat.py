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
  self.assertEqual(len(variants),7)
  with tempfile.TemporaryDirectory() as d:
   source=Path(d)/'test.cpp';exe=Path(d)/'test'
   for v in [None,*variants]:
    values=[0,0,0,0,0] if v is None else [1,1-v['warmup'],1-v['preload'],1-v['cache'],v['force_1080p']]
    names=['FWCOMPAT_DIAGNOSTICS','FWCOMPAT_SKIP_DECODER_WARMUP','FWCOMPAT_SKIP_VIDEO_MODULE_PRELOAD','FWCOMPAT_DISABLE_SHADER_CACHE','FWCOMPAT_FORCE_1080P']
    source.write_text('#include "fwcompat.h"\n'+''.join(f'static_assert({n}=={x});\n' for n,x in zip(names,values))+'int main(){}\n')
    flags=[] if v is None else [f'-D{n}={x}' for n,x in zip(names,values)]
    subprocess.run([compiler,'-std=c++20','-I'+str(ROOT/'include'),*flags,str(source),'-o',str(exe)],check=True)
   bad=subprocess.run([compiler,'-std=c++20','-I'+str(ROOT/'include'),'-DFWCOMPAT_FORCE_1080P=1',str(source),'-o',str(exe)],capture_output=True)
   self.assertNotEqual(bad.returncode,0)
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
