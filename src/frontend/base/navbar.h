#pragma once
#include <Arduino.h>

static const char NAVBAR_CSS[] PROGMEM = R"=====(.site-nav { display:flex; align-items:center; gap:1rem; padding:.6rem 1rem; background:#161b22;
            border-bottom:1px solid #30363d; flex-wrap:wrap; view-transition-name:site-nav;
            position:sticky; top:0; z-index:30; }
.site-nav .brand { display:flex; align-items:center; gap:.75rem; text-decoration:none; }
.site-nav .brand img { height:64px; border-radius:14px; }
.site-nav .brand-text .name { font-size:1.4rem; font-weight:700; color:#fff; line-height:1; }
.site-nav .brand-text .sub  { font-size:.68rem; color:#8b949e; margin-top:2px; }
.site-nav .nav-stats { margin-left:auto; display:flex; gap:.85rem; align-items:center; flex-wrap:wrap; }
.site-nav .nav-stat  { display:flex; flex-direction:column; line-height:1.05; align-items:flex-start; }
.site-nav .nav-stat .ns-label { font-size:.55rem; text-transform:uppercase; letter-spacing:.07em; color:#8b949e; white-space:nowrap; }
.site-nav .nav-stat .ns-val   { font-size:.95rem; font-weight:700; color:#fff; font-variant-numeric:tabular-nums; white-space:nowrap; overflow:hidden; }
.site-nav .nav-links { display:flex; gap:.4rem; }
)=====";

static const char NAVBAR_HTML[] PROGMEM = R"=====(<nav class="site-nav">
  <a class="brand" href="/">
    <img src="/logo.webp?v=__FWVER__" alt="LuxDMX">
    <div class="brand-text"><div class="name">LuxDMX</div><div class="sub" id="nav-sub">&nbsp;</div></div>
  </a>
  <div class="nav-stats">
    <div class="nav-stat" style="width:5.5rem"><span class="ns-label">DMX FPS</span><span class="ns-val" id="fps">&middot;</span></div>
    <div class="nav-stat" style="width:5.5rem"><span class="ns-label">In FPS</span><span class="ns-val" id="infps">&middot;</span></div>
    <div class="nav-stat" style="width:4.5rem"><span class="ns-label">Style</span><span class="ns-val" id="txstyle">&middot;</span></div>
    <div class="nav-stat" style="width:4rem"><span class="ns-label" id="net-label">WiFi</span><span class="ns-val" id="rssi">&middot;</span></div>
    <div class="nav-stat" style="width:3.8rem"><span class="ns-label">Heap</span><span class="ns-val" id="heap">&middot;</span></div>
    <div class="nav-stat" style="width:5rem"><span class="ns-label">Uptime</span><span class="ns-val" id="uptime">&middot;</span></div>
    <div class="nav-stat" style="width:3rem"><span class="ns-label">Jitter</span><span class="ns-val" id="jitter">&middot;</span></div>
    <div class="nav-stat" style="width:2.6rem"><span class="ns-label">Fixtures</span><span class="ns-val" id="nfix">&middot;</span></div>
    <div class="nav-stat" style="width:4rem"><span class="ns-label">RDM tx</span><span class="ns-val" id="rdm-tx">&middot;</span></div>
    <div class="nav-stat" style="width:4rem"><span class="ns-label">RDM rx</span><span class="ns-val" id="rdm-rx">&middot;</span></div>
  </div>
  <div class="nav-links">
    <a href="/" data-tab="/" class="btn btn-outline-secondary btn-sm">Status</a>
    <a href="/rdm" data-tab="/rdm" class="btn btn-outline-secondary btn-sm">RDM</a>
    <a href="/config" data-tab="/config" class="btn btn-outline-secondary btn-sm">Settings</a>
  </div>
</nav>
)=====";

static const char NAVBAR_JS[] PROGMEM = R"=====(<script>
window.LuxNav = (function(){
  var NAVIDS=['fps','infps','txstyle','rssi','net-label','heap','uptime','jitter','nav-sub','nfix','rdm-tx','rdm-rx'];
  var NAV_TAIL=10;
  var PER_OUT=5, CHANS_PER_OUT=512;
  function fmtK(n){ return n>=1e6 ? (n/1e6).toFixed(1)+'M' : n>=1e4 ? (n/1e3).toFixed(0)+'k' : String(n); }
  function saveNav(){ try{ var o={}; NAVIDS.forEach(function(id){ var e=document.getElementById(id); if(e) o[id]=[e.innerHTML,e.style.color||'']; }); sessionStorage.setItem('luxnav',JSON.stringify(o)); }catch(_){} }
  function paintNav(){ try{ var o=JSON.parse(sessionStorage.getItem('luxnav')||'{}'); NAVIDS.forEach(function(id){ var e=document.getElementById(id); if(e&&o[id]){ e.innerHTML=o[id][0]; if(o[id][1]) e.style.color=o[id][1]; } }); }catch(_){} }
  function stats(buf){
    if(!(buf instanceof ArrayBuffer) || buf.byteLength<16) return;
    var v=new DataView(buf);
    var nOut=Math.max(0,Math.floor((buf.byteLength-16-NAV_TAIL-1)/(CHANS_PER_OUT+PER_OUT)));
    var statsOff=16+nOut*CHANS_PER_OUT, out=[], inp=[], sty=[];
    for(var oi=0;oi<nOut;oi++){
      out.push((v.getUint16(statsOff+2*oi)/10).toFixed(1));
      inp.push((v.getUint16(statsOff+2*nOut+2*oi)/10).toFixed(1));
      sty.push(v.getUint8(statsOff+4*nOut+oi));
    }
    document.getElementById('fps').textContent   = out.length ? out.join(' · ') : (v.getUint16(0)/10).toFixed(1);
    document.getElementById('infps').textContent = inp.length ? inp.join(' · ') : '0.0';
    var se=document.getElementById('txstyle');
    if(se){
      if(sty.length){
        se.textContent = sty.map(function(b){ return (b&1?'D':'C') + (b&2?'·':''); }).join(' ');
        se.title = sty.map(function(b,i){
          return 'Output ' + String.fromCharCode(65+i) + ': ' + (b&1?'delta (follows the input)':'continuous (free-run)')
               + (b&2?', set over Art-Net':', set here'); }).join(String.fromCharCode(10));
        se.style.color = sty.some(function(b){ return b&2; }) ? 'var(--lux-cyan)' : '';
      } else { se.textContent='·'; se.title=''; }
    }
    var rssi=v.getInt16(2), heap=v.getUint32(4), upS=v.getUint32(8);
    var re=document.getElementById('rssi'), nl=document.getElementById('net-label');
    if(rssi>=10){ nl.textContent='LAN'; re.textContent=rssi+'M'; re.style.color='#45d85c'; }
    else if(rssi>=1){ nl.textContent='AP'; re.textContent='active'; re.style.color='#f33abc'; }
    else { nl.textContent='WiFi'; re.textContent=rssi+' dBm'; re.style.color=rssi>-65?'#45d85c':rssi>-80?'#ffaa1c':'#f33abc'; }
    document.getElementById('heap').textContent=(heap/1024).toFixed(0)+' KB';
    var h=Math.floor(upS/3600), m=Math.floor((upS%3600)/60), s=upS%60;
    document.getElementById('uptime').textContent=(h?h+'h ':'')+(m?m+'m ':'')+s+'s';
    document.getElementById('jitter').textContent=(v.getUint16(14)/10).toFixed(1)+' ms';
    var t=buf.byteLength-NAV_TAIL;
    document.getElementById('nfix').textContent=v.getUint16(t);
    document.getElementById('rdm-tx').textContent=fmtK(v.getUint32(t+2));
    document.getElementById('rdm-rx').textContent=fmtK(v.getUint32(t+6));
    saveNav();
  }
  var path=(location.pathname.replace(/\/+$/,'')||'/');
  var links=document.querySelectorAll('.site-nav .nav-links a');
  for(var i=0;i<links.length;i++){ var on=links[i].getAttribute('data-tab')===path;
    links[i].classList.toggle('btn-primary',on); links[i].classList.toggle('btn-outline-secondary',!on); }
  paintNav();
  fetch('/info.json').then(function(r){return r.json();}).then(function(d){
    document.getElementById('nav-sub').innerHTML=(d.hostname||'')+'.local &nbsp;&middot;&nbsp; '+(d.ip||'')+' &nbsp;&middot;&nbsp; v'+(d.version||'');
    saveNav();
  }).catch(function(){});
  return { stats:stats };
})();
</script>
)=====";
