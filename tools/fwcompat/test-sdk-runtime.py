#!/usr/bin/env python3
# ps5-native-app-boilerplate / ProsperoLight - Firmware startup experiment.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
"""Execute actual patched target-state code on the host, including NULL failure."""
from pathlib import Path
import subprocess
import sys
import tempfile
source=Path(sys.argv[1]).read_text()
start=source.index('static int append_target_state(')
end=source.index('\n}\n',start)+3
function=source[start:end]
code=r'''
#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>
#define DISPLAY_WIDTH 1920
#define DISPLAY_HEIGHT 1080
static int failed;
void fwcompat_checkpoint(const char *s, long long n) {(void)s;(void)n;}
void fwcompat_gpu_failure(void) {failed=1;}
typedef struct {uint16_t offset,reserved;uint32_t value;} agc_register_t;
static uint32_t float_bits(float f) {uint32_t n;memcpy(&n,&f,4);return n;}
''' + function + r'''
int main(void) {
 agc_register_t rows[16],out[128]; unsigned count=0;
 assert(append_target_state(out,&count,0,0)==-1 && failed && count==0);
 struct {void *blocks; unsigned char pad[24]; uint32_t count;} d={0,{0},16};
 failed=0;assert(append_target_state(out,&count,&d,0)==-1 && failed);
 agc_register_t *blocks[]={rows};d.blocks=blocks;
 const uint16_t offsets[]={0x318,0x31b,0x31c,0x31d,0x31e,0x31f,0x321,0x323,0x324,0x325,0x390,0x398,0x3a0,0x3a8,0x3b0,0x3b8};
 for(unsigned i=0;i<16;i++) rows[i]=(agc_register_t){offsets[i],0,i};
 failed=0;assert(append_target_state(out,&count,&d,(void *)0x200000000ull)==0 && !failed && count>=16);
 rows[15].offset=0xffff;failed=0;
 assert(append_target_state(out,&count,&d,0)==-1 && failed);
 return 0;
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(code)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('PASS actual SDK target-state: NULL/default-block/missing-register failure and valid table')

# Compile the actual early hooks and actual later runtime state branch together.
helpers=source[source.index('static atomic_uint fwcompat_agc_init_calls;'):source.index('static _Thread_local uint32_t runtime_ngg_ge_pc_alloc;')]
hooks=source[source.index('static int fwcompat_early_initialized;'):source.index('/* End FWCompat round 2 hooks. */')]
hooks=hooks.removesuffix('#endif\n')
branch_start=source.index('    if (runtime_agc_initialized) {\n        static atomic_uint reuse_reports;')
branch=source[branch_start:source.index('\n#else',branch_start)]
code=r'''
#include <assert.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stddef.h>
#include <string.h>
static int failed, calls, getters, load_error, init_result, reused=-1;
static void *defaults=(void *)1;
static char snapshot_stage[128];static long long snapshot_value;
static int runtime_agc_initialized;
void fwcompat_checkpoint(const char *s,long long n) {
 if (!strcmp(s,"agc.runtime.reused_early")) reused=(int)n;
 if (!strncmp(s,"agc.after_query.",16)) {strcpy(snapshot_stage,s);snapshot_value=n;}
}
void fwcompat_gpu_failure(void) {failed=1;}
static int stub_init(uint32_t v) {assert(v==8);calls++;return init_result;}
static void *stub_defaults(void) {getters++;return defaults;}
typedef struct {int (*init)(uint32_t);void *(*get_defaults)(void);} agc_api_t;
typedef struct {int unused;} video_api_t;
static int load_apis(void *a,void *b,void *c,agc_api_t *agc,video_api_t *v) {
 (void)a;(void)b;(void)c;(void)v;agc->init=stub_init;agc->get_defaults=stub_defaults;return load_error;
}
''' + helpers + hooks + '\nstatic int later(void) { agc_api_t agc={stub_init,stub_defaults}; int init_rc;\n' + branch + r'''
return init_rc;
}
int main(void) {
 assert(calls==0 && getters==0); // Control has not invoked the hooks.
 assert(later()==0 && calls==1 && getters==0 && reused==-1);
 runtime_agc_initialized=0;calls=0;
 assert(ps5_agc_fwcompat_early_init()==0 && runtime_agc_initialized && calls==1 && getters==1);
 ps5_agc_fwcompat_after_warmup();assert(!failed && getters==2);
 assert(later()==0 && calls==1 && reused==1);
 defaults=0;ps5_agc_fwcompat_after_warmup();assert(failed && calls==1);
 runtime_agc_initialized=0;fwcompat_early_initialized=0;failed=0;getters=0;
 init_result=(int)0x8a6c0004u;
 assert(ps5_agc_fwcompat_early_init()==init_result && failed && !runtime_agc_initialized && getters==0);
 init_result=0;failed=0;assert(ps5_agc_fwcompat_early_init()==0 && failed && runtime_agc_initialized);
 int init_before=calls;int getter_before=getters;
 defaults=(void *)1;failed=0;
 const char *names[]={"agc.after_query.compute","agc.after_query.h264_sdr","agc.after_query.hevc_sdr","agc.after_query.hevc_main10_hdr"};
 for(unsigned i=0;i<4;i++) {ps5_agc_fwcompat_query_snapshot(i);assert(!strcmp(snapshot_stage,names[i]) && snapshot_value==1);}
 assert(!failed && calls==init_before && getters==getter_before+4);
 defaults=0;ps5_agc_fwcompat_query_snapshot(2);assert(failed && calls==init_before && snapshot_value==0 && !strcmp(snapshot_stage,names[2]));
 getter_before=getters;ps5_agc_fwcompat_query_snapshot(99);assert(getters==getter_before);
 runtime_agc_initialized=0;fwcompat_early_initialized=0;failed=0;
 ps5_agc_fwcompat_query_snapshot(0);assert(failed && getters==getter_before);
 runtime_agc_initialized=0;failed=0;load_error=-1;int before=calls;
 assert(ps5_agc_fwcompat_early_init()==-1 && failed && calls==before);
 return 0;
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'hooks.c').write_text(code)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'hooks.c'),'-o',str(p/'hooks')],check=True)
 subprocess.run([str(p/'hooks')],check=True)
print('PASS actual SDK early hooks and runtime reuse: one init, raw failure, NULL before/after warmup, API failure, control')
