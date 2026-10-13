#pragma once

namespace app::ui {

inline constexpr char kIndexHtml[] = R"html(
<!doctype html>
<html lang="en-US">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <meta name="theme-color" content="#10151f">
  <title>ESP32-CAM</title>
  <style>
    :root{color-scheme:dark;--bg:#10151f;--panel:#192130;--line:#2c394c;--text:#edf3fb;--muted:#9dafc5;--accent:#53d6a5;--danger:#ff6b7a}
    *{box-sizing:border-box}body{margin:0;background:radial-gradient(circle at top,#1d2b40,var(--bg) 55%);color:var(--text);font:15px system-ui,sans-serif;min-height:100vh}
    main{width:min(1100px,100%);margin:auto;padding:24px}.top{display:flex;align-items:center;justify-content:space-between;gap:16px;margin-bottom:18px}
    h1{font-size:clamp(1.4rem,4vw,2.2rem);margin:0}.badge{border:1px solid var(--line);background:var(--panel);border-radius:999px;padding:8px 12px;color:var(--muted)}
    .online{color:var(--accent)}.layout{display:grid;grid-template-columns:minmax(0,2fr) minmax(260px,1fr);gap:18px}.card{background:color-mix(in srgb,var(--panel) 92%,transparent);border:1px solid var(--line);border-radius:18px;padding:16px;box-shadow:0 18px 50px #0005}
    .viewport{aspect-ratio:4/3;background:#05070a;border-radius:12px;display:grid;place-items:center;overflow:hidden;contain:layout paint}.viewport img{display:block;width:100%;height:100%;object-fit:contain;backface-visibility:hidden}.placeholder{color:var(--muted)}
    .actions{display:flex;flex-wrap:wrap;gap:9px;margin-top:14px}button,select,input{font:inherit}button,select{border:1px solid var(--line);background:#222d3d;color:var(--text);border-radius:10px;padding:10px 13px}button{cursor:pointer}button.primary{background:var(--accent);border-color:var(--accent);color:#08251b;font-weight:700}button.danger{color:#ffdce0;border-color:#74404a}button:hover{filter:brightness(1.12)}
    h2{font-size:1rem;margin:0 0 14px}.field{display:grid;grid-template-columns:1fr auto;gap:8px;align-items:center;margin:13px 0}.field label{color:var(--muted)}.field input[type=range]{grid-column:1/-1;width:100%;accent-color:var(--accent)}
    dl{display:grid;grid-template-columns:1fr auto;gap:9px;margin:0}dt{color:var(--muted)}dd{margin:0;font-variant-numeric:tabular-nums}.footer,.status-meta{margin-top:16px;color:var(--muted);font-size:.85rem}.status-meta{text-align:right}
    @media(max-width:760px){main{padding:12px}.layout{grid-template-columns:1fr}.top{align-items:flex-start;flex-direction:column}.card{border-radius:14px}}
  </style>
</head>
<body><main>
  <header class="top"><div><h1>ESP32-CAM</h1><div class="footer">Multi-client MJPEG streamer</div></div><div id="health" class="badge">Connecting…</div></header>
  <section class="layout">
    <article class="card">
      <div class="viewport"><span id="placeholder" class="placeholder">Stream stopped</span><img id="camera" alt="Camera stream" decoding="async" fetchpriority="high" hidden></div>
      <div class="actions"><button id="start" class="primary">Start stream</button><button id="stop">Stop</button><button id="still">Capture still</button><button id="stopAll" class="danger">Disconnect all streams</button></div>
    </article>
    <aside class="card">
      <h2>Camera settings</h2>
      <div class="field"><label for="size">Resolution</label><select id="size"><option value="5">QVGA · 320×240</option><option value="6">CIF · 400×296</option><option value="8">VGA · 640×480</option><option value="10">SVGA · 800×600</option><option value="12">UXGA · 1600×1200</option></select></div>
      <div class="field"><label for="quality">JPEG quality</label><output id="qualityValue"></output><input id="quality" type="range" min="4" max="40" value="12"></div>
      <div class="field"><label for="brightness">Brightness</label><output id="brightnessValue"></output><input id="brightness" type="range" min="-2" max="2" value="0"></div>
      <div class="field"><label for="contrast">Contrast</label><output id="contrastValue"></output><input id="contrast" type="range" min="-2" max="2" value="0"></div>
      <div class="field"><label><input id="vflip" type="checkbox"> Flip vertically</label></div>
      <div class="field"><label><input id="hmirror" type="checkbox"> Mirror horizontally</label></div>
      <hr style="border:0;border-top:1px solid var(--line);margin:18px 0">
      <h2>System health</h2><dl><dt>Uptime</dt><dd id="uptime">–</dd><dt>Stream server</dt><dd id="server">–</dd><dt>Stream state</dt><dd id="streamState">–</dd><dt>Clients</dt><dd id="clients">–</dd><dt>Peer</dt><dd id="peer">–</dd><dt>Sessions</dt><dd id="sessions">–</dd><dt>Frames</dt><dd id="frames">–</dd><dt>Frame rate</dt><dd id="fps">–</dd><dt>Adaptive profile</dt><dd id="profile">–</dd><dt>Delivery time</dt><dd id="delivery">–</dd><dt>Wi-Fi</dt><dd id="rssi">–</dd><dt>Free memory</dt><dd id="heap">–</dd><dt>Minimum free</dt><dd id="minHeap">–</dd><dt>Largest allocation</dt><dd id="maxAlloc">–</dd><dt>Stack headroom</dt><dd id="stack">–</dd></dl><div id="lastUpdate" class="status-meta">Waiting for health data…</div>
    </aside>
  </section>
</main><script>
'use strict';
const $=id=>document.getElementById(id),image=$('camera'),placeholder=$('placeholder');
const streamUrl=`http://${location.hostname}:81/stream`;
let streamWanted=false,reconnectDelay=1000,reconnectTimer,healthLoading=false;
const kib=value=>`${Math.round(value/1024)} KiB`;
function duration(ms){const seconds=Math.floor(ms/1000),hours=Math.floor(seconds/3600),minutes=Math.floor(seconds%3600/60);return `${hours}h ${minutes}m ${seconds%60}s`}
function displayImage(src){image.src=src;image.hidden=false;placeholder.hidden=true}
function hideImage(message='Stream stopped'){image.removeAttribute('src');image.hidden=true;placeholder.textContent=message;placeholder.hidden=false}
function connectStream(){if(!streamWanted||document.hidden)return;clearTimeout(reconnectTimer);displayImage(`${streamUrl}?t=${Date.now()}`)}
function startStream(){streamWanted=true;reconnectDelay=1000;connectStream()}
function stopStream(){streamWanted=false;clearTimeout(reconnectTimer);hideImage()}
function showStill(){stopStream();displayImage(`/capture?t=${Date.now()}`)}
async function control(name,value){const response=await fetch(`/control?var=${encodeURIComponent(name)}&val=${encodeURIComponent(value)}`,{method:'POST'});if(!response.ok)throw new Error(await response.text())}
async function loadStatus(){try{const state=await fetch('/status').then(r=>{if(!r.ok)throw new Error(`HTTP ${r.status}`);return r.json()});for(const name of ['quality','brightness','contrast']){$(name).value=state[name];$(`${name}Value`).value=state[name]}$('size').value=state.framesize;$('vflip').checked=state.vflip;$('hmirror').checked=state.hmirror}catch(error){console.warn('camera settings unavailable',error)}}
async function loadHealth(){if(healthLoading)return;healthLoading=true;try{const response=await fetch('/health',{cache:'no-store'});if(!response.ok)throw new Error(`HTTP ${response.status}`);const state=await response.json();$('health').textContent='● Online';$('health').classList.add('online');$('uptime').textContent=duration(state.uptime_ms);$('server').textContent=state.stream_server?'Ready':'Stopped';$('streamState').textContent=state.stream_active?'Streaming':'Idle';$('clients').textContent=state.stream_clients;$('peer').textContent=state.stream_peer;$('sessions').textContent=state.stream_sessions;$('frames').textContent=state.stream_frames;$('fps').textContent=`${state.stream_fps} / ${state.stream_target_fps} FPS`;$('profile').textContent=`L${state.stream_pressure} · JPEG +${state.stream_quality_offset}`;$('delivery').textContent=`${state.stream_delivery_ms} ms`;$('rssi').textContent=`${state.rssi} dBm`;$('heap').textContent=kib(state.free_heap);$('minHeap').textContent=kib(state.min_free_heap);$('maxAlloc').textContent=kib(state.max_alloc_heap);$('stack').textContent=`${Math.round(state.stream_stack_headroom/1024*10)/10} KiB`;$('lastUpdate').textContent=`Updated ${new Date().toLocaleTimeString()}`}catch{$('health').textContent='● Offline';$('health').classList.remove('online');$('lastUpdate').textContent='Health endpoint unavailable'}finally{healthLoading=false}}
$('start').onclick=startStream;$('stop').onclick=stopStream;$('still').onclick=showStill;$('stopAll').onclick=async()=>{stopStream();await fetch('/stream-stop',{method:'POST'})};
$('size').onchange=async event=>{await control('framesize',event.target.value)};
for(const name of ['quality','brightness','contrast']){$(name).oninput=event=>$(`${name}Value`).value=event.target.value;$(name).onchange=event=>control(name,event.target.value)}
for(const name of ['vflip','hmirror']){$(name).onchange=event=>control(name,event.target.checked?1:0)}
image.onerror=()=>{hideImage(streamWanted?'Reconnecting…':'Stream unavailable');if(streamWanted&&!document.hidden){reconnectTimer=setTimeout(connectStream,reconnectDelay);reconnectDelay=Math.min(reconnectDelay*2,10000)}};
document.addEventListener('visibilitychange',()=>{if(document.hidden){image.removeAttribute('src')}else{connectStream();loadHealth()}});loadStatus();loadHealth();setInterval(()=>{if(!document.hidden)loadHealth()},2000);
</script></body></html>
)html";

}  // namespace app::ui
