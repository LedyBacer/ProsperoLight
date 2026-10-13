// ps5-native-app-boilerplate - deterministic GPU-only high entropy workload.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';
class PLTestGpuScene {
  constructor(canvas,settings) {
    this.gl=canvas.getContext('webgl2',{alpha:false,antialias:false,preserveDrawingBuffer:false});
    if(!this.gl)throw Error('UNSUPPORTED_WEBGL2');
    this.settings=settings;this.draws=0;this.lastUpdate=-1;
    const gl=this.gl;
    const shader=(type,source)=>{
      const value=gl.createShader(type);gl.shaderSource(value,source);gl.compileShader(value);
      if(!gl.getShaderParameter(value,gl.COMPILE_STATUS)){gl.deleteShader(value);throw Error('UNSUPPORTED_WEBGL2_SHADER');}
      return value;
    };
    const vertex=shader(gl.VERTEX_SHADER,`#version 300 es
    void main(){vec2 p=vec2(float((gl_VertexID<<1)&2),float(gl_VertexID&2));gl_Position=vec4(p*2.0-1.0,0.0,1.0);}`);
    const fragment=shader(gl.FRAGMENT_SHADER,`#version 300 es
    precision highp float;precision highp int;
    uniform uint seed;uniform uint frame;uniform float entropy;uniform float movement;
    out vec4 color;
    uint hash(uint v){v^=v>>16;v*=0x7feb352du;v^=v>>15;v*=0x846ca68bu;return v^(v>>16);}
    void main(){uvec2 p=uvec2(gl_FragCoord.xy)+uvec2(uint(float(frame)*movement),0u);
      uint h=hash(p.x^(p.y*0x9e3779b9u)^seed^(frame*0x85ebca6bu));
      vec3 noise=vec3(float(h&255u),float((h>>8)&255u),float((h>>16)&255u))/255.0;
      vec3 base=vec3(0.15+0.7*float((p.x/64u+p.y/64u)&1u));
      color=vec4(mix(base,noise,entropy),1.0);}`);
    this.program=gl.createProgram();gl.attachShader(this.program,vertex);gl.attachShader(this.program,fragment);gl.linkProgram(this.program);
    gl.deleteShader(vertex);gl.deleteShader(fragment);
    if(!gl.getProgramParameter(this.program,gl.LINK_STATUS))throw Error('UNSUPPORTED_WEBGL2_PROGRAM');
    gl.useProgram(this.program);this.vao=gl.createVertexArray();gl.bindVertexArray(this.vao);
    this.frameLocation=gl.getUniformLocation(this.program,'frame');
    gl.uniform1ui(gl.getUniformLocation(this.program,'seed'),settings.seed>>>0);
    this.entropyLocation=gl.getUniformLocation(this.program,'entropy');
    gl.uniform1f(this.entropyLocation,settings.entropy??1);
    gl.uniform1f(gl.getUniformLocation(this.program,'movement'),settings.movement??1);
    gl.viewport(0,0,canvas.width,canvas.height);
    canvas.addEventListener('webglcontextlost',event=>{event.preventDefault();finish('BROWSER_GPU_CONTEXT_LOST');});
  }
  draw(elapsed,entropy) {
    this.gl.uniform1f(this.entropyLocation,entropy??this.settings.entropy??1);
    const index=Math.floor(elapsed*(this.settings.update_hz??this.settings.fps));
    // Drawing every rAF preserves the non-preserved backbuffer. Content changes
    // only at the configured update rate, independent of the display rAF rate.
    this.gl.uniform1ui(this.frameLocation,index>>>0);this.gl.drawArrays(this.gl.TRIANGLES,0,3);
    this.draws++;this.lastUpdate=index;
    if(this.gl.isContextLost())throw Error('BROWSER_GPU_CONTEXT_LOST');
  }
  sample() {
    const bytes=new Uint8Array(64*64*4);
    this.gl.readPixels(0,0,64,64,this.gl.RGBA,this.gl.UNSIGNED_BYTE,bytes);
    return bytes; // preparation only; never read back frames in the rAF loop
  }
  statistics() {return {version:'webgl2-hash/2',phase_program:this.settings.scene==='decoder-regression'?'low entropy 20s / full entropy 40s / recovery 20s':null,supported:true,draw_calls:this.draws,
    content_update_hz:this.settings.update_hz??this.settings.fps,entropy:this.settings.entropy??1,
    movement:this.settings.movement??1,width:this.gl.drawingBufferWidth,height:this.gl.drawingBufferHeight};}
}
