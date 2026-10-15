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

/* crash beacon: POST any uncaught error back to the dev server so the box-side log
 * captures it (server writes crash.log in web/) — survives even a dead rAF loop */
let lastBeacon=0, lastBeaconMsg='';
function crashBeacon(msg){
  try{
    const txt=String(msg).slice(0,300);
    monCount++;
    mon.innerHTML=`<span class="t">CRASH</span> ${txt.slice(0,120)}`;
    const now=performance.now();
    if(txt!==lastBeaconMsg || now-lastBeacon>2000){   // rate-limit: same error max 1 line / 2s
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

/* GM drum kit notes the emu's live keybed can write (see liveNote) + common extras.
 * DPAL color = instrument identity: same note always same color everywhere. */
const GM_DRUMS=[36,38,42,46,49,45,41,43,51,47,39,56,37,54,40,58,62,63,27,34];
const DPAL=['#e05c5c','#e08b3c','#d9c94a','#7fd94a','#4ad9a8','#4ac5d9','#5c7fe0','#9a5ce0','#e05ca8','#e05c7f','#c9d96b','#8a6bd9'];

const COLORS=[
  '#e05c5c','#e08b3c','#d9c94a','#7fd94a','#4ad9a8','#4ac5d9','#5c7fe0','#9a5ce0',
  '#e05ca8','#e05c7f','#d98a6b','#c9d96b','#6bd9c9','#8a6bd9','#d96b9a','#9ad96b'];

function i32(off){const d=new DataView(M.HEAPU8.buffer);return d.getInt32(S+off,true);}
function f64(off){const d=new DataView(M.HEAPU8.buffer);return d.getFloat64(S+off,true);}
function safeU8(off){ try{ return H8[off]|0; }catch(_){ return 0; } }   // guards during boot races
function trkOff(t){return OFF.tracks+t*TK.stride;}
function trkU8(t,f){return safeU8(S+trkOff(t)+TK[f]);}
function stepOff(t,s){return trkOff(t)+TK.steps+s*ST.size;}
function stepU8(t,s,f){return safeU8(S+stepOff(t,s)+ST[f]);}

function readMidiOut(){
  if(!M||!H8||!OFF.out_len) return;              // boot race guard
  const n=i32(OFF.out_len), base=OFF.out;
  if(n>0){
    const bytes=H8.slice(S+base,S+base+n);
    if(midiOut){
      try{
        midiOut.send(bytes);
        for(let i=0;i<bytes.length;){            // stamp each message into the echo ring
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
  ctx.fillStyle='#181820'; ctx.fillRect(0,0,CH,20);  ctx.fillStyle='#d9d9e8'; ctx.font='11px monospace';
  const routeG=()=> 'U';                                     // v1: all-USB routing; glyphs A=all B=ble U=usb
  ctx.fillText(`TRK ${String(selTrack+1).padStart(2,'0')} │ PTN 01 │ ${Math.round(f64(OFF.bpm))}`,6,14);
  // K3's numeric anchor lives in the top bar's right corner (green, like the old sel-track
  // marker): no extra row — 20px bar + 16×12px rows + 20px footer is exactly the 240 grid.
  ctx.font='11px monospace';
  ctx.fillStyle= trkU8(selTrack,'mute')?'#5a5a62':'#5fe08a';
  ctx.fillText(`S ${String(trkU8(selTrack,'loop_start')).padStart(2,'0')}`,204,14);
  const barH=11, gapY=1, x0=56, x1=232, w=x1-x0;             // left gutter: TRK LEN R [M]
  ctx.font='9px monospace';
  for(let t=0;t<TRACKS;t++){
    const y=24+t*(barH+gapY);
    const muted=trkU8(t,'mute');
    const start=trkU8(t,'loop_start'), len=trkU8(t,'loop_len');
    // left gutter: TRK nn | LEN nn | route | M
    ctx.fillStyle= muted?'#5a5a62':'#9a9aa8';
    ctx.fillText(String(t+1).padStart(2,'0'),4,y+9);
    ctx.fillStyle= muted?'#4a4a52':'#6a6a78';
    ctx.fillText(String(len).padStart(2,'0'),30,y+9);
    ctx.fillStyle= muted?'#4a4a52':'#7a7a88';
    ctx.fillText(routeG(t),46,y+9);
    if(muted){ ctx.fillStyle='#e0a040'; ctx.fillText('M',48,y+9); }
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
  ctx.fillText('K1 START   K2 LEN   K3 MUTE   K4 PROB',6,233);
}

/* ---------- the screen: SEQ grid (03-screens) ---------- */
function drawGrid(){
  if(trkU8(selTrack,'channel')===9) return drawDrumGrid();      // ch10 = drum lanes view
  ctx.fillStyle='#000'; ctx.fillRect(0,0,CH,CH);
  ctx.fillStyle='#181820'; ctx.fillRect(0,0,CH,20);
  ctx.fillStyle='#d9d9e8'; ctx.font='11px monospace';
  // top-right corner: cursor step + v0 note name (HOME uses this syntax for S nn)
  const st0=stepU8(selTrack,cursor,'voices'), n0=(st0===0xFF)?null:st0;
  const NAMES=['C','C#','D','D#','E','F','F#','G','G#','A','A#','B'];
  const nm = n0===null?'':` ${NAMES[n0%12]}${Math.floor(n0/12)-1}`;
  ctx.fillText(`SEQ TRK ${selTrack+1} · p${page+1}/4`,6,14);
  ctx.fillStyle= trkU8(selTrack,'mute')?'#5a5a62':'#5fe08a';
  ctx.fillText(`st${cursor}${nm}`,186,14);
  const cols=16,rows=4, cw=(CH-28)/cols, chh=(CH-60)/rows;
  const start=trkU8(selTrack,'loop_start'), len=trkU8(selTrack,'loop_len');
  const isDrum=trkU8(selTrack,'channel')===9;
  ctx.font='9px monospace';
  for(let r=0;r<rows;r++)for(let c=0;c<cols;c++){
    const s=page*0+r*16+c;                       // full overview: row = page, all 64 visible
    const x=18+c*cw, y=24+r*chh;
    const lit=(r===page)?1:0.45;                 // current keybed page row full-lit, others dimmed
    const rel=(s+64-start)&63, inWin=rel<len;
    const on=stepU8(selTrack,s,'on')&&stepU8(selTrack,s,'nvoices');
    const nv=stepU8(selTrack,s,'nvoices');
    const vBase=trkOff(selTrack)+TK.steps+s*ST.size;           // voices[] = ds_note v[4]
    // hue = pitch (12-semitone wheel, piano-roll convention). Drums too: GM note
    // numbers cluster arbitrarily (36/38/42/46...), so raw pitch hue is meaningless
    // — map through the emu's drum-key palette index instead (instrument = identity).
    let fill;
    if(on){
      const v0n=H8[S+vBase];                                   // v[0].note
      if(isDrum){
        const gi=GM_DRUMS.indexOf(v0n);
        fill=DPAL[(gi>=0?gi:0)%DPAL.length];
      } else {
        fill=`hsl(${v0n%12*30},70%,55%)`;
      }
      if(!inWin) fill='#555560';                 // outside loop window: greyed, still visible
    } else fill=(r===page)?'#1d1d26':'#15151a';
    ctx.globalAlpha=lit;
    ctx.fillStyle=fill;
    ctx.fillRect(x+1,y+1,cw-2,chh-2);
    if(on&&!isDrum){                             // v1..3 stack: 3px hue strips at cell bottom
      for(let vi=1;vi<Math.min(nv,4);vi++){
        const vn=H8[S+vBase+vi*4];               // v[vi].note (ds_note = 4 bytes)
        ctx.fillStyle=`hsl(${vn%12*30},70%,55%)`;
        ctx.fillRect(x+1,y+chh-2-vi*3,cw-2,2);
      }
    }
    const pos=i32(OFF.pos16+selTrack*4), isNow=playing&&pos===s;
    if(isNow){ctx.strokeStyle='#fff';ctx.strokeRect(x+1.5,y+1.5,cw-3,chh-3);}
    if(cursor===s){ctx.strokeStyle='#e6c84a';ctx.strokeRect(x+0.5,y+0.5,cw-1,chh-1);}
    if(stepU8(selTrack,s,'prob')<100&&on){ctx.fillStyle='#000';ctx.globalAlpha=.35*lit;ctx.fillRect(x+1,y+chh-7,cw-2,4);}
    ctx.globalAlpha=1;
    if(c===0){ctx.fillStyle='#8a8a99';ctx.fillText(`P${r+1}`,3,y+11);}   // row = page label
  }
  ctx.fillStyle='#8a8a99'; ctx.font='10px monospace';
  ctx.fillText('K1 START  K2 LEN  K3 DIV  K4 CH',18,233);
}

/* ---------- the screen: drum lanes (03 §2.3 — ALL drums visible, selected lane lit) ---------- */
function drawDrumGrid(){
  ctx.fillStyle='#000'; ctx.fillRect(0,0,CH,CH);
  ctx.fillStyle='#181820'; ctx.fillRect(0,0,CH,20);
  ctx.fillStyle='#d9d9e8'; ctx.font='11px monospace';
  ctx.fillText(`DRUM TRK ${selTrack+1} · p${page+1}/4`,6,14);
  ctx.fillStyle= trkU8(selTrack,'mute')?'#5a5a62':'#5fe08a';
  ctx.fillText(GM_SHORT[drumSel]||('D'+drumSel),200,14);
  const nL=GM_DRUMS.length, lh=Math.min(21,(CH-40)/nL), x0=34;
  const pos=i32(OFF.pos16+selTrack*4);
  ctx.font='8px monospace';
  for(let li=0;li<nL;li++){
    const y=24+li*lh, sel=li===drumSel;
    ctx.fillStyle=sel?'#c9c9d4':'#6a6a76';
    ctx.fillText(GM_SHORT[li],2,y+lh-6);
    for(let c=0;c<16;c++){
      const s=page*16+c;
      const vBase=trkOff(selTrack)+TK.steps+s*ST.size;
      let on=false;
      for(let vi=0;vi<stepU8(selTrack,s,'nvoices');vi++) if(H8[S+vBase+vi*4]===GM_DRUMS[li]) on=true;
      const cw=(CH-8-x0)/16;
      ctx.globalAlpha=sel?1:0.35;
      ctx.fillStyle=on?DPAL[li%DPAL.length]:(sel?'#1d1d26':'#15151a');
      ctx.fillRect(x0+c*cw+1,y+1,cw-2,lh-2);
      if(playing&&pos===s){ctx.globalAlpha=1;ctx.fillStyle='#fff';ctx.fillRect(x0+c*cw,y,cw,1);}  // playhead tick
    }
    ctx.globalAlpha=1;
  }
  ctx.fillStyle='#8a8a99'; ctx.font='10px monospace';
  ctx.fillText('BLK=drum WHT=toggle · K1-K4 track',18,233);
}

/* ---------- step-entry (02-input v1): key press writes the cursor step ---------- */
let cursor=0;                                    // absolute step 0..63
const liveHeld=new Set();                        // panel keys currently sounding (note-off dedup)
/* Drum GRID editing (spec 02 §2.1 / 03 §2.3): black keys = pick instrument, white keys =
 * toggle that drum across the 16 steps of the keybed page. TR-808 muscle memory. */
let drumSel=0;                                   // index into GM_DRUMS (black-key order)
const GM_SHORT=['KCK','SNR','CHH','OHH','CUP','TOM','RID','PERC','SHK','CAB','MAR'];
function drumKeyHandler(pressed,down,released){
  const t=selTrack, inGrid=mode==='grid';
  const black=GM_DRUMS.length;                   // key id >= blackStart => black key
  for(const k of (pressed||[])){
    if(k>=16){                                   // black key: pick drum (audition, light LED)
      drumSel=Math.min(k-16,GM_DRUMS.length-1);
      liveNote(k,true);                          // hear it
    } else if(inGrid){
      const s=page*16+k;                         // white key = toggle drumSel at this step
      const vBase=trkOff(t)+TK.steps+s*ST.size;
      let hit=-1;
      for(let vi=0;vi<stepU8(t,s,'nvoices');vi++) if(H8[S+vBase+vi*4]===GM_DRUMS[drumSel]) hit=vi;
      if(hit>=0) M._ds_voice_remove_wasm(t,s,GM_DRUMS[drumSel]);   // engine removes by note
      else M._ds_voice_add_wasm(t,s,GM_DRUMS[drumSel],100,1);
      liveNote(16+drumSel,true);                 // audition via the black key's note
    } else liveNote(k,true);                     // non-grid (step-entry/PLAY): melodic path
  }
  for(const k of (released||[])) if(k>=16||!inGrid) liveNote(k,false);
}

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
  try{
    M._ds_tick_wasm(dt); readMidiOut();          // tick always: live note-offs must drain even when stopped
    if(mode==='home')drawHome(); else drawGrid();
  }catch(e){ crashBeacon('frame: '+e.message); lastT=0; }  // log it; next rAF retries
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
      if(playing){ M._ds_stop_wasm(); playing=false; readMidiOut(); clearHeldMirrors(); }  // drain CC123, kill mirrors
      else if(upBits.length===1){ ensureAudio(); M._ds_play_wasm(); playing=true; }  // fresh click (not key-off of a chord)
    }
  }
}
/* STOP must not leave phantom "held" notes anywhere:
 * engine pool is cleared in ds_stop (C); here we drop the JS mirrors so a later
 * hardware/echo note-off can't re-light keys or double-fire. */
function clearHeldMirrors(){
  liveHeld.clear();
  keyStates.clear();                                 // also un-light hardware-held keys
  if(DSPanel.syncKeys) DSPanel.syncKeys();
}
function bitName(bit){ return Object.entries(DSPanel.B).find(([n,b])=>b===bit)?.[0]; }
function panelEnc(role,n){
  if(role===0){                                              // SELECT: track, NO wrap
    const next=Math.max(0,Math.min(15,selTrack+(n>0?1:-1)));  // endpoints dead-stop; (future:
    if(next!==selTrack) selectTrack(next); return;            //  accel flick → jump TRK01/16)
  }
  if(role===1){ M._ds_set_bpm_wasm(Math.max(40,Math.min(240,f64(OFF.bpm)+n))); return; } // ALGORITHM: BPM (v1)
  if(role===2){ return; }                                                     // PRESETS: unsignaled
  const t=selTrack;
  if(role===3) M._ds_set_loop_wasm(t,Math.max(0,Math.min(63,trkU8(t,'loop_start')+n)),-1); // K1 START
  if(role===4) M._ds_set_loop_wasm(t,-1,Math.max(1,Math.min(64,trkU8(t,'loop_len')+n)));   // K2 LEN
  if(role===5) M._ds_mute_wasm(t, n>0?1:0);                                   // K3 MUTE: right=on, left=off
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
    acc.onstatechange=(e)=>{                        // hot-plug; skip echo-of-our-own-change events
      if(e.target===midiAccess && e.port && e.port.connection==='open') return;
      refreshMidi();
    };
    bindMidiIn(acc);
    const sel=$('midiouts'); if(!sel) return;
    const outs=[...acc.outputs.values()];
    const prev=midiOut?midiOut.id:'';
    const sig=outs.map(o=>o.id+':'+o.name).join('|');
    if(sel.dataset.sig!==sig){                     // rebuild only on real port-list changes
      sel.dataset.sig=sig;
      sel.innerHTML='<option value="">— none (beeps only, no loop) —</option>';
      for(const o of outs){ const op=document.createElement('option'); op.value=o.id; op.textContent=o.name; sel.appendChild(op); }
    }
    sel.onchange=()=>{ midiOut=outs.find(o=>o.id===sel.value)||null; if(midiOut) monEvent([0xF6]); };
    midiOut=outs.find(o=>o.id===prev)||null;       // keep choice if still present; default none
    sel.value=midiOut?midiOut.id:'';
    if(st) st.textContent=outs.length+' out(s) ready';
  },e=>{ if(st) st.textContent='WebMIDI denied'; });
}

/* ---------- WebMIDI in: the FM-1 hardware drives this page (fm1-ctrl map) ----------
 * fm1-ctrl firmware (docs/maps/melodee.json v0.2, keyscan.c/encoders.c):
 *   notes  53..79  ch1  -> 27 keys F3..G5 (vel 100, 0x80 on release)
 *   CC 40..53 ch1  -> 14 buttons FX SEL ENV LFO EDIT GLO HOME SAVE ARP SEQ PLAY REC OCT- OCT+ (127/0)
 *   CC 20..26 ch1  -> encoders SELECT ALGORITHM PRESETS K1..4, relative 64±n
 *   CC 20..26 ch2  -> push+turn (second layer, 0xB1)
 *   CC 27          -> MASTER (abs 0..127)
 * ECHO GUARD (exact-match): a message we sent out within the last 250 ms is
 * dropped on input (IAC / thru loops our own stream back). Exact 3-byte match —
 * a blanket time window would swallow real hardware events mid-playback.      */
const BTN_CC0=40, ENC_CC0=20;
const keyStates=new Set();                       // notes currently held from hardware
let midiInId='all';                              // 'all' = bind every input
let echoRing=[];                                 // [{s:'90 35 64', t:ms}] recent out sends
const ECHO_MS=250;
function echoStamp(msg){                         // remember what we sent
  const now=performance.now();
  echoRing.push({s:[...msg].map(x=>x.toString(16).padStart(2,'0')).join(' '), t:now});
  if(echoRing.length>64) echoRing=echoRing.slice(-48);
}
function echoHit(st,d1,d2){
  const s=[st,d1,d2].map(x=>x.toString(16).padStart(2,'0')).join(' ');
  const now=performance.now();
  return echoRing.some(r=>r.s===s&&now-r.t<ECHO_MS);
}
function noteToKeyIdx(note){ return note-53; }   // key idx 0..26
function midiIn3(st,d1,d2){
  const t=st&0xF0, ch=st&0x0F;
  if(echoHit(st,d1,d2)) return;                  // our own echo — ignore, breaks the loop
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
    } else if((ch===0||ch===1)&&d1>=ENC_CC0&&d1<ENC_CC0+7){ // encoder turn (ch2 = push+turn)
      DSPanel.turnRole(d1-ENC_CC0, v-64);
    } else if(d1===27){                          // MASTER (absolute)
      DSPanel.masterVol(v);
    }
  }
}
function bindMidiIn(acc){
  const ins=[...acc.inputs.values()];
  const sel=$('midiins');
  if(sel){
    const sig=ins.map(i=>i.id+':'+i.name).join('|');
    if(sel.dataset.sig!==sig){                     // rebuild ONLY when the port list changed —
      sel.dataset.sig=sig;                         // never mid-interaction (Chrome reverts the
      const prev=midiInId;                         // native popup if options mutate on change)
      sel.innerHTML='<option value="all">— all inputs —</option>';
      for(const i of ins){ const op=document.createElement('option'); op.value=i.id; op.textContent=i.name; sel.appendChild(op); }
      sel.value=[...sel.options].some(o=>o.value===prev)?prev:'all';
    }
    sel.onchange=()=>{ midiInId=sel.value; bindMidiHandlers(acc); };   // rebind bytes only, DOM untouched
  }
  bindMidiHandlers(acc);
}
function bindMidiHandlers(acc){
  for(const i of acc.inputs.values()){
    const want = midiInId==='all' || i.id===midiInId;
    i.onmidimessage = want ? (e)=>{
      const d=e.data;
      if(!d||d.length<2||d[0]>=0xF0) return;     // ignore realtime/system
      midiIn3(d[0],d[1],d[2]||0);
    } : null;
  }
}

let page=0, started=false;
function ensureAudio(){
  if(!audioCtx) audioCtx=new (window.AudioContext||window.webkitAudioContext)();
  started=true;
}

/* crash guard: any uncaught error surfaces in the mini monitor + beacons back to server */
addEventListener('error',e=>{ crashBeacon(e.message||e.error); console.error(e.error||e.message); });
addEventListener('unhandledrejection',e=>{ crashBeacon('promise: '+(e.reason&&(e.reason.message||e.reason))); console.error('unhandled rejection:', e.reason); });

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
  DSPanel.onKey((pressed,down,released)=>{       // drum grid edits go through the drum handler;
    if(mode==='grid' && trkU8(selTrack,'channel')===9) drumKeyHandler(pressed,down,released);
    else stepEntry(pressed,down,released);       // melodic / step-entry keeps the 02 behavior
  });
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
