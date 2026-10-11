// ps5-native-app-boilerplate - Deterministic native decoder stimulus.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';
const trials = [
  ['H1','HEVC','classic',3,3840,2160,120,80],
  ['H2','HEVC','classic',4,3840,2160,120,80],
  ['H3','HEVC','adaptive',4,3840,2160,120,80],
  ['H4','HEVC','adaptive',3,3840,2160,120,80],
  ['H5','HEVC','classic',5,3840,2160,120,80],
  ['H6','HEVC','adaptive',5,3840,2160,120,80],
  ['H7','HEVC','classic',3,3840,2160,120,80],
  ['A1','H.264','classic',3,3840,2160,120,20],
  ['A2','H.264','adaptive',3,3840,2160,120,20],
  ['A3','H.264','classic',3,3840,2160,120,20],
  ['C1','HEVC','classic',3,3840,2160,60,80],
  ['C2','H.264','classic',3,1920,1080,120,20]
].map(([trial_id,codec,decoder,cores,width,height,fps,bitrate_mbps]) =>
  ({suite:'decoder-v1',trial_id,codec,decoder,cores,width,height,fps,bitrate_mbps,seed:0x51dec0de,mode:'Unpaced',vsync:true,hdr:false}));
const phases = [{name:'Разогрев',start:0,end:10,load:'low'},
  {name:'Низкая сложность',start:10,end:30,load:'low'},
  {name:'Перегрузка',start:30,end:70,load:'high'},
  {name:'Восстановление',start:70,end:90,load:'low'}];
const trial=document.querySelector('#trial'),status=document.querySelector('#status');
const canvas=document.querySelector('#scene'),ctx=canvas.getContext('2d',{alpha:false});
let noise=[],simple=null,prepared='',running=false,report=null,began=0,previous=0,lastPhase=-1,pending=false;
for(const settings of trials) {
  const option=document.createElement('option');option.value=settings.trial_id;
  option.textContent=settings.trial_id+' '+settings.codec+' '+settings.decoder+' '+settings.cores+' cores'+(settings.trial_id[0]==='C'?' (контроль)':'');trial.append(option);
  if(settings.trial_id[0]!=='C') {
    const row=document.createElement('tr');
    for(const value of [settings.trial_id,settings.codec,settings.decoder,settings.cores,`${settings.width}×${settings.height} / ${settings.fps} / ${settings.bitrate_mbps}`]) {
      const cell=document.createElement('td');cell.textContent=value;row.append(cell);
    }
    document.querySelector('#matrix').append(row);
  }
}
const selected=()=>trials.find(s=>s.trial_id===trial.value);
function showSettings() {
  const s=selected();document.querySelector('#settings').textContent=
    `${s.codec}, ${s.width}×${s.height}, ${s.fps} FPS, ${s.bitrate_mbps} Mbps; ${s.decoder}, ${s.cores} cores. Unpaced / VSync On / SDR.`;
  document.querySelector('#start').disabled=prepared!==`${s.width}x${s.height}`||pending;
}
trial.onchange=showSettings;showSettings();
async function prepare() {
  const s=selected();prepared='';noise=[];simple=null;
  document.querySelector('#start').disabled=true;document.querySelector('#prepare').disabled=true;trial.disabled=true;
  status.textContent='Создаём четыре шумовые текстуры; эта работа не входит в измерения…';
  try {
    canvas.width=s.width;canvas.height=s.height;
    // Generate only before a timed run. Fixed xorshift32 seed, no Math.random().
    for(let n=0;n<4;n++) {
      const texture=document.createElement('canvas');texture.width=s.width;texture.height=s.height;
      const image=texture.getContext('2d').createImageData(s.width,s.height);
      let seed=(s.seed+n)>>>0;
      for(let at=0;at<image.data.length;at+=4) {
        seed^=seed<<13;seed^=seed>>>17;seed^=seed<<5;
        const shade=32+(seed>>>25);image.data[at]=shade;image.data[at+1]=shade;image.data[at+2]=shade;image.data[at+3]=255;
      }
      texture.getContext('2d').putImageData(image,0,0);noise.push(texture);
      await new Promise(resolve=>setTimeout(resolve,0));
    }
    simple=document.createElement('canvas');simple.width=s.width;simple.height=s.height;
    const c=simple.getContext('2d');c.fillStyle='#363636';c.fillRect(0,0,s.width,s.height);
    for(let x=0;x<s.width;x+=s.width/24) {c.fillStyle='#828282';c.fillRect(x,0,4,s.height);}
    // Upload/touch textures before warmup to avoid counting first-use stalls.
    for(const texture of noise) ctx.drawImage(texture,0,0);ctx.drawImage(simple,0,0);
    prepared=`${s.width}x${s.height}`;status.textContent='Сцены готовы. Подключитесь с настройками выбранной строки и нажмите «Начать».';
  } catch(e) {status.textContent='Подготовка не удалась: '+e.message;}
  finally {document.querySelector('#prepare').disabled=false;trial.disabled=false;showSettings();}
}
async function post(url,data) {
  const response=await fetch(url,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data)});
  const reply=await response.json();if(!response.ok) throw Error(reply.error||'HTTP '+response.status);return reply;
}
async function start() {
  if(running||pending) return;
  const settings={...selected()};
  if(prepared!==`${settings.width}x${settings.height}`) return;
  document.querySelector('#start').disabled=true;
  try {
    // Request fullscreen within the click's activation, before network awaits.
    await document.documentElement.requestFullscreen();
    const run=await post('/api/decoder/start',{settings});
    report={schema:1,decoder_run_id:run.id,settings,started_utc:new Date().toISOString(),user_agent:navigator.userAgent,
      screen:{width:screen.width,height:screen.height,dpr:devicePixelRatio},canvas:{width:canvas.width,height:canvas.height},
      phases:phases.map(p=>({...p,frames:0,intervals_ms:[]})),warnings:[],hidden_events:0};
    document.body.classList.add('running');document.querySelector('#download').disabled=true;
    running=true;pending=true;began=null;previous=0;lastPhase=-1;requestAnimationFrame(tick);
  } catch(e) {status.textContent='Не удалось начать: '+e.message;document.querySelector('#start').disabled=false;
    if(document.fullscreenElement) await document.exitFullscreen();}
}
function draw(t,phase) {
  const w=canvas.width,h=canvas.height;
  const offset=Math.floor(t*w*.15)%w;
  const texture=phase.load==='high'?noise[Math.floor(t*120)%4]:simple;
  ctx.drawImage(texture,-offset,0);ctx.drawImage(texture,w-offset,0);
  if(phase.load==='low') {ctx.fillStyle='#e0e0e0';ctx.fillRect(w*.48+w*.35*Math.sin(t*.8),h*.2,w*.04,h*.6);}
  ctx.fillStyle='#101010';ctx.fillRect(0,0,w,h*.08);
  ctx.fillStyle='white';ctx.font=`${Math.round(h*.025)}px sans-serif`;
  ctx.fillText(`${report.settings.trial_id} | ${phase.name} | ${t.toFixed(1)} / 90 s`,w*.02,h*.055);
}
function tick(now) {
  if(!running) return;
  // A queued RAF timestamp can precede performance.now() at startup.
  // Establish the measurement origin from the first RAF, in the same clock.
  if(began===null) began=now;
  const t=(now-began)/1000;
  const index=phases.findIndex(p=>t>=p.start&&t<p.end);
  if(index<0) {finish(false);return;}
  const phase=report.phases[index];
  if(index===lastPhase&&previous) phase.intervals_ms.push(now-previous);
  phase.frames++;previous=now;lastPhase=index;draw(t,phase);requestAnimationFrame(tick);
}
async function waitForLogs(id) {
  for(let n=0;n<100;n++) {
    await new Promise(resolve=>setTimeout(resolve,3000));
    try {
      const response=await fetch('/api/decoder/runs/'+id,{cache:'no-store'});
      if(!response.ok) throw Error('HTTP '+response.status);
      const state=await response.json();
      if(state.status==='saved') {
        status.textContent='Логи сохранены на сервере: '+id+'\n'+(state.comparison_eligible?'Настройки совпали. Прогон пригоден для анализа.':JSON.stringify(state,null,2));
        pending=false;trial.disabled=false;showSettings();return;
      }
      if(state.status==='error'||state.status==='timeout') throw Error(state.error);
    } catch(e) {status.textContent='Сбор логов: '+e.message+'\nСкачайте отчёт кнопкой ниже; не начинайте новый стрим до проверки.';pending=false;trial.disabled=false;showSettings();return;}
  }
  status.textContent='Таймаут ожидания логов. Убедитесь, что стрим завершён.';pending=false;trial.disabled=false;showSettings();
}
async function finish(aborted) {
  if(!running) return;
  running=false;report.aborted=aborted;report.ended_utc=new Date().toISOString();
  for(const phase of report.phases) {
    const elapsed=phase.intervals_ms.reduce((a,b)=>a+b,0);
    phase.browser_fps=elapsed?phase.intervals_ms.length*1000/elapsed:0;
    phase.raf_over_1_5_budget=phase.intervals_ms.filter(ms=>ms>1500/report.settings.fps).length;
    if(!aborted&&phase.browser_fps<report.settings.fps*.95) report.warnings.push(`${phase.name}: browser ${phase.browser_fps.toFixed(1)} FPS < requested ${report.settings.fps}. Host stimulus may be a bottleneck.`);
  }
  if(report.hidden_events) report.warnings.push('Browser hidden during timed test.');
  document.body.classList.remove('running');document.querySelector('#download').disabled=false;trial.disabled=true;
  if(document.fullscreenElement) await document.exitFullscreen();
  status.textContent='Сцена завершена. Теперь Тачпад + L1 на PS5. Ожидаем логи после завершения стрима…';
  try {const saved=await post('/api/reports',report);await waitForLogs(saved.id);}
  catch(e) {status.textContent='Отчёт не отправлен: '+e.message+'\nСкачайте JSON; завершите стрим. Сервер освободит незавершённый прогон через 5 минут.';pending=false;trial.disabled=false;showSettings();}
}
document.querySelector('#prepare').onclick=prepare;document.querySelector('#start').onclick=start;
document.querySelector('#download').onclick=()=>{
  if(!report) return;const url=URL.createObjectURL(new Blob([JSON.stringify(report,null,2)],{type:'application/json'}));
  const a=document.createElement('a');a.href=url;a.download='decoder-'+report.settings.trial_id+'-'+report.decoder_run_id+'.json';a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);
};
document.addEventListener('visibilitychange',()=>{if(running&&document.hidden) report.hidden_events++;});
document.addEventListener('fullscreenchange',()=>{if(running&&!document.fullscreenElement) finish(true);});
document.addEventListener('keydown',event=>{if(event.key==='Escape'&&running) finish(true);});
