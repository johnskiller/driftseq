/* emu.js — DriftSeq emu glue: the FM-1 panel (panel.js) drives the pure-C engine (wasm);
 * the 240×240 canvas is the instrument's screen (specs 03: HOME hero + SEQ grid);
 * the strip under the device is the MIDI-out monitor (same bytes WebMIDI sends).
 */
'use strict';
const TRACKS=16, STEPS=64, CH=240;
let M=null, S=0, H8=null;
let OFF={}, TK={}, ST={};
let playing=false, lastT=0, rafId=0;
let selTrack=0, mode='home';                 // HOME hero / SEQ grid (HOME button toggles)
let midiOut=null, midiAccess=null;
let audioCtx=null;
const $=id=>document.getElementById(id);
const ctx=$('screen').getContext('2d');
const mon=$('mon');
let monCount=0;

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

const COLORS=[
  '#e05c5c','#e08b3c','#d9c94a','#7fd94a','#4ad9a8','#4ac5d9','#5c7fe0','#9a5ce0',
  '#e05ca8','#e05c7f','#d98a6b','#c9d96b','#6bd9c9','#8a6bd9','#d96b9a','#9ad96b'];

function i32(off){const d=new DataView(M.HEAPU8.buffer);return d.getInt32(S+off,true);}
function f64(off){const d=new DataView(M.HEAPU8.buffer);return d.getFloat64(S+off,true);}
function trkOff(t){return OFF.tracks+t*TK.stride;}
function trkU8(t,f){return H8[S+trkOff(t)+TK[f]];}
function stepOff(t,s){return trkOff(t)+TK.steps+s*ST.size;}
function stepU8(t,s,f){return H8[S+stepOff(t,s)+ST[f]];}

function readMidiOut(){
  const n=i32(OFF.out_len), base=OFF.out;
  if(n>0){
    const bytes=H8.slice(S+base,S+base+n);
    if(midiOut){ try{ midiOut.send(bytes);}catch(e){} }
    for(let i=0;i<bytes.length;){
      const b=bytes[i];
      if(b<0x80){i++;continue;}
      const type=b&0xF0;
      if(type===0xC0||type===0xD0){ if(type===0xC0) monEvent(bytes.slice(i,i+2)); i+=2; continue; }
      if(type===0x90&&bytes[i+2]>0){
        monEvent(bytes.slice(i,i+3));
        const ch=b&0x0F;
        for(let t=0;t<TRACKS;t++) if(trkU8(t,'channel')===ch){ flash[t]=1; break; }
        beep(bytes[i+1],bytes[i+2]);
      }
      else if(type===0xB0){ monEvent(bytes.slice(i,i+3)); }   // CC (stop's all-notes-off lands here)
      else if(type===0x80||(type===0x90&&bytes[i+2]===0)){ monEvent(bytes.slice(i,i+3)); }  // note-off
      i+=3;
    }
  }
}
function beep(note,vel){
  if(!audioCtx) return;
  const o=audioCtx.createOscillator(), g=audioCtx.createGain();
  o.type='square'; o.frequency.value=440*Math.pow(2,(note-69)/12);
  g.gain.value=Math.min(0.12,vel/127*0.12);
  o.connect(g).connect(audioCtx.destination);
  o.start(); g.gain.setTargetAtTime(0.0001,audioCtx.currentTime+0.08,0.04); o.stop(audioCtx.currentTime+0.25);
}

/* ---------- the screen: HOME hero (03-screens) ---------- */
function drawHome(){
  ctx.fillStyle='#000'; ctx.fillRect(0,0,CH,CH);
  ctx.fillStyle='#181820'; ctx.fillRect(0,0,CH,20);
  ctx.fillStyle='#d9d9e8'; ctx.font='11px monospace';
  ctx.fillText(`DriftSeq  ${Math.round(f64(OFF.bpm))} BPM  ${playing?'PLAY':'STOP'}`,6,14);
  ctx.fillStyle='#7fd94a';
  ctx.fillText(`TRK ${String(selTrack+1).padStart(2,'0')}`,196,14);
  const barH=11, gapY=1, x0=28, x1=232, w=x1-x0;
  for(let t=0;t<TRACKS;t++){
    const y=24+t*(barH+gapY);
    const muted=trkU8(t,'mute');
    const start=trkU8(t,'loop_start'), len=trkU8(t,'loop_len');
    ctx.fillStyle= muted?'#141418':'#1d1d26';
    ctx.fillRect(x0,y,w,barH);
    if(!muted){
      ctx.fillStyle=COLORS[t];
      ctx.globalAlpha=0.28;
      for(let k=0;k<len;k++){
        const px=x0+((start+k)&63)*w/64;
        ctx.fillRect(px,y,w/64-0.5,barH);
      }
      ctx.globalAlpha=1;
      for(let s=0;s<STEPS;s++){
        if(stepU8(t,s,'on')&&stepU8(t,s,'nvoices')){
          const rel=(s+64-start)&63;
          if(rel<len){
            const px=x0+s*w/64;
            ctx.globalAlpha= stepU8(t,s,'prob')<100?0.55:1;
            ctx.fillStyle=COLORS[t];
            ctx.fillRect(px,y,Math.max(1.5,w/64-1),barH);
            ctx.globalAlpha=1;
          }
        }
      }
      const pos=i32(OFF.pos16+t*4);
      const rel=(pos+64-start)&63;
      if(rel<len&&playing){
        ctx.fillStyle='#fff';
        ctx.fillRect(x0+pos*w/64-1,y,2,barH);
      }
    }
    if(t===selTrack){ ctx.strokeStyle='#7fd94a'; ctx.strokeRect(x0-2.5,y-1,w+5,barH+2); }
    flash[t]*=0.88;
  }
  ctx.fillStyle='#181820'; ctx.fillRect(0,220,CH,20);
  ctx.fillStyle='#8a8a99'; ctx.font='10px monospace';
  ctx.fillText('K1 LEN  K2 MUTE  K3 START  K4 PROB',6,233);
}

/* ---------- the screen: SEQ grid (03-screens) ---------- */
function drawGrid(){
  ctx.fillStyle='#000'; ctx.fillRect(0,0,CH,CH);
  ctx.fillStyle='#181820'; ctx.fillRect(0,0,CH,20);
  ctx.fillStyle='#d9d9e8'; ctx.font='11px monospace';
  ctx.fillText(`SEQ  TRK ${selTrack+1}  page ${page+1}/4  steps ${page*16}-${page*16+15}`,6,14);
  const cols=16,rows=4, cw=(CH-8)/cols, chh=(CH-60)/rows;
  const start=trkU8(selTrack,'loop_start'), len=trkU8(selTrack,'loop_len');
  for(let r=0;r<rows;r++)for(let c=0;c<cols;c++){
    const s=page*16+r*16+c;
    const x=4+c*cw, y=24+r*chh;
    const rel=(s+64-start)&63, inWin=rel<len;
    const on=stepU8(selTrack,s,'on')&&stepU8(selTrack,s,'nvoices');
    const pos=i32(OFF.pos16+selTrack*4), isNow=playing&&pos===s;
    ctx.fillStyle= on? (inWin?COLORS[selTrack]:'#333') : (inWin?'#1d1d26':'#121216');
    ctx.fillRect(x+1,y+1,cw-2,chh-2);
    if(isNow){ctx.strokeStyle='#fff';ctx.strokeRect(x+1.5,y+1.5,cw-3,chh-3);}
    if(stepU8(selTrack,s,'prob')<100&&on){ctx.fillStyle='#000';ctx.globalAlpha=.35;ctx.fillRect(x+1,y+chh-6,cw-2,4);ctx.globalAlpha=1;}
  }
  ctx.fillStyle='#8a8a99'; ctx.font='10px monospace';
  ctx.fillText('OCT: page · keys play into cursor · K1-K4 step params',6,233);
}

/* ---------- step-entry (02-input v1): key press writes the cursor step ---------- */
let cursor=0;                                    // absolute step 0..63
const liveHeld=new Set();                        // panel keys currently sounding (note-off dedup)
function liveNote(keyIdx,down){                  // panel key -> live preview note through the engine
  const t=selTrack;
  let note;
  if(trkU8(t,'channel')===9){                    // drums: fixed GM map
    const gm=[36,38,42,46,49,45,41,43,51,47,39,56,37,54,40,58,62,63,27,34];
    note=gm[keyIdx%gm.length];
  } else note=53+keyIdx;                         // F3 = 53
  const key='k'+t+':'+note;
  if(down){ M._ds_live_on_wasm(t,note,100); liveHeld.add(key); }
  else{
    liveHeld.delete(key);                          // stale or not, JS-side tracking mirrors the pool
    M._ds_live_off_wasm(t,note);                   // C-side dedups: stale off emits nothing
  }
  readMidiOut();                                 // live on/off emit into out[] immediately — drain now
}
function stepEntry(pressed,down,released){
  if(!down||(!Array.isArray(pressed)&&!Array.isArray(released))) return;
  const t=selTrack;
  const start=trkU8(t,'loop_start'), len=trkU8(t,'loop_len');
  if(((cursor+64-start)&63)>=len) cursor=start;   // resync into the window
  for(const keyIdx of (pressed||[])){
    liveNote(keyIdx,true);                        // hear it now
    const note = trkU8(t,'channel')===9 ? [36,38,42,46,49,45,41,43,51,47,39,56,37,54,40,58,62,63,27,34][keyIdx%20] : 53+keyIdx;
    M._ds_voice_add_wasm(t,cursor,note,100,1);
  }
  for(const keyIdx of (released||[])) liveNote(keyIdx,false);   // key up -> note-off
  if(pressed&&pressed.length) cursor=(start+(((cursor+64-start)+1)%len))&63;  // advance inside the window
}

/* ---------- main loop ---------- */
const flash=new Float32Array(TRACKS);
function frame(t){
  if(!lastT)lastT=t;
  let dt=(t-lastT)/1000; lastT=t;
  if(dt>0.25)dt=0.25;
  M._ds_tick_wasm(dt); readMidiOut();            // tick always: live note-offs must drain even when stopped
  if(mode==='home')drawHome(); else drawGrid();
  rafId=requestAnimationFrame(frame);
}

/* ---------- panel semantics (02-input) ---------- */
function panelButton(ev){
  if(!ev) return;
  // ev.bits = buttons currently held; ev.released = buttons just released (PLAY/STOP is one
  // physical button: a click = down+up in the same mask → toggle on the release edge)
  const downBits = Array.isArray(ev) ? ev : (ev.bits || []);
  const upBits   = Array.isArray(ev) ? []  : (ev.released || []);
  for(const bit of downBits){
    const name=bitName(bit);
    switch(name){
      case 'SEQ': mode='grid'; break;
      case 'ARP': mode='home'; break;               // v1: no Euclid page yet
      case 'OCT-': page=(page+3)&3; break;
      case 'OCT+': page=(page+1)&3; break;
      case 'REC': M._ds_toggle_step_wasm(selTrack,cursor); break;  // v1: REC=cursor toggle
      case 'HOME': mode = mode==='home'?'grid':'home'; page=0; break;
      case 'PLAY': break;                              // handled on release edge (toggle)
      case 'SELECT': selectTrack((selTrack+15)%16); break;
      default: break;                               // SAVE/FX/SCL/ENV/LFO/EDIT/GLO: v1 unsignaled
    }
  }
  for(const bit of upBits){
    if(bitName(bit)==='PLAY'){                       // toggle on release edge (one physical PLAY/STOP key)
      if(playing){ M._ds_stop_wasm(); playing=false; readMidiOut(); }  // drain the CC123 all-notes-off
      else if(upBits.length===1){ ensureAudio(); M._ds_play_wasm(); playing=true; }  // fresh click (not key-off of a chord)
    }
  }
}
function bitName(bit){ return Object.entries(DSPanel.B).find(([n,b])=>b===bit)?.[0]; }
function panelEnc(role,n){
  if(role===0){ selectTrack((selTrack+ (n>0?1:15))%16); return; }             // SELECT: track
  if(role===1){ M._ds_set_bpm_wasm(Math.max(40,Math.min(240,f64(OFF.bpm)+n))); return; } // ALGORITHM: BPM (v1)
  if(role===2){ return; }                                                     // PRESETS: unsignaled
  const t=selTrack;
  if(role===3) M._ds_set_loop_wasm(t,-1,Math.max(1,Math.min(64,trkU8(t,'loop_len')+n)));   // K1 LEN
  if(role===4) M._ds_mute_wasm(t, trkU8(t,'mute')?(n>0?0:1):(n<0?1:0));       // K2 MUTE
  if(role===5) M._ds_set_loop_wasm(t,Math.max(0,Math.min(63,trkU8(t,'loop_start')+n)),-1); // K3 START
  if(role===6){                                                               // K4 PROB
    const s=cursor, p=stepU8(t,s,'prob');
    H8[S+stepOff(t,s)+ST.prob]=Math.max(0,Math.min(100,p+n*5));
  }
}
function selectTrack(t){
  selTrack=t; M._ds_select_track_wasm(t);
  const start=trkU8(t,'loop_start');
  if(((cursor+64-start)&63)>=trkU8(t,'loop_len')) cursor=start;
}

/* ---------- WebMIDI out ---------- */
function refreshMidi(){
  const st=$('webmidi-state');
  if(!navigator.requestMIDIAccess){ if(st) st.textContent='WebMIDI unavailable (Chrome/Edge)'; return; }
  navigator.requestMIDIAccess({sysex:false}).then(acc=>{
    midiAccess=acc;
    acc.onstatechange=()=>refreshMidi();            // hot-plug: a device appearing refreshes
    bindMidiIn(acc);
    const sel=$('midiouts'); if(!sel) return;
    const outs=[...acc.outputs.values()];
    const prev=midiOut?midiOut.id:null;
    sel.innerHTML='';
    if(!outs.length){
      sel.innerHTML='<option>— no MIDI outputs —</option>';
      if(st) st.textContent='no outputs found';
      midiOut=null;
      return;
    }
    for(const o of outs){ const op=document.createElement('option'); op.value=o.id; op.textContent=o.name; sel.appendChild(op); }
    sel.onchange=()=>{ midiOut=outs.find(o=>o.id===sel.value)||null; if(midiOut) monEvent([0xF6]); };
    // keep the previous choice if still present, else take the first
    midiOut=outs.find(o=>o.id===prev)||outs[0];
    sel.value=midiOut.id;
    if(st) st.textContent=outs.length+' out(s) ready';
  },e=>{ if(st) st.textContent='WebMIDI denied'; });
}

/* ---------- WebMIDI in: the FM-1 hardware drives this page (fm1-ctrl map) ----------
 * fm1-ctrl firmware (docs/maps/melodee.json v0.2, keyscan.c/encoders.c):
 *   notes  53..79  ch1  -> 27 keys F3..G5 (vel 100, 0x80 on release)
 *   CC 40..53 ch1  -> 14 buttons FX SEL ENV LFO EDIT GLO HOME SAVE ARP SEQ PLAY REC OCT- OCT+ (127/0)
 *   CC 20..26 ch1  -> encoders SELECT ALGORITHM PRESETS K1..4, relative 64±n
 *   CC 20..26 ch2  -> push+turn (second layer, 0xB1)
 *   CC 27          -> MASTER (abs 0..127)                                        */
const BTN_CC0=40, ENC_CC0=20;
const keyStates=new Set();                       // notes currently held from hardware
function noteToKeyIdx(note){ return note-53; }   // key idx 0..26
function midiIn3(st,d1,d2){
  const t=st&0xF0, ch=st&0x0F;
  if(t===0x90&&d2>0){                            // key down
    const i=noteToKeyIdx(d1);
    if(i>=0&&i<27&&!keyStates.has(d1)){ keyStates.add(d1); DSPanel.keyDown(i); }
  } else if(t===0x80||(t===0x90&&d2===0)){       // key up
    const i=noteToKeyIdx(d1);
    if(i>=0&&i<27&&keyStates.delete(d1)) DSPanel.keyUp(i);
  } else if(t===0xB0){
    const v=d2||0;
    if(d1>=BTN_CC0&&d1<BTN_CC0+14){              // button press/release, bit = cc-40
      const bit=d1-BTN_CC0;
      if(v>=64) DSPanel.btnDown(bit); else DSPanel.btnUp(bit);
    } else if(ch===0&&d1>=ENC_CC0&&d1<ENC_CC0+7){        // encoder turn, relative 64±n
      DSPanel.turnRole(d1-ENC_CC0, v-64);
    } else if(ch===1&&d1>=ENC_CC0&&d1<ENC_CC0+7){        // push+turn layer
      DSPanel.turnRole(d1-ENC_CC0, v-64);
    } else if(d1===27){                          // MASTER (absolute)
      DSPanel.masterVol(v);
    }
  }
}
function bindMidiIn(acc){
  for(const inp of acc.inputs.values()){
    inp.onmidimessage=(e)=>{
      const d=e.data;
      if(!d||d.length<2||d[0]>=0xF0) return;     // ignore realtime/system
      // swallow echo of our own out stream only if it came back on an input we didn't choose — keep simple: accept all
      midiIn3(d[0],d[1],d[2]||0);
    };
  }
}

let page=0, started=false;
function ensureAudio(){
  if(!audioCtx) audioCtx=new (window.AudioContext||window.webkitAudioContext)();
  started=true;
}

/* ---------- boot ---------- */
async function boot(){
  M=await DriftSeqModule();
  S=M._ds_new(42n)|0;
  H8=M.HEAPU8;
  OFF={
    out:M._ds_offset_out(), out_len:M._ds_offset_out_len(),
    pos16:M._ds_offset_pos16(), frac16:M._ds_offset_frac16(),
    bpm:M._ds_offset_bpm(), tracks:M._ds_track_off(),
  };
  TK={stride:M._ds_track_stride(), channel:M._ds_trk_off_channel(), prog:M._ds_trk_off_prog(),
      loop_start:M._ds_trk_off_loop_start(), loop_len:M._ds_trk_off_loop_len(),
      division:M._ds_trk_off_division(), mute:M._ds_trk_off_mute(), steps:M._ds_trk_off_steps()};
  ST={size:M._ds_step_size(), on:M._ds_step_off_on(), nvoices:M._ds_step_off_nvoices(),
      prob:M._ds_step_off_prob(), voices:M._ds_step_off_voices()};

  DSPanel.build();
  DSPanel.onButton(panelButton);
  DSPanel.onKey(stepEntry);
  DSPanel.onEnc(panelEnc);

  $('go').addEventListener('click',()=>{
    ensureAudio();
    $('start').hidden=true;
    rafId=requestAnimationFrame(frame);
    refreshMidi();                                  // user gesture: ask for WebMIDI here
  });
  $('b_refresh').addEventListener('click',refreshMidi);

  selectTrack(0);
  $('status').textContent='engine loaded · demo pattern (seed 42) · pick a MIDI out below';
  $('status').hidden=false;
}
boot().catch(e=>{
  const st=document.getElementById('status');
  st.textContent='BOOT FAIL: '+e.message; st.hidden=false;
  console.error(e);
});
