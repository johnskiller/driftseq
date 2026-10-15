/* panel.js — the FM-1 face as SVG, driving the DriftSeq engine.
 * Page structure and panel drawing after Melodee's browser simulator (John's fork, GPL-3.0),
 * after Felucca's web/emu/index.html (Hugelton Instruments, GPL-3.0), after X0X.
 * Exposes: DSPanel.build(), DSPanel.onButton(bit,down), DSPanel.onKey(k,down), DSPanel.onEnc(role,n),
 * DSPanel.setLeds({litB,dimB,litK,dimK}), DSPanel.screen() -> the 240x240 canvas.
 * Panel semantics (panel.c): 14 buttons (2x6 + OCT-/OCT+), 7 encoders + MASTER, keys F3..G5 (27).
 */
'use strict';
const DSPanel = (() => {
  const NS = "http://www.w3.org/2000/svg";
  const B = { FX:0, SCL:1, ENV:2, LFO:3, EDIT:4, GLO:5, HOME:6, SAVE:7, ARP:8, SEQ:9, PLAY:10, REC:11, "OCT-":12, "OCT+":13 };
  const EN = { SELECT:0, ALGORITHM:1, PRESETS:2, "KNOB 1":3, "KNOB 2":4, "KNOB 3":5, "KNOB 4":6 };
  const WHITE = [0,2,4,6,7,9,11,12,14,16,18,19,21,23,24,26];
  const BLACK = [1,3,5,8,10,13,15,17,20,22,25];
  const BLACK_AFTER = [0,1,2,4,5,7,8,9,11,12,14];
  const KEYNAMES = ["F","F♯","G","G♯","A","A♯","B","C","C♯","D","D♯","E"];
  const keyName = (k) => KEYNAMES[k%12] + (3 + Math.floor((k+5)/12));
  const ROW1 = ["FX","SCL","ENV","LFO","EDIT","GLO"], ROW2 = ["HOME","SAVE","ARP","SEQ","PLAY","REC"];
  const KNOBS = ["MASTER","SELECT","PRESETS","ALGORITHM","KNOB 1","KNOB 2","KNOB 3","KNOB 4"];
  // one layout: the FM-1's own wide arrangement
  const L = {
    w:1000, h:624, screen:[292,44,256],
    knobs:[[105,100,24],[215,100,24],[105,205,24],[215,205,24],[610,100,22],[714,100,22],[818,100,22],[922,100,22]],
    trays:[[50,250,220,50],[568,160,400,140]],
    oct:[[60,258,90,34],[170,258,90,34]],
    grid:[582,174,54,48,63,62], keybed:[36,332,928,256],
  };
  const svg = document.getElementById("panel"), device = document.getElementById("device"),
        canvas = document.getElementById("screen");
  function el(tag, attrs, parent = svg) {
    const e = document.createElementNS(NS, tag);
    for (const [k,v] of Object.entries(attrs)) e.setAttribute(k,v);
    parent.appendChild(e); return e;
  }
  const SANS = "-apple-system,'Helvetica Neue',Arial,sans-serif";
  function label(parent,x,y,s,size,ink="var(--label)") {
    const t = el("text",{x,y,"text-anchor":"middle","font-family":SANS,"font-size":size,
                         "font-weight":600,fill:ink,"letter-spacing":".04em"},parent);
    t.textContent = s; return t;
  }
  const knobs=[], buttons=[], keys=[];
  const btnBy=new Map(), keyBy=new Map();
  let btnMask=0, keyMask=0, latched=0;
  const angle = Object.fromEntries(KNOBS.map(n=>[n,0]));
  const handlers = { button:null, key:null, enc:null };

  function build() {
    svg.innerHTML="";
    svg.setAttribute("viewBox",`0 0 ${L.w} ${L.h}`);
    device.style.aspectRatio = `${L.w} / ${L.h}`;
    for (const d of device.querySelectorAll(".hit")) d.remove();
    knobs.length = buttons.length = keys.length = 0;
    el("rect",{x:4,y:4,width:L.w-8,height:L.h-8,rx:40,fill:"var(--body)"});
    for (const [x,y,w,h] of L.trays) el("rect",{x,y,width:w,height:h,rx:14,fill:"var(--tray)"});
    const [sx,sy,ss] = L.screen;
    el("rect",{x:sx,y:sy,width:ss,height:ss,rx:18,fill:"#0b0b0c"});
    const inset = ss/32;
    Object.assign(canvas.style,{ left:`${(sx+inset)/L.w*100}%`, top:`${(sy+inset)/L.h*100}%`,
                                 width:`${(ss-2*inset)/L.w*100}%`, height:`${(ss-2*inset)/L.h*100}%` });
    KNOBS.forEach((n,i)=>{
      const [x,y,r]=L.knobs[i], g=el("g",{});
      el("circle",{cx:x,cy:y,r:r+5,fill:"var(--tray)"},g);
      el("circle",{cx:x,cy:y,r,fill:"var(--part)",stroke:"var(--edge)","stroke-width":2},g);
      const ind = n==="MASTER" ? el("line",{x1:x,y1:y-r+5,x2:x,y2:y-r+14,stroke:"var(--label)","stroke-width":3,"stroke-linecap":"round"},g) : null;
      label(g,x,y-r-12,n,16);
      el("circle",{cx:x,cy:y,r:r+12,fill:"transparent"},g);
      knobs.push({g,ind,x,y,name:n,role:n==="MASTER"?"master":EN[n]});
    });
    const button=([x,y,w,h],n,sub)=>{
      const g=el("g",{});
      el("rect",{x,y,width:w,height:h,rx:8,fill:"var(--part)",stroke:"var(--edge)","stroke-width":1.5,class:"face btn"},g);
      const ink = n==="REC" ? "#ff453a" : undefined;
      if (sub) { label(g,x+w/2,y+h/2-2,n,10.8,ink); label(g,x+w/2,y+h/2+11,sub,9.9); }
      else label(g,x+w/2,y+h/2+4.5,n,12.6,ink);
      buttons.push({g,bit:B[n],name:sub?`${n} / ${sub}`:n});
    };
    button(L.oct[0],"OCT-");
    button(L.oct[1],"OCT+");
    const [gx,gy,gw,gh,dx,dy]=L.grid;
    for (let i=0;i<6;i++){
      button([gx+i*dx,gy,gw,gh],ROW1[i]);
      button([gx+i*dx,gy+dy,gw,gh],ROW2[i],ROW2[i]==="PLAY"?"STOP":"");
    }
    const [x0,y0,w,h]=L.keybed;
    el("rect",{x:x0,y:y0,width:w,height:h,rx:18,fill:"var(--tray)"});
    const pitch=(w-24)/16, kw=pitch*0.78, kh=h*0.425, sw=Math.max(3,kw*0.14);
    const key=(cx,cy,k)=>{
      const g=el("g",{}), y=cy-kh/2;
      el("rect",{x:cx-kw/2,y,width:kw,height:kh,rx:kw/2,class:"face key"},g);
      el("rect",{x:cx-sw/2,y:y+kw*0.4,width:sw,height:kh-kw*0.8,rx:sw/2,class:"slit"},g);
      el("rect",{x:cx-pitch/2,y:cy-h*0.24,width:pitch,height:h*0.48,fill:"transparent"},g);
      keys[k]={g,name:keyName(k)};
    };
    WHITE.forEach((k,i)=>key(x0+12+(i+0.5)*pitch, y0+h*0.75, k));
    BLACK_AFTER.forEach((a,j)=>key(x0+12+(a+1)*pitch, y0+h*0.25, BLACK[j]));
    const hit=(c,cls,role)=>{
      const b=c.g.getBBox(), d=document.createElement("div");
      d.className="hit"+(cls?" "+cls:"");
      Object.assign(d.style,{left:`${b.x/L.w*100}%`,top:`${b.y/L.h*100}%`,
                             width:`${b.width/L.w*100}%`,height:`${b.height/L.h*100}%`});
      d.setAttribute("role",role); d.setAttribute("aria-label",c.name); d.title=c.name; d.tabIndex=0;
      d.addEventListener("touchstart",(e)=>e.preventDefault(),{passive:false});
      d.addEventListener("contextmenu",(e)=>e.preventDefault());
      device.insertBefore(d, document.getElementById("start"));
      c.hit=d;
    };
    knobs.forEach((k)=>{ hit(k,"knob","slider"); bindKnob(k); drawKnob(k); });
    buttons.forEach((b)=>{ hit(b,"","button"); bindButton(b); });
    keys.forEach((k,i)=>{ hit(k,"","button"); bindKey(k,i); });
    syncButtons(true); syncKeys(true); drawLeds(lastLeds);
  }

  function syncButtons(redraw){
    let m=latched;
    for (const b of btnBy.values()) m |= 1<<b;
    if (m!==btnMask){
      const released = btnMask & ~m;
      btnMask=m;
      if (handlers.button) handlers.button({ mask:m, bits:[...btnBy.values()], down:true, released:[...bitsOf(released)] });
    }
    else if (!redraw) return;
    for (const b of buttons){
      const on=!!(m&(1<<b.bit));
      b.g.classList.toggle("held",on);
      b.g.classList.toggle("latched",!!(latched&(1<<b.bit)));
      b.hit.setAttribute("aria-pressed",on);
    }
  }
  function* bitsOf(m){ for(let i=0;i<14;i++) if(m&(1<<i)) yield i; }
  function syncKeys(redraw){
    let m=0;
    for (const k of keyBy.values()) m |= 1<<k;
    if (m!==keyMask){
      const pressed=[...keyBy.values()];
      const releasedBits = keyMask & ~m;
      keyMask=m;
      if (handlers.key) handlers.key(pressed, true, [...bitsOf(releasedBits)]);
    }
    else if (!redraw) return;
    keys.forEach((k,i)=>k.g.classList.toggle("held",!!(m&(1<<i))));
  }
  // compress a button bitmap change into per-bit down events the engine understands
  function maskEvents(by,m){
    const ev={ mask:m, bits:[...by.values()], down:true };
    return ev;
  }
  function bindButton(b){
    b.hit.addEventListener("pointerdown",(e)=>{
      e.preventDefault();
      if (e.shiftKey||e.button===2){ latched^=1<<b.bit; syncButtons(true); return; }
      if (latched&(1<<b.bit)){ latched&=~(1<<b.bit); syncButtons(true); return; }
      try{ b.hit.setPointerCapture(e.pointerId); }catch(err){}
      btnBy.set("p"+e.pointerId,b.bit);
      syncButtons();
    });
    const up=(e)=>{ if (btnBy.delete("p"+e.pointerId)) syncButtons(); };
    b.hit.addEventListener("pointerup",up);
    b.hit.addEventListener("pointercancel",up);
    b.hit.addEventListener("keydown",(e)=>{
      if ((e.key==="Enter"||e.key===" ")&&!e.repeat){ btnBy.set("f"+b.bit,b.bit); syncButtons(); e.preventDefault(); e.stopPropagation(); }
    });
    b.hit.addEventListener("keyup",()=>{ if (btnBy.delete("f"+b.bit)) syncButtons(); });
  }
  function bindKey(k,i){
    k.hit.addEventListener("pointerdown",(e)=>{
      e.preventDefault();
      k.hit.setPointerCapture(e.pointerId);
      keyBy.set("p"+e.pointerId,i);
      syncKeys();
    });
    const up=(e)=>{ if (keyBy.delete("p"+e.pointerId)) syncKeys(); };
    k.hit.addEventListener("pointerup",up);
    k.hit.addEventListener("pointercancel",up);
    k.hit.addEventListener("keydown",(e)=>{
      if ((e.key==="Enter"||e.key===" ")&&!e.repeat){ keyBy.set("f"+i,i); syncKeys(); e.preventDefault(); e.stopPropagation(); }
    });
    k.hit.addEventListener("keyup",()=>{ if (keyBy.delete("f"+i)) syncKeys(); });
  }
  function drawKnob(k){ if (k.ind) k.ind.setAttribute("transform",`rotate(${angle[k.name]} ${k.x} ${k.y})`); }
  function turn(k,n){
    if (!n) return;
    angle[k.name]=(angle[k.name]+n*18)%360;
    drawKnob(k);
    if (handlers.enc) handlers.enc(k.role,n);
  }
  function bindKnob(k){
    let drag=null, wheel=0;
    k.hit.addEventListener("pointerdown",(e)=>{ e.preventDefault(); k.hit.setPointerCapture(e.pointerId); drag={x:e.clientX,y:e.clientY,acc:0}; });
    k.hit.addEventListener("pointermove",(e)=>{
      if (!drag) return;
      const dx=e.clientX-drag.x, dy=drag.y-e.clientY;
      drag.acc += Math.abs(dx)>Math.abs(dy)?dx:dy;
      drag.x=e.clientX; drag.y=e.clientY;
      const step=k.role==="master"?3:8, n=Math.trunc(drag.acc/step);
      if (n){ drag.acc-=n*step; turn(k,n); }
    });
    const end=()=>{ drag=null; };
    k.hit.addEventListener("pointerup",end);
    k.hit.addEventListener("pointercancel",end);
    k.hit.addEventListener("wheel",(e)=>{
      e.preventDefault();
      const dy=e.deltaMode===1?e.deltaY*33:e.deltaY;
      if (Math.abs(dy)>=50){ turn(k,dy<0?1:-1); wheel=0; return; }
      wheel-=dy;
      const n=Math.trunc(wheel/25);
      if (n){ wheel-=n*25; turn(k,n); }
    },{passive:false});
    k.hit.addEventListener("keydown",(e)=>{
      const d={ArrowUp:1,ArrowRight:1,ArrowDown:-1,ArrowLeft:-1}[e.key];
      if (d){ turn(k,d); e.preventDefault(); e.stopPropagation(); }
    });
  }

  // LED grammar: dark nothing, dim something, bright now (ui_input.c's, simplified for the emu)
  let lastLeds={litB:0,dimB:0,litK:0,dimK:0};
  function drawLeds(l){
    lastLeds=l||lastLeds;
    for (const b of buttons){
      const bit=1<<b.bit;
      b.g.classList.toggle("lit",!!(lastLeds.litB&bit));
      b.g.classList.toggle("dim",!!(lastLeds.dimB&bit)&&!(lastLeds.litB&bit));
    }
    keys.forEach((k,i)=>{
      k.g.classList.toggle("lit",!!(lastLeds.litK&(1<<i)));
      k.g.classList.toggle("dim",!!(lastLeds.dimK&(1<<i))&&!(lastLeds.litK&(1<<i)));
    });
  }

  // computer keyboard: F3..A#4 on Z/A rows, B4..G5 on Q..O, 1..= the 12 buttons, [ ] OCT
  const KEYMAP={};
  ["KeyZ","KeyX","KeyC","KeyV","KeyB","KeyN","KeyM","Comma","Period","Slash"].forEach((c,i)=>KEYMAP[c]={key:WHITE[i]});
  [["KeyS",1],["KeyD",3],["KeyF",5],["KeyH",8],["KeyJ",10],["KeyL",13],["Semicolon",15],["Quote",17]].forEach(([c,k])=>KEYMAP[c]={key:k});
  ["KeyQ","KeyW","KeyE","KeyR","KeyT","KeyY","KeyU","KeyI","KeyO"].forEach((c,i)=>KEYMAP[c]={key:18+i});
  ["Digit1","Digit2","Digit3","Digit4","Digit5","Digit6","Digit7","Digit8","Digit9","Digit0","Minus","Equal"]
    .forEach((c,i)=>KEYMAP[c]={btn:B[[...ROW1,...ROW2][i]]});
  Object.assign(KEYMAP,{BracketLeft:{btn:B["OCT-"]},BracketRight:{btn:B["OCT+"]}});
  addEventListener("keydown",(e)=>{
    if (e.metaKey||e.ctrlKey||e.altKey||e.target.closest?.(".hit.knob")) return;
    const m=KEYMAP[e.code];
    if (!m) return;
    e.preventDefault();
    if (e.repeat) return;
    if (m.key!==undefined){ keyBy.set("k"+e.code,m.key); syncKeys(); }
    else { btnBy.set("k"+e.code,m.btn); syncButtons(); }
  });
  addEventListener("keyup",(e)=>{
    const id="k"+e.code;
    if (keyBy.delete(id)) syncKeys();
    if (btnBy.delete(id)) syncButtons();
  });
  addEventListener("blur",()=>{
    for (const id of [...btnBy.keys()]) if (id[0]!=="p") btnBy.delete(id);
    for (const id of [...keyBy.keys()]) if (id[0]!=="p") keyBy.delete(id);
    syncButtons(); syncKeys();
  });

  return {
    build, drawLeds,
    onButton:(f)=>handlers.button=f,
    onKey:(f)=>handlers.key=f,
    onEnc:(f)=>handlers.enc=f,
    screen:()=>canvas,
    B, EN,
    knobName:(role)=>KNOBS[role+1],
  };
})();
