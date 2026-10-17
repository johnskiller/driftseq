/* emu.js — DriftSeq webemu SHELL (batch 2): the screen truth lives in C.
 * This file only: boots the UI wasm module (DriftUiModule = app layer + engine
 * + Barlow fonts), wires panel.js inputs to C event exports, blits the C
 * framebuffer to the 240x240 canvas each rAF, and routes the engine's MIDI
 * byte stream to WebMIDI/WebAudio. BUILD stamp = what the C painters ship. */
'use strict';
const BUILD='fw-b2 10-17';
const CH=240;
let M=null, H8=null, fbPtr=0, imgData=null, playing=false, lastT=0, rafId=0;
let midiOut=null, audioCtx=null;
const $=id=>document.getElementById(id);
const ctx=$('screen').getContext('2d');
const mon=$('mon');
let monCount=0;

/* crash beacon: unchanged from r16j (survives a dead rAF loop) */
let lastBeacon=0, lastBeaconMsg='';
function crashBeacon(msg){
  try{
    const txt=String(msg).slice(0,300);
    monCount++;
    mon.innerHTML=`<span class="t">CRASH</span> ${txt.slice(0,120)}`;
    const now=performance.now();
    if(txt!==lastBeaconMsg || now-lastBeacon>2000){
      lastBeacon=now; lastBeaconMsg=txt;
      const body=new Date().toISOString()+' '+txt+'\n';
      navigator.sendBeacon
        ? navigator.sendBeacon('/crash-beacon', new Blob([body],{type:'text/plain'}))
        : fetch('/crash-beacon',{method:'POST',body,keepalive:true});
    }
  }catch(_){}
}

function monEvent(bytes){
  if(!bytes||!bytes.length) return;
  monCount++;
  let txt;
  const b=bytes, t=b[0]&0xF0;
  const ch=(b[0]&0x0F)+1;
  if (t===0x90&&b[2]>0) txt=`note-on  ch${ch} note ${b[1]} vel ${b[2]}`;
  else if (t===0x80||(t===0x90&&b[2]===0)) txt=`note-off ch${ch} note ${b[1]}`;
  else if (t===0xC0) txt=`PC       ch${ch} prog ${b[1]}`;
  else if (t===0xB0) txt=`CC       ch${ch} cc${b[1]} = ${b[2]}`;
  else txt=[...b].map(x=>x.toString(16).padStart(2,'0')).join(' ');
  const stamp=new Date().toISOString().slice(14,22);
  mon.innerHTML=`<span class="t">${stamp}</span> ${txt}`;
}

function beep(note,vel){
  if(!audioCtx) return;
  const o=audioCtx.createOscillator(), g=audioCtx.createGain();
  o.type='square'; o.frequency.value=440*Math.pow(2,(note-69)/12);
  g.gain.value=Math.min(0.12,vel/127*0.12);
  o.connect(g).connect(audioCtx.destination);
  o.start(); g.gain.setTargetAtTime(0.0001,audioCtx.currentTime+0.08,0.04); o.stop(audioCtx.currentTime+0.25);
}

/* ---- MIDI stream drain: C appends events to g.out; we send + echo ---- */
const flash=new Float32Array(16);                 // track activity metering (no-op in shell visuals)
function readMidiOut(){
  if(!M) return;
  const n=M._ds_web_out_len();
  if(n<=0) return;
  const base=M._ds_web_out();
  const bytes=H8.slice(base,base+n);
  M._ds_web_out_clear();
  if(midiOut){
    try{
      midiOut.send(bytes);
      for(let i=0;i<bytes.length;){
        const b=bytes[i]; if(b<0x80){i++;continue;}
        const t2=b&0xF0, len=(t2===0xC0||t2===0xD0)?2:3;
        echoStamp(bytes.slice(i,i+len)); i+=len;
      }
    }catch(e){}
  }
  for(let i=0;i<bytes.length;){
    const b=bytes[i];
    if(b<0x80){i++;continue;}
    const type=b&0xF0;
    if(type===0xC0||type===0xD0){ if(type===0xC0) monEvent(bytes.slice(i,i+2)); i+=2; continue; }
    if(type===0x90&&bytes[i+2]>0){ monEvent(bytes.slice(i,i+3)); beep(bytes[i+1],bytes[i+2]); }
    else if(type===0xB0){ monEvent(bytes.slice(i,i+3)); }
    else { monEvent(bytes.slice(i,i+3)); }
    i+=3;
  }
}

/* ---- framebuffer blit: C owns pixels; JS copies 240x240 RGB565 → canvas ---- */
function blit(){
  const px=H8, p=fbPtr;
  const d=imgData.data;
  for(let i=0;i<CH*CH;i++){
    const v=px[p+i*2]|(px[p+i*2+1]<<8);
    const o=i*4;
    d[o]=((v>>11)&31)<<3; d[o+1]=((v>>5)&63)<<2; d[o+2]=(v&31)<<3; d[o+3]=255;
  }
  ctx.putImageData(imgData,0,0);
  ctx.font='8px monospace'; ctx.fillStyle='rgba(255,255,255,0.35)';
  ctx.fillText(BUILD,196,238);                     // build stamp (John: freshness at a glance)
}

/* ---- main loop ---- */
function frame(t){
  if(!lastT)lastT=t;
  let dt=(t-lastT)/1000; lastT=t;
  if(dt>0.25)dt=0.25;
  try{
    M._ds_ui_frame(dt);
    readMidiOut();
    blit();
  }catch(e){ crashBeacon('frame: '+e.message); lastT=0; }
  rafId=requestAnimationFrame(frame);
}

/* ---- panel semantics: mirror of the old emu's mask events → C edges ---- */
function panelButton(ev){
  if(!ev) return;
  const downBits = Array.isArray(ev) ? ev : (ev.bits || []);
  const upBits   = Array.isArray(ev) ? []  : (ev.released || []);
  for(const bit of downBits) M._ds_web_btn(bit, 1);
  for(const bit of upBits){
    M._ds_web_btn(bit, 0);
    if(bit===DSPanel.B.PLAY) playing = !playing;   // mirror for local UI only
  }
}
function panelEnc(role,n){ M._ds_web_enc(role, n>0?Math.min(n,127):-Math.min(-n,128)); }
function panelKey(pressed,down,released){
  for(const k of (pressed||[]))  M._ds_web_key(k, 1);
  for(const k of (released||[])) M._ds_web_key(k, 0);
}

/* ---- WebMIDI out/in (unchanged contract from r16j) ---- */
function refreshMidi(){
  const st=$('webmidi-state');
  if(!navigator.requestMIDIAccess){ if(st) st.textContent='WebMIDI unavailable (Chrome/Edge)'; return; }
  navigator.requestMIDIAccess({sysex:false}).then(acc=>{
    acc.onstatechange=(e)=>{
      if(e.target===midiAccess && e.port && e.port.connection==='open') return;
      refreshMidi();
    };
    bindMidiIn(acc);
    const sel=$('midiouts'); if(!sel) return;
    const outs=[...acc.outputs.values()];
    const prev=midiOut?midiOut.id:'';
    const sig=outs.map(o=>o.id+':'+o.name).join('|');
    if(sel.dataset.sig!==sig){
      sel.dataset.sig=sig;
      sel.innerHTML='<option value="">— none (beeps only, no loop) —</option>';
      for(const o of outs){ const op=document.createElement('option'); op.value=o.id; op.textContent=o.name; sel.appendChild(op); }
    }
    sel.onchange=()=>{ midiOut=outs.find(o=>o.id===sel.value)||null; if(midiOut) monEvent([0xF6]); };
    midiOut=outs.find(o=>o.id===prev)||null;
    sel.value=midiOut?midiOut.id:'';
    if(st) st.textContent=outs.length+' out(s) ready';
  },e=>{ if(st) st.textContent='WebMIDI denied'; });
}
let midiAccess=null, midiInId='all', echoRing=[];
const ECHO_MS=250;
function echoStamp(msg){
  const now=performance.now();
  echoRing.push({s:[...msg].map(x=>x.toString(16).padStart(2,'0')).join(' '), t:now});
  if(echoRing.length>64) echoRing=echoRing.slice(-48);
}
function echoHit(st,d1,d2){
  const s=[st,d1,d2].map(x=>x.toString(16).padStart(2,'0')).join(' ');
  const now=performance.now();
  return echoRing.some(r=>r.s===s&&now-r.t<ECHO_MS);
}
function noteToKeyIdx(note){ return note-53; }
function midiIn3(st,d1,d2){
  const t=st&0xF0, ch=st&0x0F;
  if(echoHit(st,d1,d2)) return;
  if(t===0x90&&d2>0){
    const i=noteToKeyIdx(d1);
    if(i>=0&&i<27) DSPanel.keyDown(i);
  } else if(t===0x80||(t===0x90&&d2===0)){
    const i=noteToKeyIdx(d1);
    if(i>=0&&i<27) DSPanel.keyUp(i);
  } else if(t===0xB0){
    const v=d2||0;
    if(d1>=40&&d1<54){ const bit=d1-40; if(v>=64) DSPanel.btnDown(bit); else DSPanel.btnUp(bit); }
    else if((ch===0||ch===1)&&d1>=20&&d1<27){ DSPanel.turnRole(d1-20, v-64); }
    else if(d1===27){ DSPanel.masterVol(v); }
  }
}
function bindMidiIn(acc){
  midiAccess=acc;
  const ins=[...acc.inputs.values()];
  const sel=$('midiins');
  if(sel){
    const sig=ins.map(i=>i.id+':'+i.name).join('|');
    if(sel.dataset.sig!==sig){
      sel.dataset.sig=sig;
      const prev=midiInId;
      sel.innerHTML='<option value="all">— all inputs —</option>';
      for(const i of ins){ const op=document.createElement('option'); op.value=i.id; op.textContent=i.name; sel.appendChild(op); }
      sel.value=[...sel.options].some(o=>o.value===prev)?prev:'all';
    }
    sel.onchange=()=>{ midiInId=sel.value; bindMidiHandlers(acc); };
  }
  bindMidiHandlers(acc);
}
function bindMidiHandlers(acc){
  for(const i of acc.inputs.values()){
    const want = midiInId==='all' || i.id===midiInId;
    i.onmidimessage = want ? (e)=>{
      const d=e.data;
      if(!d||d.length<2||d[0]>=0xF0) return;
      midiIn3(d[0],d[1],d[2]||0);
    } : null;
  }
}

let started=false;
function ensureAudio(){
  if(!audioCtx) audioCtx=new (window.AudioContext||window.webkitAudioContext)();
  started=true;
}

addEventListener('error',e=>{ crashBeacon(e.message||e.error); console.error(e.error||e.message); });
addEventListener('unhandledrejection',e=>{ crashBeacon('promise: '+(e.reason&&(e.reason.message||e.reason))); console.error('unhandled rejection:', e.reason); });

/* ---- boot ---- */
async function boot(){
  M=await DriftUiModule();
  H8=M.HEAPU8;
  M._ds_ui_boot();
  fbPtr=M._ds_web_fb();
  imgData=ctx.createImageData(CH,CH);

  DSPanel.build();
  DSPanel.onButton(panelButton);
  DSPanel.onKey(panelKey);
  DSPanel.onEnc(panelEnc);

  $('go').addEventListener('click',()=>{
    ensureAudio();
    $('start').hidden=true;
    rafId=requestAnimationFrame(frame);
    refreshMidi();
  });
  $('b_refresh').addEventListener('click',refreshMidi);

  $('status').textContent='C painters live (fw batch 2) · demo pattern (seed 42) · pick a MIDI out below';
  $('status').hidden=false;
}
boot().catch(e=>{
  const st=document.getElementById('status');
  st.textContent='BOOT FAIL: '+e.message; st.hidden=false;
  console.error(e);
});
