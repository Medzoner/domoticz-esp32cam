#pragma once

namespace app::ui {

// Onboarding page. The access point name is inserted between the two parts so
// that the page never needs a formatting buffer on the HTTP task stack.
inline constexpr char kOnboardingHtmlBeforeAccessPoint[] = R"html(<!doctype html>
<html lang="en-US">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <meta name="theme-color" content="#10151f">
  <title>Wi-Fi setup · ESP32-CAM</title>
  <style>
    :root{color-scheme:dark;--bg:#10151f;--panel:#192130;--line:#2c394c;--text:#edf3fb;--muted:#9dafc5;--accent:#53d6a5}
    *{box-sizing:border-box}
    body{margin:0;background:radial-gradient(circle at top,#1d2b40,var(--bg) 55%);color:var(--text);font:15px system-ui,sans-serif;min-height:100vh}
    main{width:min(680px,100%);margin:auto;padding:24px}
    .top{display:flex;align-items:center;justify-content:space-between;gap:16px;margin-bottom:18px}
    h1{font-size:clamp(1.4rem,4vw,2rem);margin:0}.sub{margin:6px 0 0;color:var(--muted)}
    .badge{border:1px solid var(--line);background:var(--panel);border-radius:999px;padding:8px 12px;color:var(--muted);white-space:nowrap}
    .card{background:color-mix(in srgb,var(--panel) 92%,transparent);border:1px solid var(--line);border-radius:18px;padding:18px;box-shadow:0 18px 50px #0005}
    h2{font-size:1rem;margin:0 0 12px}.row{display:flex;align-items:center;gap:10px;flex-wrap:wrap}.muted{color:var(--muted);font-size:.88rem}
    button,input{font:inherit}button{border:1px solid var(--line);background:#222d3d;color:var(--text);border-radius:10px;padding:10px 13px;cursor:pointer}
    button:hover{filter:brightness(1.12)}button.primary{background:var(--accent);border-color:var(--accent);color:#08251b;font-weight:700}
    button:disabled{opacity:.55;cursor:default}
    .nets{list-style:none;margin:12px 0 0;padding:0;display:grid;gap:8px;max-height:210px;overflow:auto}
    .net{width:100%;display:flex;justify-content:space-between;gap:10px;text-align:left;background:#222d3d}
    .net .ssid{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.net .meta{color:var(--muted);font-size:.85rem;white-space:nowrap}
    form{margin-top:18px;border-top:1px solid var(--line);padding-top:8px}
    label.lbl{display:block;color:var(--muted);margin:14px 0 6px}
    input[type=text],input[type=password]{width:100%;border:1px solid var(--line);background:#111725;color:var(--text);border-radius:10px;padding:11px 12px}
    input:focus{outline:2px solid var(--accent);outline-offset:1px}
    .pw{display:grid;grid-template-columns:1fr auto;gap:8px}
    .chk{display:flex;gap:9px;align-items:flex-start;margin:16px 0;color:var(--muted)}
    .chk input{accent-color:var(--accent);margin-top:3px}
    .actions{display:flex;gap:10px;flex-wrap:wrap;align-items:center}
    .foot{margin-top:16px;color:var(--muted);font-size:.85rem;line-height:1.6}.foot b{color:var(--text)}
    @media(max-width:560px){main{padding:14px}.card{border-radius:14px}}
  </style>
</head>
<body><main>
  <header class="top">
    <div><h1>Wi-Fi setup</h1><p class="sub">Connect the camera to your network</p></div>
    <span class="badge">Setup mode</span>
  </header>
  <section class="card">
    <h2>Nearby networks</h2>
    <div class="row"><button id="scan" type="button">Scan again</button><span id="scanStatus" class="muted">Looking for networks…</span></div>
    <ul id="nets" class="nets"></ul>
    <form method="post" action="/save" autocomplete="off">
      <label class="lbl" for="ssid">Network name</label>
      <input id="ssid" name="ssid" type="text" maxlength="32" required placeholder="SSID" autocapitalize="none" autocorrect="off" spellcheck="false">
      <label class="lbl" for="pass">Password</label>
      <div class="pw">
        <input id="pass" name="pass" type="password" maxlength="64" placeholder="Leave empty for an open network" autocapitalize="none" autocorrect="off" spellcheck="false">
        <button id="toggle" type="button">Show</button>
      </div>
      <label class="chk"><input type="hidden" name="dhcp" value="0"><input type="checkbox" name="dhcp" value="1" checked> Use DHCP to obtain an address automatically</label>
      <div class="actions"><button class="primary" type="submit">Save and restart</button><span class="muted">The camera restarts once the settings are saved.</span></div>
    </form>
  </section>
  <footer class="foot">Setup access point: <b id="ap">)html";

inline constexpr char kOnboardingHtmlAfterAccessPoint[] = R"html(</b><br>
    Open this page at <b>http://192.168.4.1/</b> if your device did not open it automatically.<br>
    The camera retries the last saved network for 10 minutes, then returns to this setup page.
  </footer>
</main>
<script>
  const $ = id => document.getElementById(id);
  async function scan() {
    $('scan').disabled = true;
    $('scanStatus').textContent = 'Scanning…';
    try {
      const response = await fetch('/scan', {cache: 'no-store'});
      if (!response.ok) throw new Error('scan failed');
      const list = await response.json();
      const nets = $('nets');
      nets.textContent = '';
      $('scanStatus').textContent = list.length ? list.length + ' network(s) found' : 'No networks found';
      list.forEach(net => {
        const item = document.createElement('li');
        const button = document.createElement('button');
        button.type = 'button';
        button.className = 'net';
        const name = document.createElement('span');
        name.className = 'ssid';
        name.textContent = net.ssid;
        const meta = document.createElement('span');
        meta.className = 'meta';
        meta.textContent = (net.open ? 'Open' : 'Secured') + ' · ' + net.rssi + ' dBm';
        button.appendChild(name);
        button.appendChild(meta);
        button.addEventListener('click', () => { $('ssid').value = net.ssid; $('pass').focus(); });
        item.appendChild(button);
        nets.appendChild(item);
      });
    } catch (error) {
      $('scanStatus').textContent = 'Scan failed, type the network name instead';
    } finally {
      $('scan').disabled = false;
    }
  }
  $('scan').addEventListener('click', scan);
  $('toggle').addEventListener('click', () => {
    const field = $('pass');
    const showing = field.type === 'text';
    field.type = showing ? 'password' : 'text';
    $('toggle').textContent = showing ? 'Show' : 'Hide';
  });
  scan();
</script>
</body></html>
)html";

inline constexpr char kOnboardingSavedHtmlBefore[] = R"html(<!doctype html>
<html lang="en-US"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><meta name="theme-color" content="#10151f"><title>Saved · ESP32-CAM</title>
<style>:root{color-scheme:dark}body{margin:0;min-height:100vh;display:grid;place-items:center;background:radial-gradient(circle at top,#1d2b40,#10151f 55%);color:#edf3fb;font:15px system-ui,sans-serif;text-align:center;padding:24px}
main{max-width:460px}h1{font-size:1.5rem;margin:0 0 10px}p{color:#9dafc5;line-height:1.6}b{color:#53d6a5}</style></head>
<body><main><h1>Settings saved</h1><p>The camera is connecting to <b>)html";

inline constexpr char kOnboardingSavedHtmlAfter[] = R"html(</b> and restarting now.</p>
<p>Reconnect your device to the normal Wi-Fi network; the setup access point disappears once the camera is online.</p>
</main></body></html>
)html";

inline constexpr char kOnboardingErrorHtmlBefore[] = R"html(<!doctype html>
<html lang="en-US"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><meta name="theme-color" content="#10151f"><title>Check the form · ESP32-CAM</title>
<style>:root{color-scheme:dark}body{margin:0;min-height:100vh;display:grid;place-items:center;background:radial-gradient(circle at top,#1d2b40,#10151f 55%);color:#edf3fb;font:15px system-ui,sans-serif;text-align:center;padding:24px}
main{max-width:460px}h1{font-size:1.5rem;margin:0 0 10px}p{color:#9dafc5;line-height:1.6}a{color:#53d6a5}</style></head>
<body><main><h1>That did not work</h1><p>)html";

inline constexpr char kOnboardingErrorHtmlAfter[] = R"html(</p><p><a href="/">Back to setup</a></p></main></body></html>
)html";

}  // namespace app::ui
