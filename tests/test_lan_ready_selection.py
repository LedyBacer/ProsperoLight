# Copyright (C) 2026 LedyBacer and contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Real queue/policy replay: recover a stale startup phase without a FIFO tax."""
from pathlib import Path
import subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
class LanSelection(unittest.TestCase):
    def test_bounded_selection_and_surface_ownership(self):
        source=r'''
#include "moonlight_pipeline.hpp"
#include <cassert>
#include <set>
int main() {
 using namespace moonlight;
 assert(ready_capacity(1,60,5994,false,0)==1);
 assert(ready_capacity(1,60,5994,false,1000)==2);
 assert(ready_capacity(1,60,5994,true,0)==2);
 assert(ready_capacity(1,90,11988,false,0)==2);
 assert(ready_capacity(2,60,5994,false,0)==2);
 ReadyMailbox<int> q;q.capacity=2;int out=0;std::set<int> retired;
 assert(!q.publish(1,&out));assert(!q.publish(2,&out));
 assert(q.limit(1,&out));assert(out==1);assert(retired.insert(out).second);
 assert(q.publish(3,&out));assert(out==2);assert(retired.insert(out).second);
 assert(q.take(&out)&&out==3);assert(retired.insert(out).second);
 assert(!q.full&&!q.second&&retired.size()==3);
 q.limit(2,&out);assert(!q.publish(4,&out));assert(!q.publish(5,&out));
 assert(q.take(&out)&&out==4);assert(q.take(&out)&&out==5);
 // Source ready at 6 ms missed its earliest 16.7 ms slot due to startup.
 // Blind FIFO submits it AFTER that flip and sustains one-frame lag forever.
 uint64_t flip=16683,now=flip+100,old_ready=6000,next_ready=22683;
 auto limit=lan_selection_deadline(now,old_ready,flip,5994,1000);
 assert(limit>next_ready && limit<flip+16683);
 // A real successor before that bound replaces old content and makes the
 // next 33.4 ms flip. The following pictures all follow the same fresh phase.
 unsigned drops=0;
 for(unsigned i=0;i<600;++i) {
   if(old_ready<flip) {
     limit=lan_selection_deadline(now,old_ready,flip,5994,1000);
     assert(next_ready<limit);
     old_ready=next_ready;++drops;
   }
   uint64_t shown=((old_ready/16683)+1)*16683;
   assert(shown-old_ready<=16683);
   flip=shown;now=shown+100;old_ready+=16683;next_ready=old_ready+16683;
 }
 assert(drops==1); // phase recovery, not continuous frame dropping
 // If no successor arrives, submit the retained picture before its next
 // legal slot; an idle desktop must not be blackholed or delayed unboundedly.
 limit=lan_selection_deadline(16783,6000,16683,5994,1000);
 assert(limit<=32366 && limit>=16783);
 assert(lan_selection_deadline(22000,21900,16683,5994,1000)==22000);
 assert(lan_selection_deadline(22000,6000,0,5994,1000)==22000);
 assert(lan_selection_deadline(22000,6000,16683,0,1000)==22000);
}
'''
        with tempfile.TemporaryDirectory() as d:
            p=Path(d);(p/'selection.cpp').write_text(source)
            subprocess.run(['c++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-I'+str(ROOT/'include'),str(p/'selection.cpp'),'-o',str(p/'selection')],check=True)
            subprocess.run([str(p/'selection')],check=True)
if __name__=='__main__':unittest.main()
