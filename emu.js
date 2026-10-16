/* emu.js — DriftSeq emu glue: the FM-1 panel (panel.js) drives the pure-C engine (wasm);
 * the 240×240 canvas is the instrument's screen (specs 03: HOME hero + SEQ grid);
 * the strip under the device is the MIDI-out monitor (same bytes WebMIDI sends).
 */
'use strict';
const BUILD='r14g 10-16';
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
      const isDrum=trkU8(t,'channel')===9;
      for(let s=0;s<STEPS;s++){
        const nv=stepU8(t,s,'nvoices');
        if(!stepU8(t,s,'on')||!nv) continue;
        const rel=(s+64-start)&63;
        if(rel>=len) continue;
        const px=x0+s*w/64, bw=Math.max(1.5,w/64-1);
        if(isDrum){
          // rev.14e: 2.75px block can't hold a dot matrix — horizontal mini-lines instead:
          // each distinct drum in the step = one 1px line stacked top->bottom in lane order
          // (same lane order/colors as the DRUM GRID). Melodic rows keep the solid block.
          ctx.fillStyle=COLORS[t];
          ctx.globalAlpha=0.18; ctx.fillRect(px,y,bw,barH); ctx.globalAlpha=1;
          let mask=0;
          const vB=trkOff(t)+TK.steps+s*ST.size+ST.voices;
          for(let vi=0;vi<nv;vi++){
            const dn=H8[S+vB+vi*4];
            const li=GM_DRUMS.indexOf(dn);
            if(li>=0) mask|=1<<li;
          }
          let row=0;
          for(let li=0;li<9&&row<4;li++){                 // 11px bar fits 4 lines of (1+2)
            if(!(mask&(1<<li))) continue;
            ctx.fillStyle=DPAL[li%DPAL.length];
            ctx.fillRect(px,y+1+row*3,Math.max(1.5,bw),1.5);
            row++;
          }
        } else {
          ctx.globalAlpha= stepU8(t,s,'prob')<100?0.55:1;
          ctx.fillStyle=COLORS[t];
          ctx.fillRect(px,y,bw,barH);
          ctx.globalAlpha=1;
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
  ctx.font='10px monospace';                     // rev.14g: no K-prefixes (fixed knob row, John)
  const HX=[8,72,132,188];
  ['START','LEN','MUTE','PROB'].forEach((l,i)=>{ ctx.fillStyle='#8a8a99'; ctx.fillText(l,HX[i],233); });
  ctx.fillStyle='#4a4a54'; ctx.font='8px monospace';
  ctx.fillText(BUILD,234,238);                   // build stamp every page (John: verify freshness at a glance)
}

/* ---------- the screen: SEQ grid (03-screens) ---------- */
function drawGrid(){
  if(trkU8(selTrack,'channel')===9) return drawDrumGrid();      // ch10 = drum lanes view
  ctx.fillStyle='#000'; ctx.fillRect(0,0,CH,CH);
  ctx.fillStyle='#181820'; ctx.fillRect(0,0,CH,16);
  ctx.fillStyle='#d9d9e8'; ctx.font='11px monospace';
  ctx.fillText(`SEQ TRK ${selTrack+1} · p1/4`,6,12);
  ctx.fillStyle= trkU8(selTrack,'mute')?'#5a5a62':'#5fe08a';
  const cn=chordName(selTrack,cursor);
  ctx.fillText(`st${cursor+1}${cn?' '+cn:''}`,150,12);
  if(keyShift!==0){ ctx.fillStyle='#e6c84a'; ctx.fillText(`${keyShift>0?'+':''}${keyShift}`,120,12); }
  const cols=16,rows=4, x0=18, y0=16, gw=CH-x0, gh=96;          // rev.12: 4x24px bars
  const cw=(gw-16)/cols, chh=gh/rows;
  const start=trkU8(selTrack,'loop_start'), len=trkU8(selTrack,'loop_len');
  ctx.font='8px monospace';
  for(let r=0;r<rows;r++)for(let c=0;c<cols;c++){
    const s=r*16+c;                                             // row = page-of-16, historical
    const x=x0+c*cw, y=y0+r*chh;
    const rel=(s+64-start)&63, inWin=rel<len;
    // rev.11/12: bg luminance = loop range (bright inside / dark outside), empty = plain
    ctx.fillStyle=inWin?'#2a2a33':'#16161c';
    ctx.fillRect(x+1,y+1,cw-2,chh-2);
    const vs=sortedVoices(selTrack,s);                          // pitch-ascending, high on top
    const on=stepU8(selTrack,s,'on');
    for(let li=0;li<vs.length;li++){
      const v=vs[li], lineY=y+chh-4-li*5;                       // slot i: bottom-up (high pitch top)
      if(!on){                                                  // rest: hollow/dashed line
        ctx.fillStyle='#666672'; ctx.fillRect(x+3,lineY,cw-6,1);
      } else {
        ctx.fillStyle=`hsl(${v.note%12*30},72%,55%)`;
        ctx.fillRect(x+2,lineY,cw-4,2);
      }
    }
    const pos=i32(OFF.pos16+selTrack*4), isNow=playing&&pos===s;
    if(isNow){ctx.strokeStyle='#fff';ctx.strokeRect(x+1.5,y+1.5,cw-3,chh-3);}
    if(cursor===s){ctx.strokeStyle='#e6c84a';ctx.lineWidth=1.5;ctx.strokeRect(x+0.5,y+0.5,cw-1,chh-1);ctx.lineWidth=1;}
    if(c===0){ctx.fillStyle='#8a8a99';ctx.fillText(`P${r+1}`,3,y+13);}
  }
  drawEditPanel(96);                                            // rev.12: EDIT embedded, y=112..
  drawFooter(focusLayer==='edit');
}

/* ---------- rev.12/13c: EDIT panel embedded at bottom of GRID (melodic 4 cards / drum 1 big card) ---------- */
let focusLayer='grid';                            // 'grid' | 'edit' — EDIT button toggles
function drawEditPanel(y0){                       // y0=96 (melodic, 4 compact cards) — drum calls its own
  const t=selTrack, s=cursor;
  const sBase=trkOff(t)+TK.steps+s*ST.size;
  const vs=sortedVoices(t,s), nv=vs.length;
  ctx.fillStyle='#101015'; ctx.fillRect(0,y0+16,CH,100);
  ctx.fillStyle='#3a3a44'; ctx.fillRect(0,y0+16,CH,1);          // separator
  const cardH=25;
  for(let vi=0;vi<4;vi++){
    const y=y0+16+vi*cardH, active=(focusLayer==='edit')&&vi===editVoice;
    if(vi>=nv){ ctx.fillStyle='#16161b'; ctx.fillRect(2,y+1,CH-4,cardH-2); }  // empty slot: plain dark
    if(active){ctx.strokeStyle='#e6c84a';ctx.strokeRect(2.5,y+1.5,CH-5,cardH-3);}
    if(vi>=nv){
      ctx.fillStyle='#55555f'; ctx.font='10px monospace';
      ctx.fillText(`v${vi+1} ─ 空 (弹键加入)`,8,y+16); continue;
    }
    const v=vs[vi];
    const on=stepU8(t,s,'on');
    const sym=!on?'●':(playing&&i32(OFF.pos16+t*4)===s)?'▶':'○';
    const nm2=`${NAMES[v.note%12]}${Math.floor(v.note/12)-1}`;
    const g2=v.gate===0xFF?'∞':(v.gate/4).toFixed(2);
    const prob=stepU8(t,s,'prob');
    const hue=v.note%12*30, desat=prob/100;                      // prob: 1=pure hue, 0=grey
    const frac=(v.gate===0xFF)?1:Math.min(1,(v.gate/4)/8);       // gate 8.00 = full
    // rev.14c: the WHOLE row background is the param bar (John: strip under text too fiddly)
    ctx.fillStyle=`hsl(0,0%,${18+(v.vel/127)*55}%)`;             // bg luminance = velocity
    ctx.fillRect(3,y+1,CH-6,cardH-2);
    ctx.fillStyle=`hsl(${hue},${Math.round(72*desat)}%,52%)`;
    ctx.fillRect(3,y+1,Math.max(3,(CH-6)*frac),cardH-2);         // bar length = gate
    if(v.gate===0xFF&&playing){ctx.fillStyle='#fff';ctx.fillRect(CH-6,y+1,3,cardH-2);}
    // text on top, 11px, dark 1px shadow; note name padded to 3 chars so the
    // numeric columns align no matter the note-name length (C4 vs C#4 vs G#5)
    ctx.font='11px monospace';
    const nm3=(nm2+'   ').slice(0,3);
    const segs=[`v${vi+1}${sym}`,nm3,`g${g2}`,`v${v.vel}`,`p${prob}`];
    const xs=[7,34,84,140,188];                       // fixed columns
    segs.forEach((sg,i)=>{
      ctx.fillStyle='rgba(0,0,0,.85)'; ctx.fillText(sg,xs[i]+1,y+17);
      ctx.fillStyle=active?'#fff4d6':'#f0f0f6'; ctx.fillText(sg,xs[i],y+16);
    });
  }
}
const FOOT_GRID=['STEP','LEN','MUTE','PROB'];
const FOOT_EDIT=['PIT','GAT','VEL','PROB','SEL'];
const FOOT_X=[8,72,132,188];                       // fixed columns under K1..K4 (no 'K1' prefix)
function drawFooter(editFocus){
  ctx.fillStyle='#181820'; ctx.fillRect(0,216,CH,24);
  ctx.font='10px monospace';                        // bigger: K labels are implicit (fixed row)
  const labels=editFocus?FOOT_EDIT:FOOT_GRID, col=editFocus?'#e6c84a':'#8a8a99';
  for(let i=0;i<4;i++){ ctx.fillStyle=col; ctx.fillText(labels[i],FOOT_X[i],233); }
  if(editFocus){ ctx.fillStyle='#e6c84a'; ctx.fillText(labels[4],FOOT_X[3]+40,233); } // SEL after PROB
  ctx.fillStyle='#4a4a54'; ctx.font='8px monospace'; ctx.fillText(BUILD,164,238);
}

/* ---------- the screen: drum lanes (03 §3 — ALL drums visible, selected lane lit) ---------- */
function drawDrumGrid(){
  ctx.fillStyle='#000'; ctx.fillRect(0,0,CH,CH);
  ctx.fillStyle='#181820'; ctx.fillRect(0,0,CH,16);
  ctx.fillStyle='#d9d9e8'; ctx.font='11px monospace';
  ctx.fillText(`DRUM TRK ${selTrack+1} · p1/4`,6,12);
  ctx.fillStyle= trkU8(selTrack,'mute')?'#5a5a62':'#5fe08a';
  ctx.fillText(`st${cursor+1} ${GM_SHORT[drumSel]||('D'+drumSel)}`,150,12);
  const nL=GM_DRUMS.length, lh=11, x0=30, y0=16;               // rev.13b: 9x11px lanes
  const pos=i32(OFF.pos16+selTrack*4);
  ctx.font='8px monospace';
  for(let li=0;li<nL;li++){
    const y=y0+li*lh, sel=li===drumSel;
    ctx.fillStyle=sel?'#c9c9d4':'#6a6a76';
    ctx.fillText(GM_SHORT[li],2,y+lh-3);
    for(let c=0;c<16;c++){
      const s=page*16+c;
      const vBase=trkOff(selTrack)+TK.steps+s*ST.size;
      let on=false;
      for(let vi=0;vi<stepU8(selTrack,s,'nvoices');vi++) if(H8[S+vBase+vi*4]===GM_DRUMS[li]) on=true;
      const cw=(CH-4-x0)/16;
      ctx.globalAlpha=sel?1:0.35;
      ctx.fillStyle=on?DPAL[li%DPAL.length]:(sel?'#1d1d26':'#15151a');
      ctx.fillRect(x0+c*cw+1,y+1,cw-2,lh-2);
      if(playing&&pos===s){ctx.globalAlpha=1;ctx.fillStyle='#fff';ctx.fillRect(x0+c*cw,y,cw,1);}
    }
    ctx.globalAlpha=1;
  }
  // ---- rev.13c: single big card for drumSel (100px zone) ----
  const py=115;
  ctx.fillStyle='#101015'; ctx.fillRect(0,py,CH,101);
  ctx.fillStyle='#3a3a44'; ctx.fillRect(0,py,CH,1);
  const active=focusLayer==='edit';
  ctx.fillStyle=active?'#2e2e3a':'#16161b'; ctx.fillRect(2,py+3,CH-4,94);
  if(active){ctx.strokeStyle='#e6c84a';ctx.strokeRect(2.5,py+3.5,CH-5,93);}
  const s=cursor, vBase=trkOff(selTrack)+TK.steps+s*ST.size;
  let hit=-1;
  for(let vi=0;vi<stepU8(selTrack,s,'nvoices');vi++) if(H8[S+vBase+vi*4]===GM_DRUMS[drumSel]) hit=vi;
  const on=stepU8(selTrack,s,'on');
  const col=DPAL[drumSel%DPAL.length];
  ctx.fillStyle=col; ctx.font='14px monospace';
  ctx.fillText(`${GM_SHORT[drumSel]} ${hit>=0?(on?'●':'○'):'─'}`,10,py+22);
  if(hit>=0){
    const vel=H8[S+vBase+hit*4+1], gate=H8[S+vBase+hit*4+2], prob=stepU8(selTrack,s,'prob');
    ctx.fillStyle='#fff4d6'; ctx.font='11px monospace';
    ctx.fillText(`V${vel}  G${gate===0xFF?'∞':(gate/4).toFixed(2)}  P${prob}%`,70,py+22);
    const bx=10,bw=CH-20,by=py+32,bh=52;
    ctx.fillStyle=`hsl(0,0%,${18+(vel/127)*55}%)`; ctx.fillRect(bx,by,bw,bh);
    const frac=gate===0xFF?1:Math.min(1,(gate/4)/8);
    ctx.fillStyle=col;                                          // instrument color = identity
    const desat=prob/100;
    ctx.globalAlpha=0.35+0.65*desat;                            // desat proxy via alpha over grey bg
    ctx.fillRect(bx,by,Math.max(2,bw*frac),bh);
    ctx.globalAlpha=1;
  } else {
    ctx.fillStyle='#55555f'; ctx.font='10px monospace';
    ctx.fillText('此鼓此步无音 — 白键 toggle 写入',10,py+55);
  }
  drawFooter(focusLayer==='edit');
}

/* ---------- rev.13 helpers: sorted voices + chord name (03 §4 rev.11) ---------- */
function sortedVoices(t,s){                      // -> [{note,vel,gate,rest}] pitch-ascending
  const vBase=trkOff(t)+TK.steps+s*ST.size+ST.voices;
  const nv=stepU8(t,s,'nvoices'), out=[];
  for(let vi=0;vi<nv;vi++) out.push({note:H8[S+vBase+vi*4],vel:H8[S+vBase+vi*4+1],gate:H8[S+vBase+vi*4+2]});
  out.sort((a,b)=>a.note-b.note);
  return out;
}
const NAMES=['C','C#','D','D#','E','F','F#','G','G#','A','A#','B'];   // module-level (rev.14: helpers + panels share it)
const SCALE_NAMES=['','m','m','m','m','','m','m','m','m','m','dim'];  // crude maj/min quality (v1)
function chordName(t,s){                         // 'Cmaj7' style: root + quality by interval set
  const vs=sortedVoices(t,s);
  if(vs.length===0) return '';
  if(vs.length===1){const n=vs[0].note; return NAMES[n%12]+(Math.floor(n/12)-1);}
  const root=vs[0].note%12, set=new Set(vs.map(v=>v.note%12));
  const iv=[...set].map(x=>(x-root+12)%12).sort((a,b)=>a-b).join(',');
  let q='';                                      // common triads/7ths
  if(iv==='0,4,7')q='maj'; else if(iv==='0,3,7')q='m';
  else if(iv==='0,4,7,11')q='maj7'; else if(iv==='0,3,7,10')q='m7';
  else if(iv==='0,3,6')q='dim'; else if(iv==='0,4,8')q='aug';
  else if(iv==='0,5,7')q='sus4'; else if(iv==='0,2,7')q='sus2';
  else q=null;
  return q===null ? vs.map(v=>NAMES[v.note%12]).join(' ') : NAMES[root]+q;
}

/* ---------- step-entry is the DEFAULT state (02 §2.1 rev.5): key press writes the cursor step ---------- */
let cursor=0;                                    // absolute step 0..63 — always present, cursor = selection
const liveHeld=new Set();                        // panel keys currently sounding (note-off dedup)
/* Key id → is it a black key? panel.js key ids are CHROMATIC (0..26 = F3..G5), NOT
 * "white 0..15 then black 16..26". Whites = [0,2,4,6,7,9,11,12,14,16,18,19,21,23,24,26],
 * blacks = [1,3,5,8,10,13,15,17,20,22,25]. John's report ("black key toggles step 2")
 * came from treating id>=16 as black — id16 is actually a WHITE key (C5)! */
const KEY_BLACK=new Set([1,3,5,8,10,13,15,17,20,22,25]);
const KEY_BLACK_IDX=[1,3,5,8,10,13,15,17,20,22,25];   // array form for ordinal lookup
const KEY_WHITE=[0,2,4,6,7,9,11,12,14,16,18,19,21,23,24,26];
const WHITE_POS=k=>KEY_WHITE.indexOf(k);         // white key ordinal 0..15 = step in page
/* Drum GRID editing (spec 02 §2.1 / 03 §2.3): black keys = pick instrument, white keys =
 * toggle that drum across the 16 steps of the keybed page. TR-808 muscle memory. */
let drumSel=0;                                   // index into GM_DRUMS (black-key order)
const GM_SHORT=['KCK','SNR','CHH','OHH','CUP','TOM','RID','PERC','SHK','CAB','MAR'];
function drumKeyHandler(pressed,down,released){
  const t=selTrack, inGrid=mode==='grid';
  for(const k of (pressed||[])){
    if(KEY_BLACK.has(k)){                        // black key: pick drum (audition, light LED)
      drumSel=KEY_BLACK_IDX.indexOf(k);          // black-key order = instrument order
      liveNote(k,true);                          // hear it
    } else if(inGrid){
      const s=page*16+WHITE_POS(k);              // white key ordinal = step in page
      const vBase=trkOff(t)+TK.steps+s*ST.size;
      let hit=-1;
      for(let vi=0;vi<stepU8(t,s,'nvoices');vi++) if(H8[S+vBase+vi*4]===GM_DRUMS[drumSel]) hit=vi;
      if(hit>=0) M._ds_voice_remove_wasm(t,s,GM_DRUMS[drumSel]);   // engine removes by note
      else M._ds_voice_add_wasm(t,s,GM_DRUMS[drumSel],100,1);
      liveNote(KEY_BLACK[drumSel],true);         // audition via that drum's black key note
    } else liveNote(k,true);                     // non-grid (step-entry/PLAY): melodic path
  }
  for(const k of (released||[])) if(KEY_BLACK.has(k)||!inGrid) liveNote(k,false);
}

function liveNote(keyIdx,down){                  // panel key -> live preview note through the engine
  const t=selTrack;
  let note;
  if(trkU8(t,'channel')===9){                    // drums: fixed GM map (OCT shift meaningless)
    const gm=[36,38,42,46,49,45,41,43,51,47,39,56,37,54,40,58,62,63,27,34];
    note=gm[keyIdx%gm.length];
  } else note=Math.max(0,Math.min(127,53+keyIdx+keyShift));   // F3 = 53, + keyShift (rev.7)
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
  const t=selTrack, isDrum=trkU8(t,'channel')===9;
  if(pressed&&pressed.length){
    // rev.11 OVERRIDE: one press defines the whole step content (<=4 = chord). Collect
    // all keys in this down-mask, write once; rest step + key = rewrite & revive.
    liveNote(pressed[0],true);
    const notes=[];
    for(const keyIdx of pressed){
      let note;
      if(isDrum){
        const bi=KEY_BLACK_IDX.indexOf(keyIdx);
        if(bi<0) continue;                        // white key = toggle path (drumKeyHandler), skip here
        note=GM_DRUMS[bi];
      } else note=53+keyIdx;
      notes.push(Math.max(0,Math.min(127,note+keyShift)));    // rev.7: OCT± shifts write pitch
    }
    if(notes.length){
      if(notes.length>4) return;                  // >4 keys = reject (LED flash v1.1)
      for(let vi=stepU8(t,cursor,'nvoices')-1;vi>=0;vi--)      // clear step: override, not append
        M._ds_voice_remove_wasm(t,cursor,H8[S+trkOff(t)+TK.steps+cursor*ST.size+ST.voices+vi*4]);
      H8[S+stepOff(t,cursor)+ST.on]=1;            // revive rest steps on write
      for(const n of notes) M._ds_voice_add_wasm(t,cursor,n,100,1);
      editVoice=0;
    }
  }
  for(const keyIdx of (released||[])) liveNote(keyIdx,false);   // key up -> note-off
  if(pressed&&pressed.length) cursor=Math.min(63,cursor+1);  // rev.9: advance, dead-stop at 63
}

/* ---------- rev.5 cursor axis + OCT rest/tie (02 §2.1 rev.3-5) ----------
 * K1 = pure cursor axis: left = back one, right = forward one, writes nothing.
 * GLO long-press + K1 = ×16 (one page per detent). OCT+ cycles rest→tie→restore
 * on the cursor step WITHOUT moving; OCT− = back (same as K1 left, paired). */
let gloHeld=false, restState=new Map();          // restState: step -> 0 none / 1 rest / 2 tie / 3 original-gate
function cursorMove(n){                          // n = detents (+ fwd / - back)
  const step=(gloHeld?16:1)*(n>0?1:-1)*Math.abs(n);
  const next=cursor+step;                        // rev.9: NO wrap — endpoints dead-stop (John: blind-operable)
  cursor=Math.max(0,Math.min(63,next));
}
/* ---------- rev.7: OCT± = keyboard interval shift while in GRID (John: the F3–G5
 * keybed is only ~2 octaves — OCT must shift the write pitch, not flip pages) ---------- */
let keyShift=0;                                  // semitones added to written/auditioned notes (±12/±24)
function octShift(dir){                          // dir = ±1 → ±12; clamp at ±24
  const next=keyShift+dir*12;
  if(next>=-24&&next<=24) keyShift=next;
}
function restTieCycle(){                         // EDIT page GLO+K4: none→rest→tie→none
  const t=selTrack, s=cursor;
  const cur=restState.get(s)||0;
  const next=(cur+1)%3;                          // 0 none, 1 rest, 2 tie
  restState.set(s,next);
  const vB=trkOff(t)+TK.steps+s*ST.size+ST.voices;           // v[0]: note,vel,gate,on (4B stride)
  if(next===1) H8[S+stepOff(t,s)+ST.on]=0;                   // rest: en=0, voices kept
  else if(next===2){                                         // tie: v[0].gate = 0xFF (until next event)
    H8[S+vB+3]=H8[S+vB+2]<0xFF?H8[S+vB+2]:H8[S+vB+3];        // save old gate in v[0].on scratch slot
    H8[S+vB+2]=0xFF;
  }
  else{                                                      // restore: en back, gate to pre-cycle value
    H8[S+stepOff(t,s)+ST.on]=1;
    if(cur===2) H8[S+vB+2]=H8[S+vB+3]||4;                    // v[0].on is scratch here; engine re-derives
  }
}

/* ---------- the screen: EDIT page (03 §3 rev.6) — voice cards + step params ---------- */
const COND_NAMES=['—','每2圈','每3圈','每4圈','每5圈','每6圈','每7圈','仅首圈','末4圈','FILL','flam'];
let editVoice=0;                                 // active voice 0..3 (SELECT knob cycles)
let condZoom=0;                                  // >0 = cond zoom feedback timer (frames)
function drawEdit(){  // DEPRECATED rev.12 — embedded panel replaced standalone page; kept for reference
  ctx.fillStyle='#000'; ctx.fillRect(0,0,CH,CH);
  const t=selTrack, s=cursor;
  const vBase=trkOff(t)+TK.steps+s*ST.size+ST.voices;
  const nv=stepU8(t,s,'nvoices');
  ctx.fillStyle='#181820'; ctx.fillRect(0,0,CH,20);
  ctx.font='11px monospace';
  const v0n=(nv>0)?H8[S+vBase]:null;
  const isDrum=trkU8(t,'channel')===9;
  let nm='';
  if(v0n!==null&&v0n!==0xFF){
    nm = isDrum ? ` ${GM_SHORT[GM_DRUMS.indexOf(v0n)]||('D'+v0n)}` : ` ${NAMES[v0n%12]}${Math.floor(v0n/12)-1}`;
  }
  ctx.fillStyle='#d9d9e8'; ctx.fillText(`EDIT TRK ${t+1} · st${s+1}${nm}`,6,14);
  // voice cards: 4 rows, active card inverted
  for(let vi=0;vi<4;vi++){
    const y=24+vi*47, active=vi===editVoice;
    ctx.fillStyle=active?'#2a2a36':'#141419';
    ctx.fillRect(4,y,CH-8,43);
    if(active){ ctx.strokeStyle='#e6c84a'; ctx.strokeRect(4.5,y+0.5,CH-9,42); }
    const note=(vi<nv)?H8[S+vBase+vi*4]:0xFF;
    const vel =(vi<nv)?H8[S+vBase+vi*4+1]:0;
    const gate=(vi<nv)?H8[S+vBase+vi*4+2]:0;
    ctx.fillStyle=active?'#fff4d6':'#9a9aa6'; ctx.font='13px monospace';
    if(note===0xFF){
      ctx.fillText(`v${vi+1}  ─ 空  (弹黑键加入)`,14,y+27);
    } else {
      const on=stepU8(t,s,'on');
      const sym=!on?'●':(playing&&i32(OFF.pos16+t*4)===s)?'▶':'○';   // ●=silent-keep ▶=sounding ○=armed
      const gn=gate===0xFF?'∞':(gate/4).toFixed(2);
      const nm2=isDrum?(GM_SHORT[GM_DRUMS.indexOf(note)]||('D'+note)):`${NAMES[note%12]}${Math.floor(note/12)-1}`;
      ctx.fillText(`v${vi+1} ${sym} ${nm2}`,14,y+27);
      ctx.font='10px monospace'; ctx.fillStyle='#8a8a99';
      ctx.fillText(`${String(note).padStart(3,'0')}`,118,y+27);
      ctx.fillText(`V${vel} G${gn}`,150,y+27);
    }
  }
  // footer: K1..K4 values for the active voice (K4 = step prob)
  const sBase=trkOff(t)+TK.steps+s*ST.size;
  const av=Math.min(editVoice,Math.max(0,nv-1));
  const aGate=(nv>0&&av<nv)?H8[S+vBase+av*4+2]:1;
  const cond=H8[S+sBase+ST.cond];
  ctx.fillStyle='#181820'; ctx.fillRect(0,216,CH,24);
  ctx.font='10px monospace';
  const vIdx=av+1;
  if(condZoom>0){
    ctx.fillStyle='#fff4d6'; ctx.font='12px monospace';
    ctx.fillText(`COND ${cond+1}/11 ${COND_NAMES[cond]}`,10,231);
  } else {
    ctx.fillStyle='#d9d9e8';
    ctx.fillText(`PIT ${nv?('v'+vIdx):'—'}`,10,226);
    ctx.fillText(`GAT ${aGate===0xFF?'∞':(aGate/4).toFixed(2)}`,62,226);
    ctx.fillText(`VEL ${nv?H8[S+vBase+av*4+1]:'—'}`,128,226);
    ctx.fillText(`PRB ${stepU8(t,s,'prob')}%`,186,226);
    ctx.fillStyle='#6a6a76';
    ctx.fillText('K1 pit K2 gat K3 vel K4 prob SEL=v',10,237);
    ctx.fillStyle='#4a4a54'; ctx.font='8px monospace'; ctx.fillText(BUILD,192,237);
  }
  if(condZoom>0) condZoom--;
}

/* ---------- EDIT page keys (03 §3.2): any key = add voice at its pitch + make active;
 * same-note replay on the active card = remove it (STEP-ENTRY semantics reused).
 * rev.8: white keys write too (John: black-key-only was arbitrary — the keybed is
 * the input surface in EDIT just like in GRID). Drums map through GM_DRUMS. ---------- */
function editKeyHandler(pressed,down,released){   // rev.14a: EDIT layer — cursor NEVER advances
  const t=selTrack, s=cursor;
  const isDrum=trkU8(t,'channel')===9;
  for(const k of (pressed||[])){
    liveNote(k,true);                              // hear it
    const note=isDrum
      ? GM_DRUMS[KEY_BLACK_IDX.indexOf(k)>=0?KEY_BLACK_IDX.indexOf(k):KEY_WHITE.indexOf(k)%GM_DRUMS.length]
      : Math.max(0,Math.min(127,53+k+keyShift));
    if(isDrum&&note<0) continue;                   // drum track, unmapped key
    const vB=trkOff(t)+TK.steps+s*ST.size+ST.voices;
    let hit=-1;
    for(let vi=0;vi<stepU8(t,s,'nvoices');vi++) if(H8[S+vB+vi*4]===note) hit=vi;
    if(hit>=0){                                    // same note = delete that voice (any card)
      M._ds_voice_remove_wasm(t,s,note);
      if(editVoice>=stepU8(t,s,'nvoices')) editVoice=Math.max(0,stepU8(t,s,'nvoices')-1);
      continue;
    }
    const nv=stepU8(t,s,'nvoices');
    if(nv<4){
      M._ds_voice_add_wasm(t,s,note,100,4);
      editVoice=stepU8(t,s,'nvoices')-1;           // new voice becomes active
    } else {                                       // rev.11: full -> replace the HIGHEST note (v4 after sort)
      const vs=sortedVoices(t,s);
      M._ds_voice_remove_wasm(t,s,vs[3].note);
      M._ds_voice_add_wasm(t,s,note,100,4);
      editVoice=3;
    }
  }
  for(const k of (released||[])) liveNote(k,false);
}

/* ---------- main loop ---------- */
const flash=new Float32Array(TRACKS);
function frame(t){
  if(!lastT)lastT=t;
  let dt=(t-lastT)/1000; lastT=t;
  if(dt>0.25)dt=0.25;
  try{
    M._ds_tick_wasm(dt); readMidiOut();          // tick always: live note-offs must drain even when stopped
    if(mode==='home')drawHome(); else drawGrid();      // rev.12: edit = embedded panel
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
      case 'EDIT':                                     // rev.12: focus layer, not page
        if(mode==='grid'||mode==='edit'){ mode='grid'; focusLayer=(focusLayer==='edit')?'grid':'edit'; }
        else mode='grid';
        break;
      case 'ARP': mode='home'; break;               // v1: no Euclid page yet
      case 'GLO': gloHeld=true; break;               // modifier: rest of this down-mask = shifted
      case 'OCT-':                                     // rev.11 OCT cluster: GLO+OCT- = back 1 (play/edit axis)
        if(mode==='grid'){ if(gloHeld||focusLayer==='edit') cursor=Math.max(0,cursor-1); else octShift(-1); }
        else octShift(-1);                              // HOME/PLAY: octave shift
        break;
      case 'OCT+':                                     // GLO+OCT+ = rest/tie/restore cycle (grid+edit layer)
        if(mode==='grid'){
          if(gloHeld) restTieCycle();
          else if(focusLayer==='edit') cursor=Math.min(63,cursor+1);
          else octShift(+1);
        } else octShift(+1);
        break;
      case 'REC': M._ds_toggle_step_wasm(selTrack,cursor); break;  // v1: REC=cursor toggle
      case 'HOME': mode = mode==='home'?'grid':'home'; page=0; break;
      case 'PLAY': break;                              // handled on release edge (toggle)
      case 'SELECT': selectTrack((selTrack+15)%16); break;
      default: break;                               // SAVE/FX/SCL/ENV/LFO: v1 unsignaled
    }
  }
  for(const bit of upBits){
    if(bitName(bit)==='GLO') gloHeld=false;          // long-press window closes on release
    else if(bitName(bit)==='PLAY'){                       // toggle on release edge (one physical PLAY/STOP key)
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
  const editFocus=(focusLayer==='edit'&&mode==='grid');     // rev.12: EDIT layer focus
  if(editFocus){                                            // ---- EDIT layer (03 §4) ----
    const t=selTrack, s=cursor;
    const vBase=trkOff(t)+TK.steps+s*ST.size+ST.voices;
    const nv=stepU8(t,s,'nvoices');
    if(role===0){                                           // SELECT: cycle active voice
      editVoice=Math.max(0,Math.min(3,editVoice+(n>0?1:-1))); return;
    }
    const av=Math.min(editVoice,Math.max(0,nv-1));
    if(nv===0) return;                                      // empty step: nothing to edit yet
    if(role===3){                                           // K1 PITCH (GLO held = COND)
      if(gloHeld){
        const sBase=trkOff(t)+TK.steps+s*ST.size;
        let c=(H8[S+sBase+ST.cond]+(n>0?1:-1)+11)%11;       // 11-state ring (enum, no bounds issue)
        M._ds_cond_set_wasm(t,s,c); condZoom=70;            // zoom feedback ~1s
      } else {
        const p=H8[S+vBase+av*4];
        M._ds_note_set_wasm(t,s,av,Math.max(0,Math.min(127,p+n)),-1,-1);
      }
      return;
    }
    if(role===4){                                           // K2 GATE: +1 detent = +0.25 step (gate raw = ×4)
      const g=H8[S+vBase+av*4+2];
      let ng;
      if(g===0xFF) ng = n>0 ? 1 : 0xFF;                     // ∞ → right detent returns to 0.25
      else { ng=g+n; if(ng>32){ng=0xFF;} if(ng<1){ng=1;} }  // 8.00 → right = ∞ (pending #6)
      M._ds_note_set_wasm(t,s,av,-1,-1,ng); return;
    }
    if(role===5){                                           // K3 VEL
      const v=H8[S+vBase+av*4+1];
      M._ds_note_set_wasm(t,s,av,-1,Math.max(1,Math.min(127,v+n)),-1); return;
    }
    if(role===6){                                           // K4 PROB (GLO held = rest/tie 3-state, rev.7)
      if(gloHeld){ restTieCycle(); return; }
      const p=stepU8(t,s,'prob');
      H8[S+stepOff(t,s)+ST.prob]=Math.max(0,Math.min(100,p+n*5)); return;
    }
    return;
  }
  if(role===0){                                              // SELECT: track, NO wrap
    const next=Math.max(0,Math.min(15,selTrack+(n>0?1:-1)));  // endpoints dead-stop; (future:
    if(next!==selTrack) selectTrack(next); return;            //  accel flick → jump TRK01/16)
  }
  if(role===1){ M._ds_set_bpm_wasm(Math.max(40,Math.min(240,f64(OFF.bpm)+n))); return; } // ALGORITHM: BPM (v1)
  if(role===2){ return; }                                                     // PRESETS: unsignaled
  const t=selTrack;
  if(role===3){                                              // K1: GRID=cursor axis; GLO+=START (rev.11)
    if(mode==='grid'){ if(gloHeld){ M._ds_set_loop_wasm(t,Math.max(0,Math.min(63,trkU8(t,'loop_start')+n)),-1); } else cursorMove(n); return; }
    M._ds_set_loop_wasm(t,Math.max(0,Math.min(63,trkU8(t,'loop_start')+n)),-1); return;
  }
  if(role===4){                                              // K2: LEN (rev.11: GLO+K2 x16 retired)
    M._ds_set_loop_wasm(t,-1,Math.max(1,Math.min(64,trkU8(t,'loop_len')+n))); return;
  }
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
      prob:M._ds_step_off_prob(), voices:M._ds_step_off_voices(), cond:M._ds_step_off_cond()};

  DSPanel.build();
  DSPanel.onButton(panelButton);
  DSPanel.onKey((pressed,down,released)=>{       // drum grid edits go through the drum handler;
    if(mode!=='grid'){                            // PLAY page: keys = live audition only
      for(const k of (pressed||[])) liveNote(k,true);
      for(const k of (released||[])) liveNote(k,false);
      return;
    }
    if(focusLayer==='edit'){                      // rev.14a: EDIT layer — keys edit voices, cursor stays
      editKeyHandler(pressed,down,released); return;
    }
    if(trkU8(selTrack,'channel')===9) drumKeyHandler(pressed,down,released);
    else stepEntry(pressed,down,released);        // melodic grid layer: override step-entry (rev.11)
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
