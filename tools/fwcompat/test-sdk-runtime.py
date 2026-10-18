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
