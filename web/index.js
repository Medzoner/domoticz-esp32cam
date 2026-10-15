'use strict';

const $ = id => document.getElementById(id);
const image = $('camera');
const placeholder = $('placeholder');
const playback = $('playback');
const playbackPlaceholder = $('playbackPlaceholder');
const streamUrl = `http://${location.hostname}:81/stream`;
const playbackBaseUrl = `http://${location.hostname}:82`;
const snapshotUrl = `${location.origin}/capture`;
let streamWanted = false;
let reconnectDelay = 1000;
let reconnectTimer;
let healthLoading = false;
let recordingsLoaded = false;
let recordingWasActive = false;

const memory = value => value >= 1048576
  ? `${(value / 1048576).toFixed(1)} MiB`
  : `${Math.round(value / 1024)} KiB`;
const pause = milliseconds => new Promise(resolve => setTimeout(resolve, milliseconds));

function duration(ms) {
  const seconds = Math.floor(ms / 1000);
  const hours = Math.floor(seconds / 3600);
  const minutes = Math.floor(seconds % 3600 / 60);
  return `${hours}h ${minutes}m ${seconds % 60}s`;
}

function displayImage(src) {
  image.src = src;
  image.hidden = false;
  placeholder.hidden = true;
}

function hideImage(message = 'Stream stopped') {
  image.removeAttribute('src');
  image.hidden = true;
  placeholder.textContent = message;
  placeholder.hidden = false;
}

function connectStream() {
  if (!streamWanted || document.hidden) return;
  clearTimeout(reconnectTimer);
  displayImage(`${streamUrl}?t=${Date.now()}`);
}

function startStream() {
  streamWanted = true;
  reconnectDelay = 1000;
  connectStream();
}

function stopStream() {
  streamWanted = false;
  clearTimeout(reconnectTimer);
  hideImage();
}

function showStill() {
  stopStream();
  displayImage(`/capture?t=${Date.now()}`);
}

async function control(name, value) {
  const response = await fetch(
    `/control?var=${encodeURIComponent(name)}&val=${encodeURIComponent(value)}`,
    {method: 'POST'},
  );
  if (!response.ok) throw new Error(await response.text());
}

async function mutate(path) {
  const response = await fetch(path, {method: 'POST'});
  if (!response.ok) throw new Error(await response.text());
  return response;
}

function stopPlayback(message = 'Choose a recording') {
  playback.removeAttribute('src');
  playback.hidden = true;
  playbackPlaceholder.textContent = message;
  playbackPlaceholder.hidden = false;
}

function playRecording(id) {
  stopStream();
  playback.src = `${playbackBaseUrl}/play?id=${id}`;
  playback.hidden = false;
  playbackPlaceholder.hidden = true;
}

async function deleteRecording(id) {
  if (!confirm(`Delete recording ${String(id).padStart(5, '0')}?`)) return;
  stopPlayback();
  await mutate(`/recording/delete?id=${id}`);
  await loadRecordings();
}

async function loadRecordings() {
  const container = $('recordingFiles');
  try {
    const response = await fetch('/recordings', {cache: 'no-store'});
    if (!response.ok) throw new Error(await response.text());
    const state = await response.json();
    container.textContent = '';
    if (!state.files.length) {
      const empty = document.createElement('span');
      empty.className = 'muted';
      empty.textContent = 'No recordings on the SD card.';
      container.appendChild(empty);
    }
    for (const file of state.files) {
      const row = document.createElement('div');
      row.className = 'file-row';
      const label = document.createElement('div');
      const name = document.createElement('div');
      name.className = 'file-name';
      name.textContent = `recording-${String(file.id).padStart(5, '0')}`;
      const size = document.createElement('div');
      size.className = 'muted';
      size.textContent = memory(file.bytes);
      label.append(name, size);
      const actions = document.createElement('div');
      actions.className = 'file-actions';
      const play = document.createElement('button');
      play.textContent = 'Play';
      play.onclick = () => playRecording(file.id);
      const download = document.createElement('button');
      download.textContent = 'Download';
      download.onclick = async () => {
        stopPlayback();
        await pause(350);
        location.href = `${playbackBaseUrl}/download?id=${file.id}`;
      };
      const remove = document.createElement('button');
      remove.textContent = 'Delete';
      remove.className = 'danger';
      remove.onclick = () => deleteRecording(file.id);
      actions.append(play, download, remove);
      row.append(label, actions);
      container.appendChild(row);
    }
    recordingsLoaded = true;
  } catch (error) {
    container.textContent = error.message || 'Recordings unavailable';
  }
}

async function loadStatus() {
  try {
    const response = await fetch('/status');
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const state = await response.json();
    for (const name of ['quality', 'brightness', 'contrast']) {
      $(name).value = state[name];
      $(`${name}Value`).value = state[name];
    }
    $('size').value = state.framesize;
    $('vflip').checked = state.vflip;
    $('hmirror').checked = state.hmirror;
  } catch (error) {
    console.warn('camera settings unavailable', error);
  }
}

async function loadHealth() {
  if (healthLoading) return;
  healthLoading = true;
  try {
    const response = await fetch('/health', {cache: 'no-store'});
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const state = await response.json();
    $('health').textContent = '● Online';
    $('health').classList.add('online');
    $('uptime').textContent = duration(state.uptime_ms);
    $('server').textContent = state.stream_server ? 'Ready' : 'Stopped';
    $('streamState').textContent = state.stream_active ? 'Streaming' : 'Idle';
    $('clients').textContent = state.stream_clients;
    $('peer').textContent = state.stream_peer;
    $('sessions').textContent = state.stream_sessions;
    $('frames').textContent = state.stream_frames;
    $('fps').textContent = `${state.stream_fps} / ${state.stream_target_fps} FPS`;
    $('profile').textContent = `L${state.stream_pressure} · JPEG +${state.stream_quality_offset}`;
    $('delivery').textContent = `${state.stream_delivery_ms} ms`;
    $('rssi').textContent = `${state.rssi} dBm`;
    $('totalMemory').textContent = memory(state.heap_size + state.psram_size);
    $('freeMemory').textContent = memory(state.free_heap + state.free_psram);
    $('heap').textContent = `${memory(state.free_heap)} / ${memory(state.heap_size)}`;
    $('psram').textContent = `${memory(state.free_psram)} / ${memory(state.psram_size)}`;
    $('minHeap').textContent = memory(state.min_free_heap);
    $('maxAlloc').textContent = memory(state.max_alloc_heap);
    $('stack').textContent = `${Math.round(state.stream_stack_headroom / 1024 * 10) / 10} KiB`;
    const recordingActive = state.recording_active || state.recording_pending;
    $('recordingPanel').hidden = !state.recording_supported;
    if (state.recording_supported) {
      $('recordingStatus').textContent = !state.sd_available
        ? 'SD card unavailable'
        : recordingActive
          ? `Recording segment ${String(state.recording_segment).padStart(5, '0')}`
          : 'Ready';
      $('recordStart').disabled = !state.sd_available || recordingActive;
      $('recordStop').disabled = !recordingActive;
      $('recordRefresh').disabled = recordingActive;
      $('recordedSize').textContent = memory(state.recording_bytes);
      $('recordedFrames').textContent = state.recording_frames;
      $('sdSpace').textContent = `${memory(state.sd_free_bytes)} free / ${memory(state.sd_total_bytes)}`;
      if (!recordingActive && (!recordingsLoaded || recordingWasActive)) loadRecordings();
      recordingWasActive = recordingActive;
    }
    $('lastUpdate').textContent = `Updated ${new Date().toLocaleTimeString()}`;
  } catch {
    $('health').textContent = '● Offline';
    $('health').classList.remove('online');
    $('lastUpdate').textContent = 'Health endpoint unavailable';
  } finally {
    healthLoading = false;
  }
}

for (const [id, url] of [['streamLink', streamUrl], ['snapshotLink', snapshotUrl]]) {
  $(id).href = url;
  $(id).textContent = url;
}
$('start').onclick = startStream;
$('stop').onclick = stopStream;
$('still').onclick = showStill;
$('stopAll').onclick = async () => {
  stopStream();
  await fetch('/stream-stop', {method: 'POST'});
};
$('recordStart').onclick = async () => {
  stopPlayback();
  await pause(350);
  await mutate('/recording/start');
  recordingsLoaded = false;
  loadHealth();
};
$('recordStop').onclick = async () => {
  await mutate('/recording/stop');
  loadHealth();
};
$('recordRefresh').onclick = loadRecordings;
$('playbackStop').onclick = () => stopPlayback();
$('size').onchange = event => control('framesize', event.target.value);
for (const name of ['quality', 'brightness', 'contrast']) {
  $(name).oninput = event => $(`${name}Value`).value = event.target.value;
  $(name).onchange = event => control(name, event.target.value);
}
for (const name of ['vflip', 'hmirror']) {
  $(name).onchange = event => control(name, event.target.checked ? 1 : 0);
}
image.onerror = () => {
  hideImage(streamWanted ? 'Reconnecting…' : 'Stream unavailable');
  if (streamWanted && !document.hidden) {
    reconnectTimer = setTimeout(connectStream, reconnectDelay);
    reconnectDelay = Math.min(reconnectDelay * 2, 10000);
  }
};
playback.onerror = () => stopPlayback('Playback ended or unavailable');
document.addEventListener('visibilitychange', () => {
  if (document.hidden) image.removeAttribute('src');
  else {
    connectStream();
    loadHealth();
  }
});
loadStatus();
loadHealth();
setInterval(() => {
  if (!document.hidden) loadHealth();
}, 2000);
