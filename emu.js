/* emu.js — DriftSeq web emulator shell (spec 03 screens, v1)
 * - 240×240 canvas HOME hero drift screen + SEQ grid page
 * - WebMIDI out (same byte stream the FM-1 will emit) / WebAudio beep fallback
 * - rAF loop drives ds_tick with real dt; UI sliders morph loop windows live
 */
'use strict';
const TRACKS=16, STEPS=64, CH=240;
let M=null, S=0, H8=null;
let OFF={}, TK={}, ST={};
let playing=false, lastT=0, rafId=0;
let selTrack=0, page=0, mode='home';
let midiOut=null, midiAccess=null;
let audioCtx=null;
const flash=new Float32Array(TRACKS);      // per-track bar flash decay

const $=id=>document.getElementById(id);
const screen=$('screen'), ctx=screen.getContext('2d');
const logEl=$('log');
function say(m){const d=document.createElement('div');d.textContent=m;logEl.prepend(d);while(logEl.children.length>40)logEl.lastChild.remove();}

const COLORS=[
  '#e05c5c','#e08b3c','#d9c94a','#7fd94a','#4ad9a8','#4ac5d9','#5c7fe0','#9a5ce0',
  '#e05ca8','#e05c7f','#d98a6b','#c9d96b','#6bd9c9','#8a6bd9','#d96b9a','#9ad96b'];

function u8(off){return H8[S+off];}
function i32(off){const d=new DataView(M.HEAPU8.buffer);return d.getInt32(S+off,true);}
function f32(off){const d=new DataView(M.HEAPU8.buffer);return d.getFloat32(S+off,true);}
function f64(off){const d=new DataView(M.HEAPU8.buffer);return d.getFloat64(S+off,true);}
function trkOff(t){return OFF.tracks+t*TK.stride;}
function trkU8(t,f){return H8[S+trkOff(t)+TK[f]];}
function stepOff(t,s){return trkOff(t)+TK.steps+s*ST.size;}
function stepU8(t,s,f){return H8[S+stepOff(t,s)+ST[f]];}
function voiceNote(t,s,v){return H8[S+stepOff(t,s)+ST.voices+v*4+0];}

function readMidiOut(){
  const n=i32(OFF.out_len), base=OFF.out;
  if(n>0){
    const bytes=H8.slice(S+base,S+base+n);
    if(midiOut){ try{ midiOut.send(bytes);}catch(e){} }
    for(let i=0;i<bytes.length;){
      const b=bytes[i];
      if(b<0x80){i++;continue;}
      const type=b&0xF0;
      if(type===0xC0||type===0xD0){i+=2;continue;}
      if(type===0x90&&bytes[i+2]>0){
        const ch=b&0x0F;
        for(let t=0;t<TRACKS;t++) if(trkU8(t,'channel')===ch){ flash[t]=1; break; }
        beep(bytes[i+1],bytes[i+2]);
      }
      i+=3;
    }
  }
}

function beep(note,vel){
  if(!audioCtx) return;
  const o=audioCtx.createOscillator(), g=audioCtx.createGain();
  // crude GM-ish: map channel-less; short blip
  o.type='square'; o.frequency.value=440*Math.pow(2,(note-69)/12);
  g.gain.value=Math.min(0.12,vel/127*0.12);
  o.connect(g).connect(audioCtx.destination);
  o.start(); g.gain.setTargetAtTime(0.0001,audioCtx.currentTime+0.08,0.04); o.stop(audioCtx.currentTime+0.25);
}

/* ---------- render: HOME hero ---------- */
function draw(){
  ctx.fillStyle='#000'; ctx.fillRect(0,0,CH,CH);
  // top bar
  ctx.fillStyle='#181820'; ctx.fillRect(0,0,CH,20);
  ctx.fillStyle='#d9d9e8'; ctx.font='11px monospace';
  const bpm=Math.round(f64(OFF.bpm));
  ctx.fillText(`DriftSeq  ${bpm} BPM  ${playing?'PLAY':'STOP'}`,6,14);
  ctx.fillStyle= selTrack>=0?'#7fd94a':'#555';
  ctx.fillText(`TRK ${String(selTrack+1).padStart(2,'0')}`,196,14);

  // 16 bars, main area y 24..216
  const barH=11, gapY=1, x0=28, x1=232, w=x1-x0;
  for(let t=0;t<TRACKS;t++){
    const y=24+t*(barH+gapY);
    const muted=trkU8(t,'mute');
    const start=trkU8(t,'loop_start'), len=trkU8(t,'loop_len');
    // dim full 64
    ctx.fillStyle= muted?'#141418':'#1d1d26';
    ctx.fillRect(x0,y,w,barH);
    // lit window
    if(!muted){
      ctx.fillStyle=COLORS[t];
      ctx.globalAlpha=0.28;
      for(let k=0;k<len;k++){
        const px=x0+((start+k)&63)*w/64;
        ctx.fillRect(px,y,w/64-0.5,barH);
      }
      ctx.globalAlpha=1;
      // voices solid
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
      // playhead
      const pos=i32(OFF.pos16+t*4);
      const rel=(pos+64-start)&63;
      if(rel<len&&playing){
        const px=x0+pos*w/64;
        ctx.fillStyle='#fff';
        ctx.fillRect(px-1,y,2,barH);
      }
    }
    if(t===selTrack){ ctx.strokeStyle='#7fd94a'; ctx.strokeRect(x0-2.5,y-1,w+5,barH+2); }
    flash[t]*=0.88;
  }
  // bottom K labels
  ctx.fillStyle='#181820'; ctx.fillRect(0,220,CH,20);
  ctx.fillStyle='#8a8a99'; ctx.font='10px monospace';
  ctx.fillText('K1 LEN  K2 MUTE  K3 START  K4 PROB',6,233);
}

/* ---------- SEQ grid (current page, selected track) ---------- */
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
  ctx.fillText('click=toggle · sliders below morph window live',6,233);
}

/* ---------- main loop ---------- */
function frame(t){
  if(!lastT)lastT=t;
  let dt=(t-lastT)/1000; lastT=t;
  if(dt>0.25)dt=0.25;
  if(playing){ M._ds_tick_wasm(dt); readMidiOut(); }
  if(mode==='home')draw(); else drawGrid();
  rafId=requestAnimationFrame(frame);
}

/* ---------- wiring ---------- */
function buildTrackButtons(){
  const holder=$('tracks'); holder.innerHTML='';
  for(let t=0;t<TRACKS;t++){
    const b=document.createElement('button');
    b.className='trackbtn'; b.textContent=String(t+1);
    b.onclick=()=>selectTrack(t);
    holder.appendChild(b);
  }
  refreshTrackButtons();
}
function refreshTrackButtons(){
  const bs=$('tracks').children;
  for(let t=0;t<TRACKS;t++){
    bs[t].classList.toggle('on',t===selTrack);
    bs[t].classList.toggle('muted',!!trkU8(t,'mute'));
  }
}
function selectTrack(t){
  selTrack=t; M._ds_select_track_wasm(t);
  $('t_start').value=trkU8(t,'loop_start');
  $('t_len').value=trkU8(t,'loop_len');
  refreshTrackButtons();
}
function refreshMidi(){
  if(!navigator.requestMIDIAccess){ $('webmidi-state').textContent='WebMIDI unavailable (use Chrome/Edge)'; return; }
  navigator.requestMIDIAccess({sysex:false}).then(acc=>{
    midiAccess=acc; const sel=$('midiouts'); sel.innerHTML='';
    const outs=[...acc.outputs.values()];
    if(!outs.length){ sel.innerHTML='<option>— none (WebAudio beeps) —</option>'; return; }
    for(const o of outs){ const op=document.createElement('option'); op.value=o.id; op.textContent=o.name; sel.appendChild(op); }
    sel.onchange=()=>{ midiOut=outs.find(o=>o.id===sel.value)||null; say('MIDI out → '+ (midiOut?midiOut.name:'none')); };
    midiOut=outs.find(o=>o.id===sel.value)||null;
    $('webmidi-state').textContent=`${outs.length} out(s)`;
  },e=>{ $('webmidi-state').textContent='WebMIDI denied'; });
}

async function boot(){
  M=await DriftSeqModule();
  S=M._ds_new(42n)|0;
  H8=M.HEAPU8;
  OFF={
    out:M._ds_offset_out(), out_len:M._ds_offset_out_len(),
    pos16:M._ds_offset_pos16(), frac16:M._ds_offset_frac16(),
    bpm:M._ds_offset_bpm(),
    tracks:M._ds_track_off(),
  };
  TK={stride:M._ds_track_stride(), channel:M._ds_trk_off_channel(), prog:M._ds_trk_off_prog(),
      loop_start:M._ds_trk_off_loop_start(), loop_len:M._ds_trk_off_loop_len(),
      division:M._ds_trk_off_division(), mute:M._ds_trk_off_mute(), steps:M._ds_trk_off_steps()};
  ST={size:M._ds_step_size(), on:M._ds_step_off_on(), nvoices:M._ds_step_off_nvoices(),
      prob:M._ds_step_off_prob(), voices:M._ds_step_off_voices()};
  buildTrackButtons();

  $('b_play').onclick=()=>{ if(!audioCtx)audioCtx=new (window.AudioContext||window.webkitAudioContext)(); M._ds_play_wasm(); playing=true; $('b_play').classList.add('on'); };
  $('b_stop').onclick=()=>{ M._ds_stop_wasm(); playing=false; $('b_play').classList.remove('on'); };
  $('bpm').oninput=e=>{ M._ds_set_bpm_wasm(+e.target.value); $('bpmv').textContent=e.target.value; };
  $('sw').oninput=e=>M._ds_set_swing_wasm(1,+e.target.value);
  $('t_start').oninput=e=>M._ds_set_loop_wasm(selTrack,+e.target.value,-1);
  $('t_len').oninput=e=>M._ds_set_loop_wasm(selTrack,-1,+e.target.value);
  $('b_eu').onclick=()=>{ M._ds_euclid_wasm(selTrack,1,+$('t_k').value||trkU8(selTrack,'eu_k')||4,+$('t_n').value,0); say(`Euclid applied to trk ${selTrack+1}`); };
  $('b_clear').onclick=()=>{ M._ds_clear_track_wasm(selTrack); };
  $('b_refresh').onclick=refreshMidi;
  $('b_grid').onclick=()=>{ mode = mode==='home' ? 'grid' : 'home'; };

  screen.onclick=e=>{
    if(mode!=='grid')return;
    const r=screen.getBoundingClientRect();
    const x=(e.clientX-r.left)*CH/r.width, y=(e.clientY-r.top)*CH/r.height;
    if(y<24||y>216)return;
    const cols=16,rows=4, cw=(CH-8)/cols, chh=(CH-60)/rows;
    const c=Math.floor((x-4)/cw), rr=Math.floor((y-24)/chh);
    if(c<0||c>15||rr<0||rr>3)return;
    const s=page*16+rr*16+c;
    M._ds_toggle_step_wasm(selTrack,s);
  };
  window.addEventListener('keydown',e=>{
    if(e.code==='Space'){e.preventDefault(); playing?$('b_stop').onclick():$('b_play').onclick();}
    if(e.key>='1'&&e.key<='8')selectTrack(+e.key-1);
    if(e.key==='g'||e.key==='G')mode= mode==='home'?'grid':'home';
  });
  refreshMidi();
  selectTrack(0);
  rafId=requestAnimationFrame(frame);
  say('engine loaded · demo pattern (seed 42) · press SPACE or ▶ PLAY');
}
boot().catch(e=>{ say('BOOT FAIL: '+e.message); console.error(e); });
