// ps5-native-app-boilerplate - shared deterministic preallocated stimulus patterns.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
(function(root) {
'use strict';
function fillNoise(data, initialSeed) {
  let seed=initialSeed>>>0;
  for(let at=0;at<data.length;at+=4) {
    seed^=seed<<13;seed^=seed>>>17;seed^=seed<<5;
    const shade=32+(seed>>>25);
    data[at]=shade;data[at+1]=shade;data[at+2]=shade;data[at+3]=255;
  }
}
const api={fillNoise};
if(typeof module!=='undefined'&&module.exports) module.exports=api;
root.PLTestPatterns=api;
})(typeof globalThis!=='undefined'?globalThis:this);
