// ps5-native-app-boilerplate - same-origin API controls with memory-only credentials.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';
const $=id=>document.getElementById(id);let token='',mode='fake',selected=null,connected=false,busy=false;
const terminal=new Set(['COMPLETED','FAILED','INCONCLUSIVE','CANCELLED']);
async function api(path,body,raw=false){
  const response=await fetch('/api/v1/'+path,{method:body===undefined?'GET':'POST',
    headers:{...(token?{Authorization:'Bearer '+token}:{}),...(body===undefined?{}:{'Content-Type':'application/json'})},
    body:body===undefined?undefined:JSON.stringify(body),cache:'no-store'});
  if(!response.ok){let value;try{value=await response.json();}catch{value={};}throw Error('HTTP '+response.status+' '+JSON.stringify(value.detail||{}));}
  return raw?response.blob():response.json();
}
function safe(action){return async event=>{if(event)event.preventDefault();$('error').textContent='';try{await action();}catch(e){$('error').textContent=e.message;}};}
function pretty(id,value){$(id).textContent=JSON.stringify(value,null,2);}
async function refresh(){const value=await api('runs');$('runs').replaceChildren();for(const run of value.items){const button=document.createElement('button');button.textContent=run.run_id+' · '+run.state+' · '+run.verdict;button.onclick=safe(()=>inspect(run.run_id));$('runs').append(button);}}
function chart(windows){const svg=$('chart');svg.replaceChildren();const points=windows.map((w,i)=>[i,w.callback_to_ready_us?.p95]).filter(p=>Number.isFinite(p[1]));if(!points.length)return;const max=Math.max(...points.map(p=>p[1]),1);const line=document.createElementNS('http://www.w3.org/2000/svg','polyline');line.setAttribute('points',points.map(([i,v])=>`${20+i*760/Math.max(windows.length-1,1)},${160-v/max*140}`).join(' '));line.setAttribute('stroke','#77b8ef');line.setAttribute('stroke-width','2');line.setAttribute('fill','none');svg.append(line);const label=document.createElementNS(svg.namespaceURI,'text');label.setAttribute('x','20');label.setAttribute('y','18');label.setAttribute('fill','#b8cce2');label.textContent='max '+Math.round(max)+' µs';svg.append(label);}
async function download(path,name){const blob=await api(path,undefined,true);const a=document.createElement('a');const url=URL.createObjectURL(blob);a.href=url;a.download=name;a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);}
function evidence(report){const analysis=report.analysis||{},ps5=report.ps5_measurement||{};return {
  source_sha:ps5.build_variant?.source_sha??null,installed:ps5.installed??null,
  installed_identity_verified:ps5.installed_identity_verified??false,
  build_flags:ps5.build_variant?.build_flags??null,
  effective_fields:Object.entries(analysis.session_settings||{}).map(([field,value])=>({field,...value})),
  environment:analysis.environment??null,phase_alignment:analysis.phase_alignment??null,
  progress:analysis.progress??null,host_transport:analysis.host_transport??null,
  worker_runtime_identity:report.host_measurement?.worker_runtime_identity??null,
  missing_or_invalid_evidence:analysis.issues||[],
  cleanup_errors:report.cleanup_errors||[]};}
async function inspect(id){selected=id;const run=await api('runs/'+id);if(selected!==id)return;$('detail').hidden=false;$('run-title').textContent=id;$('stage').textContent=run.stage;$('stop').disabled=terminal.has(run.state);const report=run.report||{};const analysis=report.analysis||{};$('verdicts').replaceChildren();for(const label of ['Overall: '+(report.overall_verdict||run.verdict),'Execution: '+(report.execution_state||run.state),'Functional: '+(analysis.functional_verdict||run.verdict),'Measurement: '+(analysis.measurement_validity||'Pending'),'Performance: '+(analysis.performance_delta||'NOT_COMPARABLE')]){const badge=document.createElement('span');badge.className='badge';badge.textContent=label;$('verdicts').append(badge);}pretty('events',(await api('runs/'+id+'/events')).items);pretty('report',report);pretty('evidence',evidence(report));chart(analysis.windows||[]);$('artifacts').replaceChildren();for(const file of (await api('runs/'+id+'/artifacts')).items){const button=document.createElement('button');button.className='secondary';button.textContent=file.name+' ('+file.bytes+' B)';button.onclick=safe(()=>download('runs/'+id+'/artifacts/'+file.name.split('/').map(encodeURIComponent).join('/'),file.name.split('/').at(-1)));$('artifacts').append(button);}$('bundle').disabled=!terminal.has(run.state);}
$('connect').onsubmit=safe(async()=>{token=$('token').value;$('token').value='';const caps=await api('capabilities');mode=caps.api_mode==='real'?'real':'fake';const suites=await api('suites');$('suite').replaceChildren();for(const name of suites.items){const option=document.createElement('option');option.value=option.textContent=name;$('suite').append(option);}$('connection').textContent='Connected · '+mode;$('mode').textContent=mode==='fake'?'Simulated adapters; no hardware is touched.':'Real adapters: the installed developer build must match the profile and configured hash.';connected=true;for(const id of ['doctor','start','suite','refresh','compare'])$(id).disabled=false;await refresh();});
$('create').onsubmit=safe(async()=>{$('start').disabled=true;try{const run=await api('runs',{suite:$('suite').value,mode,profile:JSON.parse($('profile').value)});await api('runs/'+run.run_id+'/start',{});await refresh();await inspect(run.run_id);}finally{$('start').disabled=false;}});
$('doctor').onclick=safe(async()=>pretty('environment',await api('doctor')));
$('refresh').onclick=safe(refresh);$('stop').onclick=safe(async()=>{if(selected){await api('runs/'+selected+'/stop',{});await inspect(selected);await refresh();}});
$('bundle').onclick=safe(()=>download('runs/'+selected+'/bundle?redact='+$('redact').checked,selected+'.zip'));
$('compare').onclick=safe(async()=>pretty('comparison',await api('compare',{pairs:JSON.parse($('pairs').value),calibration:$('calibration').checked,screening:JSON.parse($('screening').value),calibration_pairs:JSON.parse($('calibration-pairs').value)})));
setInterval(async()=>{if(!connected||busy||document.hidden)return;busy=true;try{await api('health');await refresh();if(selected)await inspect(selected);}catch(e){$('connection').textContent='Connection lost';$('error').textContent=e.message;}finally{busy=false;}},2000);
