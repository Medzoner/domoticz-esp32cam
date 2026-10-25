'use strict';

const $ = EspUi.byId;
let image = $('camera');
const placeholder = $('placeholder');
const playback = $('playback');
const playbackPlaceholder = $('playbackPlaceholder');
const streamUrl = `http://${location.hostname}:81/stream`;
const playbackBaseUrl = `http://${location.hostname}:82`;
const snapshotUrl = `${location.origin}/capture`;
let streamWanted = false;
let reconnectDelay = 1000;
let reconnectTimer = null;
let streamWatchdogTimer = null;
let streamFrameEvents = 0;
let streamFrameUpdatesObserved = false;
let streamLastFrameAt = 0;
let streamConnectedAt = 0;
let streamGeneration = 0;
let streamRetryNeeded = false;
let streamReady = null;
let healthReachable = null;
let previousUptime = null;
const streamStallTimeoutMs = 10000;
const healthTimeoutMs = 4000;
let healthLoading = false;
let recordingsLoaded = false;
let recordingWasActive = false;

const memory = EspUi.formatBytes;
const pause = milliseconds => new Promise(resolve => setTimeout(resolve, milliseconds));

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

function replaceCameraImage() {
  const previous = image;
  previous.removeAttribute('src');
  // Clone attributes only, after cancelling the old request. A fresh node keeps
  // late old-request events away from the new request's event handlers.
  const next = previous.cloneNode(false);
  previous.replaceWith(next);
  image = next;
  return next;
}

function networkOffline() {
  return typeof navigator !== 'undefined' && navigator.onLine === false;
}

function clearStreamTimers() {
  clearTimeout(reconnectTimer);
  clearTimeout(streamWatchdogTimer);
  reconnectTimer = null;
  streamWatchdogTimer = null;
}

function reconnectStream(message = 'Reconnecting…') {
  if (!streamWanted) return;
  streamRetryNeeded = true;
  clearTimeout(streamWatchdogTimer);
  streamWatchdogTimer = null;
  hideImage(message);
  if (document.hidden || networkOffline() || reconnectTimer !== null) return;
  reconnectTimer = setTimeout(() => {
    reconnectTimer = null;
    connectStream();
  }, reconnectDelay);
  reconnectDelay = Math.min(reconnectDelay * 2, 10000);
}

function watchStream(generation) {
  if (!streamWanted || document.hidden || generation !== streamGeneration) return;
  const now = performance.now();
  // Firefox reports load per MJPEG frame, even for a truncated final part.
  // Other browsers may report only the first load: do not periodically tear
  // down their healthy streams based on an event they never emit again.
  const noFirstFrame = streamFrameEvents === 0 && now - streamConnectedAt >= streamStallTimeoutMs;
  const framesStopped = streamFrameUpdatesObserved && streamFrameEvents > 0
    && now - streamLastFrameAt >= streamStallTimeoutMs;
  if (noFirstFrame || framesStopped) {
    reconnectStream('Stream interrupted — reconnecting…');
    return;
  }
  streamWatchdogTimer = setTimeout(() => watchStream(generation), 1000);
}

function connectStream() {
  if (!streamWanted || document.hidden) return;
  clearStreamTimers();
  if (networkOffline() || streamReady === false) {
    reconnectStream(networkOffline() ? 'Network unavailable — waiting…' : 'Stream unavailable — retrying…');
    return;
  }
  streamRetryNeeded = false;
  streamFrameEvents = 0;
  streamConnectedAt = performance.now();
  streamLastFrameAt = streamConnectedAt;
  const generation = ++streamGeneration;
  const requestImage = replaceCameraImage();
  const url = `${streamUrl}?t=${Date.now()}&session=${generation}`;
  const activeRequest = () => streamWanted && generation === streamGeneration
    && image === requestImage && requestImage.getAttribute('src') === url;
  requestImage.onerror = () => {
    if (activeRequest()) reconnectStream();
  };
  requestImage.onload = () => {
    if (!activeRequest()) return;
    ++streamFrameEvents;
    streamLastFrameAt = performance.now();
    if (streamFrameEvents >= 2) {
      streamFrameUpdatesObserved = true;
      reconnectDelay = 1000;
    }
  };
  displayImage(url);
  streamWatchdogTimer = setTimeout(() => watchStream(generation), 1000);
}

function startStream() {
  streamWanted = true;
  reconnectDelay = 1000;
  connectStream();
}

function stopStream() {
  streamWanted = false;
  streamRetryNeeded = false;
  ++streamGeneration;
  clearStreamTimers();
  hideImage();
}

function showStill() {
  stopStream();
  const requestImage = replaceCameraImage();
  const generation = streamGeneration;
  requestImage.onerror = () => {
    if (!streamWanted && generation === streamGeneration && image === requestImage
      && requestImage.getAttribute('src')) hideImage('Image unavailable');
  };
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
    $('flash').checked = state.flash;
  } catch (error) {
    console.warn('camera settings unavailable', error);
  }
}

async function loadHealth() {
  if (healthLoading) return;
  healthLoading = true;
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), healthTimeoutMs);
  try {
    const response = await fetch('/health', {cache: 'no-store', signal: controller.signal});
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const state = await response.json();
    if (networkOffline()) throw new Error('Network offline');
    const recovered = healthReachable === false;
    const becameReady = streamReady === false && state.stream_server === true;
    const rebooted = Number.isFinite(previousUptime) && Number.isFinite(state.uptime_ms)
      && state.uptime_ms < previousUptime;
    healthReachable = true;
    previousUptime = state.uptime_ms;
    streamReady = state.stream_server !== false;
    if (streamWanted && !streamReady) {
      reconnectStream('Stream unavailable — retrying…');
    } else if (streamWanted && (becameReady || rebooted || (recovered && streamRetryNeeded))) {
      reconnectDelay = 1000;
      connectStream();
    } else if (streamWanted && !streamRetryNeeded && streamFrameEvents > 0
      && state.stream_clients === 0 && performance.now() - streamConnectedAt >= 2000) {
      reconnectStream('Stream disconnected — reconnecting…');
    }
    EspUi.setConnection($('health'), true);
    $('uptime').textContent = EspUi.formatDuration(state.uptime_ms);
    $('temperature').textContent = `${state.chip_temperature_c.toFixed(1)} °C`;
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
    const recordingKnown = state.recording_stats_valid !== false;
    $('recordingPanel').hidden = !state.recording_supported;
    if (state.recording_supported) {
      $('recordingStatus').textContent = !recordingKnown
        ? 'SD status unavailable — retrying'
        : !state.sd_available
        ? 'SD card unavailable'
        : recordingActive
          ? `Recording segment ${String(state.recording_segment).padStart(5, '0')}`
          : 'Ready';
      $('recordStart').disabled = !recordingKnown || !state.sd_available || recordingActive;
      $('recordStop').disabled = !recordingKnown || !recordingActive;
      $('recordRefresh').disabled = !recordingKnown || recordingActive;
      $('recordedSize').textContent = recordingKnown ? memory(state.recording_bytes) : 'Unknown';
      $('recordedFrames').textContent = recordingKnown ? state.recording_frames : 'Unknown';
      $('sdSpace').textContent = recordingKnown
        ? `${memory(state.sd_free_bytes)} free / ${memory(state.sd_total_bytes)}`
        : 'Unknown';
      if (recordingKnown) {
        if (!recordingActive && (!recordingsLoaded || recordingWasActive)) loadRecordings();
        recordingWasActive = recordingActive;
      }
    }
    $('lastUpdate').textContent = `Updated ${new Date().toLocaleTimeString()}`;
  } catch {
    healthReachable = false;
    reconnectStream('Camera unreachable — reconnecting…');
    EspUi.setConnection($('health'), false);
    $('recordingStatus').textContent = 'Recording status unavailable — retrying';
    for (const id of ['recordStart', 'recordStop', 'recordRefresh']) {
      $(id).disabled = true;
    }
    for (const id of ['recordedSize', 'recordedFrames', 'sdSpace']) {
      $(id).textContent = 'Unknown';
    }
    $('lastUpdate').textContent = 'Health endpoint unavailable';
  } finally {
    clearTimeout(timeout);
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
for (const name of ['vflip', 'hmirror', 'flash']) {
  $(name).onchange = event => control(name, event.target.checked ? 1 : 0);
}
playback.onerror = () => stopPlayback('Playback ended or unavailable');
document.addEventListener('visibilitychange', () => {
  if (document.hidden) {
    ++streamGeneration;
    clearStreamTimers();
    image.removeAttribute('src');
  }
  else {
    connectStream();
    loadHealth();
  }
});
window.addEventListener('offline', () => {
  healthReachable = false;
  clearStreamTimers();
  reconnectStream('Network unavailable — waiting…');
});
window.addEventListener('online', () => {
  if (streamWanted && !document.hidden) {
    reconnectDelay = 1000;
    connectStream();
  }
  if (!document.hidden) loadHealth();
});
loadStatus();
loadHealth();
setInterval(() => {
  if (!document.hidden) loadHealth();
}, 2000);
