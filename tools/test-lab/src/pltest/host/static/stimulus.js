// ps5-native-app-boilerplate - measured browser Ready and deterministic workload.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';
let canvas=document.querySelector('#scene'),ctx=canvas.getContext('2d',{alpha:false}),gpuScene=null;
const status=document.querySelector('#status'),button=document.querySelector('#ready');
const socket=new WebSocket(`ws://${location.host}/page`);
let settings=null,textures=[],simple=null,prepared=false,running=false,report=null,start=0,last=0,count=0;
let intervals=null,hidden=0,fullscreenLost=0,lastPulse=0,preparedHash=null,preflightFrames=[];
let pendingReady=false,frameNumber=0,measureStarted=0,startCommand=null,phaseEvents=[];
function snapshot() {
  const elapsed=preflightFrames.length>1?preflightFrames.at(-1)-preflightFrames[0]:0;
  return {visibility:document.visibilityState,focused:document.hasFocus(),fullscreen:!!document.fullscreenElement,
    raf_fps:elapsed?(preflightFrames.length-1)*1000/elapsed:0,dpr:devicePixelRatio,
    screen_x:window.screenX,screen_y:window.screenY,outer_width:outerWidth,outer_height:outerHeight,
    screen_width:screen.width,screen_height:screen.height};
}
function send(data){if(socket.readyState===WebSocket.OPEN)socket.send(JSON.stringify(data));}
async function prepare(s) {
  if(running)throw Error('run already active');
  settings=s;prepared=false;button.disabled=true;preflightFrames=[];pendingReady=false;
  document.title='ProsperoLight Test Lab '+s.run_id;
  const fresh=canvas.cloneNode(false);canvas.replaceWith(fresh);canvas=fresh;
  canvas.width=s.render_width??s.width;canvas.height=s.render_height??s.height;textures=[];gpuScene=null;
  intervals=new Float64Array(Math.min(250000,Math.ceil((s.seconds+s.warmup_seconds+5)*Math.max(240,s.display_refresh_hz||0))));
  if(s.scene==='gpu-entropy'||s.workload_backend==='webgl2') {
    gpuScene=new PLTestGpuScene(canvas,s);ctx=null;gpuScene.draw(0);
    const bytes=await crypto.subtle.digest('SHA-256',gpuScene.sample());
    preparedHash=Array.from(new Uint8Array(bytes),v=>v.toString(16).padStart(2,'0')).join('');
  } else {ctx=canvas.getContext('2d',{alpha:false});
  for(let n=0;n<4;n++) {
    const t=document.createElement('canvas');t.width=s.width;t.height=s.height;
    const image=t.getContext('2d').createImageData(s.width,s.height);
    PLTestPatterns.fillNoise(image.data,s.seed+n);t.getContext('2d').putImageData(image,0,0);textures.push(t);
    if(n===0) {
      const bytes=await crypto.subtle.digest('SHA-256',image.data.slice(0,65536));
      preparedHash=Array.from(new Uint8Array(bytes),v=>v.toString(16).padStart(2,'0')).join('');
    }
    await new Promise(r=>setTimeout(r,0));
  }
  simple=document.createElement('canvas');simple.width=s.width;simple.height=s.height;
  const c=simple.getContext('2d');c.fillStyle='#363636';c.fillRect(0,0,s.width,s.height);
  for(let x=0;x<s.width;x+=s.width/24){c.fillStyle='#828282';c.fillRect(x,0,4,s.height);}
  for(const t of textures)ctx.drawImage(t,0,0);
  }
  prepared=true;document.body.classList.add('prepared');button.disabled=false;
  status.textContent='Prepared. Select Ready to enter fullscreen.';
  send({type:'prepared',run_id:settings.run_id,pixel_sample_sha256:preparedHash});
}
function draw(elapsed,frame) {
  if(gpuScene){const phased=settings.scene==='decoder-regression';
    gpuScene.draw(elapsed,phased?(elapsed>=20&&elapsed<60?1:0):undefined);return;}
  if(settings.scene==='test-pattern') {
    const bars=['#fff','#ff0','#0ff','#0f0','#f0f','#f00','#00f','#000'];
    bars.forEach((color,index)=>{ctx.fillStyle=color;ctx.fillRect(index*canvas.width/8,0,Math.ceil(canvas.width/8),canvas.height);});
    ctx.fillStyle=frame%2?'#000':'#fff';ctx.fillRect(frame*7%canvas.width,canvas.height/2,32,64);
    return;
  }
  const mode=settings.scene==='decoder-regression'?(elapsed<20?'low-motion':elapsed<60?'noise':'low-motion'):settings.scene;
  const noise=mode==='noise'||mode==='stress'||(mode==='recover'&&elapsed<settings.seconds*.7);
  if(noise)ctx.drawImage(textures[frame%4],0,0);
  else {
    ctx.drawImage(simple,0,0);
    if(mode==='scrolling') {
      const offset=frame*8%canvas.width;ctx.drawImage(simple,offset,0);ctx.drawImage(simple,offset-canvas.width,0);
    }
    const x=mode==='static-keepalive'?Math.floor(frame/settings.fps)%2:frame*5%Math.max(1,canvas.width-80);
    ctx.fillStyle=frame%2?'#eee':'#aaa';ctx.fillRect(x,canvas.height/2,mode==='static-keepalive'?4:80,40);
  }
}
function finish(reason) {
  if(!running)return;running=false;document.body.classList.remove('running');
  const values=Array.from(intervals.subarray(0,count));
  const elapsed=values.reduce((a,b)=>a+b,0);
  report={type:'report',run_id:settings.run_id,scene:settings.scene,seed:settings.seed,
    rendered_frames:frameNumber,measured_intervals:count,intervals_ms:values,
    raf_fps:elapsed?count*1000/elapsed:0,hidden_events:hidden,fullscreen_lost:fullscreenLost,
    interval_overflow:count===intervals.length,reason,pixel_sample_sha256:preparedHash,
    browser_origin_ms:start,measure_start_ms:measureStarted,measure_end_ms:performance.now(),
    start_command_id:startCommand,phase_events:phaseEvents,
    measure_start_phase_id:settings.run_id+':measure-start',measure_end_phase_id:settings.run_id+':measure-end',
    gpu_workload:gpuScene?gpuScene.statistics():{supported:false,reason:'compatibility Canvas scene selected'},
    snapshot:snapshot(),user_agent:navigator.userAgent,clock:'browser_performance_now_raf_callback'};
  send(report);status.textContent='Finished: '+reason;button.disabled=false;
}
function tick(now) {
  if(settings&&prepared) {
    preflightFrames.push(now);
    // Keep a time window: a 600-frame cap never reaches 2 s at 480 Hz.
    while(preflightFrames.length>4096||(preflightFrames.length>2&&preflightFrames[1]<now-2500))preflightFrames.shift();
    if(pendingReady&&preflightFrames.length>10&&now-preflightFrames[0]>=2000) {
      pendingReady=false;send({type:'ready',run_id:settings.run_id,snapshot:snapshot(),pixel_sample_sha256:preparedHash});
    }
    if(running) {
      const elapsed=(now-start)/1000;
      if(elapsed>=settings.warmup_seconds+settings.seconds)finish('COMPLETE');
      else {
        const measured=elapsed-settings.warmup_seconds;
        const phase=measured<0?'warmup':settings.scene==='decoder-regression'?
          (measured<20?'low-motion':measured<60?'high-entropy':'recovery'):'measure';
        if(!phaseEvents.length||phaseEvents.at(-1).phase!==phase)
          phaseEvents.push({run_id:settings.run_id,phase,phase_id:settings.run_id+':'+phase,browser_ms:now});
        if(elapsed>=settings.warmup_seconds) {
          if(!measureStarted)measureStarted=now;
          else if(last&&count<intervals.length)intervals[count++]=now-last;
        }
        last=now;
        try{draw(Math.max(0,elapsed-settings.warmup_seconds),frameNumber++);}
        catch(error){finish(String(error.message));}
      }
    } else draw(0,Math.floor(now/1000*settings.fps));
    if(now-lastPulse>=1000){lastPulse=now;send({type:'heartbeat',run_id:settings.run_id,snapshot:snapshot(),running});}
  }
  requestAnimationFrame(tick);
}
button.onclick=async()=>{
  try {await canvas.requestFullscreen();preflightFrames=[];pendingReady=true;status.textContent='Measuring Ready…';}
  catch(e){send({type:'error',run_id:settings?.run_id,reason:'FULLSCREEN_REQUIRED'});status.textContent=e.message;}
};
socket.onopen=async()=>{const response=await fetch('/page-token',{cache:'no-store'});const value=await response.json();send({type:'auth',token:value.token});};
socket.onmessage=async event=>{
  try {
    const data=JSON.parse(event.data);
    if(data.action==='prepare')await prepare(data.settings);
    if(data.action==='start') {
      if(!settings||running||document.hidden||!document.fullscreenElement)throw Error('not Ready');
      running=true;start=performance.now();last=0;count=0;hidden=0;fullscreenLost=0;frameNumber=0;measureStarted=0;
      startCommand=data.command_id;phaseEvents=[];if(gpuScene)gpuScene.draws=0;
      document.body.classList.add('running');send({type:'started',run_id:settings.run_id,command_id:startCommand,browser_origin_ms:start});
    }
    if(data.action==='clock_probe')send({type:'clock_probe',run_id:settings.run_id,command_id:data.command_id,browser_now_ms:performance.now()});
    if(data.action==='stop')finish('STOP_REQUESTED');
  } catch(e){send({type:'error',run_id:settings?.run_id,reason:String(e.message)});}
};
socket.onclose=()=>finish('WORKER_DISCONNECTED');
document.addEventListener('visibilitychange',()=>{if(running&&document.hidden){hidden++;finish('BROWSER_HIDDEN');}});
document.addEventListener('fullscreenchange',()=>{if(running&&!document.fullscreenElement){fullscreenLost++;finish('FULLSCREEN_LOST');}});
requestAnimationFrame(tick);
