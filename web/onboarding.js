'use strict';

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
    $('scanStatus').textContent = list.length
      ? `${list.length} network(s) found`
      : 'No networks found';
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
      meta.textContent = `${net.open ? 'Open' : 'Secured'} · ${net.rssi} dBm`;
      button.appendChild(name);
      button.appendChild(meta);
      button.addEventListener('click', () => {
        $('ssid').value = net.ssid;
        $('pass').focus();
      });
      item.appendChild(button);
      nets.appendChild(item);
    });
  } catch {
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
