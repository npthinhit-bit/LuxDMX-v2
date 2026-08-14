#pragma once
#include <Arduino.h>

static const char RDM_PAGE_JS[] PROGMEM = R"=====(

var sock=null, open={}, timer=null, lastD=null, lastSig='', HIST={}, busySince=0, remembered={};
var sortCol='', sortDir=1;   // fixtures-table sort column + direction (1 asc, -1 desc)
function esc(s){ return String(s==null?'':s).replace(/[&<>"]/g,function(c){return {'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]; }); }
function fid(uid){ return uid.replace(/[^0-9A-Fa-f]/g,''); }
function send(o){ if(sock&&sock.readyState===1) sock.send(JSON.stringify(o)); }

// The /ws status frame drives the shared navbar; this socket is also the page's control channel.
function connect(){
  sock=new WebSocket('ws://'+location.host+'/ws');
  sock.binaryType='arraybuffer';
  sock.onclose=function(){ setTimeout(connect,2000); };
  sock.onmessage=function(e){ if(window.LuxNav) LuxNav.stats(e.data); };
}
connect();

// ── E1.20 labels ────────────────────────────────────────────────────────────
var CAT={0:'',0x0100:'Fixture',0x0101:'Fixture Fixed',0x0102:'Fixture Moving Yoke',0x0103:'Fixture Moving Mirror',
  0x0200:'Fixture Accessory',0x0300:'Projector',0x0400:'Atmospheric',0x0500:'Dimmer',0x0600:'Power',
  0x0700:'Scenic',0x0800:'Data',0x0900:'AV',0x0a00:'Monitor',0x7000:'Control',0x7100:'Test',0x7fff:'Other'};
function catName(c){ if(!c) return ''; return CAT[c]||('0x'+c.toString(16)); }
var SENSTYPE={0:'Temperature',1:'Voltage',2:'Current',3:'Frequency',4:'Resistance',5:'Power',6:'Mass',
  7:'Length',8:'Area',9:'Volume',10:'Density',11:'Velocity',12:'Acceleration',13:'Force',14:'Energy',
  15:'Pressure',16:'Time',17:'Angle',18:'Position X',19:'Position Y',20:'Position Z',21:'Speed',
  22:'Luminous intensity',23:'Luminous flux',24:'Illuminance',25:'Chrominance red',26:'Chrominance green',
  27:'Chrominance blue',28:'Contacts',29:'Memory',30:'Items',31:'Humidity',127:'Other'};
function typeName(t){ return SENSTYPE[t]||('Type '+t); }
var PAL=['#58a6ff','#45d85c','#f0883e','#bc8cff','#f778ba','#ffd33d','#39c5cf','#ff7b72','#7ee787','#a5d6ff','#ffab70','#d2a8ff'];

// ── sensor history (timestamped) + charts grouped by sensor type ────────────
function pushHist(key,v){
  var h=HIST[key]||(HIST[key]=[]), t=Date.now();
  h.push({t:t,v:v});
  while(h.length>1 && (h.length>240 || t-h[0].t>180000)) h.shift();   // ~3 min rolling window
}
// Keep the chart history in the browser so it survives a reload or a tab switch instead of
// starting from a blank graph every time. Timestamps are absolute, so stale points (outside the
// ~3 min window) are dropped on load and save; nothing unbounded is ever stored.
function loadHist(){
  try{
    var raw=localStorage.getItem('rdmhist'); if(!raw) return;
    var h=JSON.parse(raw), now=Date.now();
    for(var k in h){ var a=(h[k]||[]).filter(function(p){ return now-p.t<180000; }); if(a.length) HIST[k]=a; }
  }catch(_){}
}
function saveHist(){
  try{
    var now=Date.now(), out={};
    for(var k in HIST){ var a=HIST[k].filter(function(p){ return now-p.t<180000; }); if(a.length) out[k]=a; }
    localStorage.setItem('rdmhist', JSON.stringify(out));
  }catch(_){}
}
loadHist();
window.addEventListener('pagehide', saveHist);
setInterval(saveHist, 5000);
function sensorGroups(d){
  var groups={};
  (d.devices||[]).forEach(function(f){
    var nm=f.label||f.modelName||('Model '+f.model);
    (f.sensors||[]).forEach(function(s,i){
      if(!s.poll) return;                       // only enabled sensors are polled + graphed
      var key=fid(f.uid)+'_'+i; pushHist(key,s.value);
      var g=groups[s.type]||(groups[s.type]={type:s.type,unit:'',series:[]});
      if(!g.unit && s.unit) g.unit=s.unit;
      var multi=f.sensors.filter(function(x){return x.type===s.type;}).length>1;
      g.series.push({key:key,name:nm+(multi?' · '+s.name:''),color:PAL[g.series.length%PAL.length]});
    });
  });
  return groups;
}
function multiChart(g){
  var W=320,H=96,ml=32,mr=8,mt=8,mb=15,pw=W-ml-mr,ph=H-mt-mb;
  var ser=g.series.map(function(s){ return {name:s.name,color:s.color,h:HIST[s.key]||[]}; }).filter(function(s){ return s.h.length; });
  if(!ser.length) return '';
  var tmin=Infinity,tmax=-Infinity,vmin=Infinity,vmax=-Infinity;
  ser.forEach(function(s){ s.h.forEach(function(d){ if(d.t<tmin)tmin=d.t; if(d.t>tmax)tmax=d.t; if(d.v<vmin)vmin=d.v; if(d.v>vmax)vmax=d.v; }); });
  var tspan=Math.max(1000,tmax-tmin);
  if(vmax===vmin){ vmax+=1; vmin-=1; }
  var pad=(vmax-vmin)*0.12; vmin-=pad; vmax+=pad;
  var X=function(t){ return ml+(t-tmin)/tspan*pw; }, Y=function(v){ return mt+(1-(v-vmin)/(vmax-vmin))*ph; };
  var fmt=function(v){ var a=Math.abs(v); return a>=100?v.toFixed(0):a>=10?v.toFixed(1):v.toFixed(2); };
  var ax='';
  [vmax,(vmax+vmin)/2,vmin].forEach(function(vv){ var y=Y(vv);
    ax+='<line x1="'+ml+'" y1="'+y.toFixed(1)+'" x2="'+(W-mr)+'" y2="'+y.toFixed(1)+'" class="c-grid"/>'+
        '<text x="'+(ml-3)+'" y="'+(y+2.6).toFixed(1)+'" class="c-ax" text-anchor="end">'+fmt(vv)+'</text>'; });
  var secs=Math.round(tspan/1000);
  [[ml,'-'+secs+'s','start'],[ml+pw/2,'-'+Math.round(secs/2)+'s','middle'],[W-mr,'now','end']].forEach(function(p){
    ax+='<line x1="'+p[0]+'" y1="'+mt+'" x2="'+p[0]+'" y2="'+(mt+ph)+'" class="c-grid"/>'+
        '<text x="'+p[0]+'" y="'+(H-4)+'" class="c-ax" text-anchor="'+p[2]+'">'+p[1]+'</text>'; });
  var lines=ser.map(function(s){
    var pts=s.h.map(function(d){ return X(d.t).toFixed(1)+','+Y(d.v).toFixed(1); }).join(' ');
    var last=s.h[s.h.length-1];
    return '<polyline points="'+pts+'" class="c-line" style="stroke:'+s.color+'"/>'+
           '<circle cx="'+X(last.t).toFixed(1)+'" cy="'+Y(last.v).toFixed(1)+'" r="1.8" class="c-dot" style="fill:'+s.color+'"/>';
  }).join('');
  var legend='<div class="legend">'+ser.map(function(s){ return '<span class="lg"><i style="background:'+s.color+'"></i>'+esc(s.name)+'</span>'; }).join('')+'</div>';
  var unit=g.unit?' <span class="c-unit">('+esc(g.unit)+')</span>':'';
  return '<div class="s-card"><div class="s-title">'+esc(typeName(g.type))+unit+'</div>'+
    '<svg class="chart-svg" viewBox="0 0 '+W+' '+H+'">'+ax+lines+'</svg>'+legend+'</div>';
}
function renderDash(d){
  var dash=document.getElementById('sensor-dash');
  var groups=sensorGroups(d), types=Object.keys(groups).sort(function(a,b){ return a-b; });
  if(!types.length){ dash.innerHTML=''; return; }
  dash.innerHTML='<div class="dash-title">Sensors'+(d.sensorPoll?' <span class="text-secondary" style="font-weight:400">· live</span>':'')+'</div>'+
    '<div class="s-grid">'+types.map(function(t){ return multiChart(groups[t]); }).join('')+'</div>';
}

// ── fixtures table ──────────────────────────────────────────────────────────
function persCell(f){
  if(f.persCount>1){
    var o='<select class="form-select pers-sel" onclick="event.stopPropagation()" onchange="rdmSetPers(\''+f.uid+'\',this.value)">';
    for(var p=1;p<=f.persCount;p++) o+='<option value="'+p+'"'+(p==f.pers?' selected':'')+'>'+p+'/'+f.persCount+'</option>';
    return o+'</select>';
  }
  return f.pers+' / '+f.persCount;
}
function sensorCell(f){
  var list=(f.sensors||[]);
  if(!list.length) return '<span class="s-none">&mdash;</span>';
  // per-fixture master: one switch to poll/graph every sensor of this fixture at once
  var head='<div class="s-item s-all">'+
    '<div class="form-check form-switch m-0"><input class="form-check-input" type="checkbox" id="sa_'+fid(f.uid)+'"'+
      ' title="Toggle this fixture\'s sensors off, then back on" onclick="event.stopPropagation();aggClick(event,\''+f.uid+'\')"></div>'+
    '<span class="s-nm s-all-lbl">all</span></div>';
  var rows=list.map(function(s,idx){
    var key=fid(f.uid)+'_'+idx;
    return '<div class="s-item">'+
      '<div class="form-check form-switch m-0"><input class="form-check-input" type="checkbox"'+(s.poll?' checked':'')+
        ' title="Poll + graph this sensor" onclick="event.stopPropagation()" onchange="rdmSensSel(\''+f.uid+'\','+idx+',this.checked)"></div>'+
      '<span class="s-nm">'+esc(s.name)+'</span>'+
      '<span class="s-val" id="sv_'+key+'">'+s.value+(s.unit?(' '+esc(s.unit)):'')+'</span>'+
    '</div>';
  }).join('');
  return head+rows;
}
// per-fixture "all" switch state: checked when every sensor on, dashed when only some
function setFixtureAll(d){
  (d.devices||[]).forEach(function(f){
    var el=document.getElementById('sa_'+fid(f.uid)); if(!el) return;
    var ss=(f.sensors||[]).map(function(s){ return !!s.poll; });
    el.checked = ss.length>0 && ss.every(Boolean);
    el.indeterminate = ss.some(Boolean) && !ss.every(Boolean);
  });
}
// RDM status badge from the Art-Net BackgroundQueue harvest (only shown when a device reports one).
function healthBadge(f){
  if(!f.stCount) return '';
  var lbl=({2:'Advisory',3:'Warning',4:'Error'})[f.stType]||('Status '+f.stType);
  var cls=f.stType>=4?'bg-danger':(f.stType>=3?'bg-warning text-dark':'bg-info text-dark');
  return ' <span class="badge '+cls+'" title="RDM status id '+f.stId+' ('+f.stCount+' queued)">'+lbl+'</span>';
}
function fxRow(f){
  var name=f.label||f.modelName||('Model '+f.model), op=open[f.uid]?' open':'', i=fid(f.uid);
  var row='<tr class="fxrow'+op+'" id="fxr_'+i+'" onclick="toggleFx(\''+f.uid+'\')">'+
    '<td><span class="caret">&#9656;</span> <span class="fx-name">'+esc(name)+'</span>'+healthBadge(f)+'</td>'+
    '<td class="fx-uid">'+esc(f.uid)+'</td>'+
    '<td><span class="uni-badge">U'+f.uni+'</span></td>'+
    '<td>'+f.addr+'</td>'+
    '<td>'+f.footprint+'</td>'+
    '<td>'+persCell(f)+'</td>'+
    '<td>'+(esc(f.mfg)||'&middot;')+'</td>'+
    '<td>'+(esc(f.modelName)||('id '+f.model))+'</td>'+
    '<td>'+(esc(catName(f.cat))||'&middot;')+'</td>'+
    '<td class="s-cell">'+sensorCell(f)+'</td>'+
    '<td><button class="btn btn-sm py-0 px-2 '+(f.identify?'btn-warning':'btn-outline-secondary')+'" title="Identify" '+
      'onclick="event.stopPropagation();rdmIdentify(\''+f.uid+'\','+(f.identify?'false':'true')+')">ID</button></td>'+
  '</tr>';
  var extra=(esc(f.sw)?('Software: '+esc(f.sw)+' &nbsp;&middot;&nbsp; '):'')+'Sub-devices: '+f.subs;
  var edit='<tr class="fxedit'+op+'" id="fxe_'+i+'"><td colspan="11">'+
    '<div class="d-flex flex-wrap align-items-end gap-3 py-1">'+
      '<div><label class="form-label small mb-1 text-secondary">DMX start address</label><div class="input-group input-group-sm" style="width:170px">'+
        '<input type="number" min="1" max="512" value="'+f.addr+'" id="addr_'+i+'" class="form-control">'+
        '<button class="btn btn-outline-primary" onclick="rdmSetAddr(\''+f.uid+'\')">Set</button></div></div>'+
      '<div><label class="form-label small mb-1 text-secondary">Device label</label><div class="input-group input-group-sm" style="width:230px">'+
        '<input type="text" maxlength="32" value="'+esc(f.label)+'" id="lbl_'+i+'" class="form-control">'+
        '<button class="btn btn-outline-primary" onclick="rdmSetLabel(\''+f.uid+'\')">Set</button></div></div>'+
      '<button class="btn btn-sm '+(f.identify?'btn-warning':'btn-outline-warning')+'" onclick="rdmIdentify(\''+f.uid+'\','+(f.identify?'false':'true')+')">'+(f.identify?'Identifying…':'Identify')+'</button>'+
      '<div class="text-secondary small ms-auto">'+extra+'</div>'+
    '</div></td></tr>';
  return row+edit;
}
var FXCOLS=[['name','Fixture'],['uid','UID'],['uni','Uni'],['addr','Addr'],['foot','Foot'],['pers','Personality'],
            ['mfg','Manufacturer'],['model','Model'],['cat','Category']];
function sortVal(f,col){
  switch(col){
    case 'name':  return (f.label||f.modelName||('Model '+f.model)).toLowerCase();
    case 'uid':   return f.uid;
    case 'uni':   return f.uni;
    case 'addr':  return f.addr;
    case 'foot':  return f.footprint;
    case 'pers':  return f.pers;
    case 'mfg':   return (f.mfg||'').toLowerCase();
    case 'model': return (f.modelName||('id '+f.model)).toLowerCase();
    case 'cat':   return f.cat;
  }
  return 0;
}
function sortDevices(devs){
  if(!sortCol) return devs;
  return devs.slice().sort(function(a,b){
    var x=sortVal(a,sortCol), y=sortVal(b,sortCol);
    return x<y ? -sortDir : x>y ? sortDir : 0;
  });
}
function setSort(col){
  if(sortCol===col) sortDir=-sortDir; else { sortCol=col; sortDir=1; }
  lastSig='';   // force the table to rebuild in the new order
  refresh();
}
function fxTable(d){
  var th=FXCOLS.map(function(c){
    var ind = sortCol===c[0] ? '<span class="sort-ind">'+(sortDir>0?'▴':'▾')+'</span>' : '';
    return '<th class="sortable" onclick="setSort(\''+c[0]+'\')">'+c[1]+ind+'</th>';
  }).join('');
  return '<table class="fxt"><thead><tr>'+th+'<th>Sensors</th><th></th>'+
    '</tr></thead><tbody>'+sortDevices(d.devices).map(fxRow).join('')+'</tbody></table>';
}
// rebuild the table when structure or any sensor switch changes (values update in place)
function sig(d){
  if(!d.devices) return '';
  return d.devices.map(function(f){
    return f.uid+'|'+f.label+'|'+f.pers+'|'+f.persCount+'|'+f.addr+'|'+f.footprint+'|'+(f.identify?1:0)+'|'+esc(f.mfg)+'|'+esc(f.modelName)+'|'+
      (f.sensors||[]).map(function(s){return (s.poll?1:0)+s.name;}).join(',');
  }).join(';');
}
// update the live sensor-value cells in place (no table rebuild)
function updateSensorCells(d){
  (d.devices||[]).forEach(function(f){
    (f.sensors||[]).forEach(function(s,i){
      var el=document.getElementById('sv_'+fid(f.uid)+'_'+i);
      if(el) el.textContent=s.value+(s.unit?(' '+s.unit):'');
    });
  });
}
function toggleFx(uid){ open[uid]=!open[uid]; var i=fid(uid); ['fxr_','fxe_'].forEach(function(p){ var el=document.getElementById(p+i); if(el) el.classList.toggle('open',open[uid]); }); }

// ── live "scanning the line" progress ──────────────────────────────────────
var PIDLBL=['reading device info','software version','manufacturer','model name','device label',
            'sensor definitions','sensor values','finishing up'];
function scanText(d){
  if(d.discStage===2 && d.discFound) return 'Reading fixture '+Math.min(d.discCur+1,d.discFound)+' of '+d.discFound+' · '+(PIDLBL[d.discSub]||'');
  if(d.discStage===3) return 'Publishing results…';
  return 'Searching the line for fixtures'+(d.discFound? ' · '+d.discFound+' found':'…');
}
function scanPct(d){
  if(d.discStage===2 && d.discFound) return Math.min(100,((d.discCur+(d.discSub/7))/d.discFound)*100);
  if(d.discStage===3) return 100;
  return null;   // search phase: total unknown -> indeterminate bar
}
function scanHtml(d){
  var pct=scanPct(d);
  return '<div class="d-flex justify-content-between align-items-baseline"><span>'+scanText(d)+'</span>'+
    (pct!=null?'<span class="text-secondary small">'+pct.toFixed(0)+'%</span>':'')+'</div>'+
    '<div class="progress mt-1" style="height:8px"><div class="progress-bar progress-bar-striped progress-bar-animated'+
      (pct==null?' w-100':'')+'"'+(pct!=null?' style="width:'+pct.toFixed(0)+'%"':'')+'></div></div>';
}

function render(d){
  lastD=d;
  document.getElementById('art-port').textContent = (d.artPort!=null) ? ('Art-Net RDM: uni '+d.artPort+(d.artnetRdm?'':' (off)')) : '';
  var bq=document.getElementById('bqp-sel');   // keep the Health poll selector in sync (a console can change it)
  if(bq && d.bqPolicy!=null && document.activeElement!==bq) bq.value=(d.bqPolicy>=1&&d.bqPolicy<=3)?d.bqPolicy:4;
  renderMergeCtl(d);   // per-output merge mode (also reflects changes made from a console over Art-Net)
  var st=document.getElementById('rdm-status'), list=document.getElementById('fx-list'), dash=document.getElementById('sensor-dash');
  if(!d.available){ st.textContent='No RDM-capable output configured (an output needs a DE/RE pin).'; list.innerHTML=''; dash.innerHTML=''; lastSig=''; document.getElementById('disc-ctl').innerHTML=''; return; }
  // Buttons stay live except while a scan is actually running, and never grey for >30 s (a stuck
  // scan must still be re-triggerable).
  var busyNow = !!(d.busy||d.discovering);
  if(busyNow){ if(!busySince) busySince=Date.now(); } else busySince=0;
  renderDiscCtl(d, busyNow && (Date.now()-busySince < 30000));
  var has=d.devices&&d.devices.length;
  if(d.busy||d.discovering){ st.innerHTML=scanHtml(d); }
  else if(!has){ st.textContent = d.scanned ? 'No RDM fixtures found.' : 'Discover to scan for fixtures.'; }
  else { st.textContent = d.devices.length+' fixture'+(d.devices.length>1?'s':''); }
  if(!has){ list.innerHTML=''; dash.innerHTML=''; lastSig=''; }
  else {
    renderDash(d);
    var s=sig(d);
    if(s!==lastSig){ list.innerHTML=fxTable(d); lastSig=s; }
    updateSensorCells(d);
    setFixtureAll(d);
    // master switch reflects the per-sensor switches: on when all enabled, dash when mixed
    var all=[]; d.devices.forEach(function(f){ (f.sensors||[]).forEach(function(x){ all.push(!!x.poll); }); });
    var m=document.getElementById('live-sw');
    m.checked = all.length>0 && all.every(Boolean);
    m.indeterminate = all.some(Boolean) && !all.every(Boolean);
  }
  var stx='';
  if(d.artTodReqs!=null) stx='Art-Net RDM: polls '+d.artPolls+' · TOD reqs '+d.artTodReqs+' · RDM reqs '+d.artRdmReqs+' · flushes '+d.artFlushes;
  document.getElementById('art-stats').textContent=stx;
}

// self-scheduling poll: fast (600ms) while a scan runs so the bar moves, 1s live, else 3s.
function schedule(){
  clearTimeout(timer);
  var live=lastD && lastD.sensorPoll;   // any sensor enabled -> poll faster so the graph keeps up
  var ms=(lastD&&(lastD.discovering||lastD.busy))?600:(live?1000:3000);
  timer=setTimeout(tick, ms);
}
function tick(){ fetch('/rdm.json').then(function(r){return r.json();}).then(render).catch(function(){}).then(schedule); }
function refresh(){ clearTimeout(timer); tick(); }
function reschedule(){ schedule(); }

// One Discover per RDM universe (each transceiver line), plus a "Scan all". Falls back to a single
// button when there is only one RDM line.
var pendingDiscLine=-1;
function discUni(line){ var rl=(lastD&&lastD.rdmLines)||[]; for(var i=0;i<rl.length;i++) if(rl[i].line===line) return rl[i].uni; return line; }
function renderDiscCtl(d, dis){
  var dc=document.getElementById('disc-ctl'); if(!dc) return;
  var rl=d.rdmLines||[], D=dis?' disabled':'';
  var busyLine=(d.discovering)?d.discLine:-99;
  var h='';
  if(rl.length>1){
    rl.forEach(function(l){ var on=d.discovering&&(d.discLine===l.line);
      h+='<button class="btn btn-outline-primary btn-sm"'+D+' onclick="rdmDiscover('+l.line+')">'+(on?'Scanning U'+l.uni+'…':'Scan U'+l.uni)+'</button>'; });
    h+='<button class="btn btn-primary btn-sm"'+D+' onclick="rdmDiscover(-1)">Scan all</button>';
  } else {
    h='<button class="btn btn-primary btn-sm"'+D+' onclick="rdmDiscover(-1)">'+(d.discovering?'Scanning…':'Discover')+'</button>';
  }
  dc.innerHTML=h;
}
function rdmDiscover(line){ pendingDiscLine=(line==null?-1:line);
  var t=document.getElementById('disc-modal-txt');
  if(t) t.textContent = pendingDiscLine<0 ? 'This scans every RDM universe.' : 'This scans universe '+discUni(pendingDiscLine)+'.';
  document.getElementById('disc-modal').classList.add('show'); }
function hideDiscModal(){ document.getElementById('disc-modal').classList.remove('show'); }
function startDiscovery(){ hideDiscModal(); send({type:'rdm_discover',line:pendingDiscLine}); busySince=Date.now();
  document.getElementById('rdm-status').textContent='Scanning…'; setTimeout(refresh,800); }
function rdmSetAddr(uid){ var el=document.getElementById('addr_'+fid(uid)); var a=parseInt(el.value,10);
  if(a>=1&&a<=512){ send({type:'rdm_setaddr',uid:uid,addr:a}); setTimeout(refresh,900);} }
function rdmSetLabel(uid){ var el=document.getElementById('lbl_'+fid(uid)); send({type:'rdm_setlabel',uid:uid,label:el.value}); setTimeout(refresh,900); }
function rdmSetPers(uid,p){ send({type:'rdm_setpers',uid:uid,pers:parseInt(p,10)}); setTimeout(refresh,900); }
function rdmIdentify(uid,on){ send({type:'rdm_identify',uid:uid,on:on}); setTimeout(refresh,600); }
function setBqp(v){ fetch('/rdm/bqp?p='+encodeURIComponent(v)).then(function(){setTimeout(refresh,300);}).catch(function(){}); }
function setMerge(out,mode){ fetch('/rdm/merge?out='+out+'&mode='+mode).then(function(){setTimeout(refresh,300);}).catch(function(){}); }
// Per-output merge selectors. Rebuilt only when the set of outputs changes; values are updated in
// place each poll so a change made from a console (over Art-Net) shows up here without a rebuild.
var mergeSig='';
function renderMergeCtl(d){
  var el=document.getElementById('merge-ctl'); if(!el) return;
  var outs=d.outputs||[];
  var s=outs.map(function(o){return o.i+':'+o.uni;}).join(',');
  if(s!==mergeSig){
    mergeSig=s;
    el.innerHTML = outs.length ? ('<span class="text-secondary">Merge</span>'+outs.map(function(o){
      return '<span class="d-inline-flex align-items-center gap-1"><span class="uni-badge">U'+o.uni+'</span>'+
        '<select class="form-select form-select-sm py-0" style="width:auto" id="merge_'+o.i+'" onchange="setMerge('+o.i+',this.value)">'+
        '<option value="0">Off</option><option value="1">HTP</option><option value="2">LTP</option></select></span>';
    }).join(' ')) : '';
  }
  outs.forEach(function(o){ var sel=document.getElementById('merge_'+o.i); if(sel && document.activeElement!==sel) sel.value=o.merge; });
}
function rdmSensSel(uid,idx,on){ send({type:'rdm_sensorsel',uid:uid,sensor:idx,on:on}); reschedule(); setTimeout(refresh,600); }

// The "all" (per-fixture) and "Live sensors" (master) switches are select-off / restore controls,
// never select-all: clicking while anything in scope is on remembers that set and turns it all off;
// clicking again restores exactly what was on. From a cold state (nothing remembered) they do
// nothing, so a stray click can never light up every sensor and tank the DMX frame rate.
function aggClick(ev, scope){
  ev.preventDefault();                       // we drive the state; don't let the checkbox just flip
  var d=lastD; if(!d||!d.devices) return;
  var sens=[];
  d.devices.forEach(function(f){
    if(scope!=='master' && f.uid!==scope) return;
    (f.sensors||[]).forEach(function(s,idx){ sens.push({uid:f.uid,idx:idx,on:!!s.poll}); });
  });
  if(!sens.length) return;
  var key = scope==='master' ? 'master' : ('fix_'+scope);
  if(sens.some(function(x){return x.on;})){    // something on -> remember it, switch all off
    remembered[key]=sens.filter(function(x){return x.on;}).map(function(x){return x.uid+'|'+x.idx;});
    if(scope==='master') send({type:'rdm_sensorpoll',on:false});
    else send({type:'rdm_sensorsel',uid:scope,sensor:-1,on:false});
  } else {                                      // all off -> restore what was remembered (may be none)
    (remembered[key]||[]).forEach(function(k){ var p=k.split('|');
      send({type:'rdm_sensorsel',uid:p[0],sensor:parseInt(p[1],10),on:true}); });
  }
  reschedule(); setTimeout(refresh,600);
}

tick();

)=====";
