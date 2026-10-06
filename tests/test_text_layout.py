# ps5-native-app-boilerplate - Launcher localization and layout.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import json
import shutil
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]

class TextLayoutTests(unittest.TestCase):
    def test_real_font_metrics_and_complete_marquee_travel(self):
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        source = r'''
#include "ui/text_lane.hpp"
#include "ui/flow_layout.hpp"
#include <cassert>
#include <cmath>
int main() {
 using hui::ui::text_lane_offset;
 const auto items=hui::ui::flow_layout({10,20,350,0},{120,140,160,90,200},64,12);
 assert(items.size()==5);
 assert(items[0].x==10 && items[1].x==142 && items[2].x==10);
 assert(items[2].y==96 && items[4].y==172);
 for (unsigned i=0;i<items.size();++i) {
  assert(items[i].x>=10 && items[i].x+items[i].w<=360);
  for(unsigned j=0;j<i;++j) {
   const auto a=items[i],b=items[j];
   assert(a.x>=b.x+b.w || b.x>=a.x+a.w || a.y>=b.y+b.h || b.y>=a.y+a.h);
  }
 }
 assert(text_lane_offset(100,90,100)==0);
 assert(text_lane_offset(0,220,100)==0);
 assert(text_lane_offset(1.9,220,100)==0);
 assert(std::fabs(text_lane_offset(3,220,100)-24)<0.001);
 assert(text_lane_offset(7,220,100)==120);
 assert(text_lane_offset(8.9,220,100)==120);
 assert(std::fabs(text_lane_offset(10,220,100)-96)<0.001);
 assert(text_lane_offset(14,220,100)==0);
 for(int i=0;i<10000;++i) {
  float o=text_lane_offset(i/100.0f,220,100);
  assert(o>=0 && o<=120);
 }
}'''
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)/'test.cpp'; path.write_text(source)
            exe = Path(folder)/'test'
            subprocess.run([compiler,'-std=c++17','-I'+str(ROOT/'third_party/ps5-homebrew-ui'),str(path),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
        # Every prepared translation must have finite positive advances. These
        # are the exact metrics used for fit/scroll decisions by the renderer.
        data=(ROOT/'assets/fonts/inter-regular.huifont').read_bytes()
        pixel_size=struct.unpack_from('<f',data,12)[0]
        count=struct.unpack_from('<I',data,32)[0]
        advance={struct.unpack_from('<I',data,40+24*i)[0]:struct.unpack_from('<f',data,60+24*i)[0] for i in range(count)}
        for catalog in (ROOT/'assets/locales/rendered').glob('*.json'):
            for key,value in json.loads(catalog.read_text()).items():
                if '\n' in value: continue
                measured=sum(advance.get(ord(c),0) for c in value)*24/pixel_size
                self.assertGreaterEqual(measured,0,(catalog.name,key))
                self.assertLess(measured,100000,(catalog.name,key))

if __name__ == '__main__': unittest.main()
