const assert = require('node:assert/strict');
const {readFileSync} = require('node:fs');
const {resolve} = require('node:path');
const test = require('node:test');
const vm = require('node:vm');

const script = readFileSync(resolve(__dirname, '../../web/index.js'), 'utf8');
const settle = () => new Promise(resolve => setImmediate(resolve));

test('resolution labels use the pinned camera SDK frame-size values', () => {
  const html = readFileSync(resolve(__dirname, '../../web/index.html'), 'utf8');
  for (const [id, label] of [[8, 'VGA · 640×480'], [9, 'SVGA · 800×600'],
    [10, 'XGA · 1024×768'], [12, 'SXGA · 1280×1024'], [13, 'UXGA · 1600×1200']]) {
    assert.ok(html.includes(`<option value="${id}">${label}</option>`));
  }
});

function harness(state) {
  const elements = new Map();
  const calls = [];
  let healthFailure = null;
  const element = () => ({append() {}, appendChild() {}, removeAttribute() {}});
  const byId = id => {
    if (!elements.has(id)) elements.set(id, element());
    return elements.get(id);
  };
  const context = vm.createContext({
    EspUi: {
      byId,
      formatBytes: String,
      formatDuration: String,
      setConnection: (element, online) => { element.online = online; },
    },
    location: {hostname: 'camera.test', origin: 'http://camera.test'},
    document: {hidden: true, addEventListener() {}, createElement: element},
    window: {addEventListener() {}},
    AbortController,
    performance,
    fetch: async path => {
      calls.push(path);
      if (path === '/health' && healthFailure === 'fetch') throw new Error('network unavailable');
      return {
        ok: !(path === '/health' && healthFailure === 'http'),
        status: 503,
        json: async () => {
          if (path === '/health' && healthFailure === 'json') throw new SyntaxError('bad JSON');
          return path === '/health' ? state : {files: []};
        },
      };
    },
    setInterval() {},
    setTimeout() {},
    clearTimeout() {},
    console,
  });
  vm.runInContext(script, context);
  return {byId, calls, context, failHealth: mode => { healthFailure = mode; }};
}

function state(overrides = {}) {
  return {
    chip_temperature_c: 25,
    recording_supported: true,
    recording_stats_valid: true,
    sd_available: true,
    recording_active: false,
    recording_pending: false,
    recording_frames: 7,
    ...overrides,
  };
}

test('unknown SD stats disable mutations and do not look like an empty card', async () => {
  const app = harness(state({recording_stats_valid: false}));
  await settle();
  assert.match(app.byId('recordingStatus').textContent, /unavailable/);
  for (const id of ['recordStart', 'recordStop', 'recordRefresh']) {
    assert.equal(app.byId(id).disabled, true);
  }
  assert.equal(app.byId('recordedFrames').textContent, 'Unknown');
  assert.equal(app.byId('sdSpace').textContent, 'Unknown');
  assert.equal(app.calls.includes('/recordings'), false);
  assert.equal(app.byId('health').online, true); // diagnostics are still reachable
});

test('known idle stats permit start and listing', async () => {
  const app = harness(state());
  await settle();
  assert.equal(app.byId('recordingStatus').textContent, 'Ready');
  assert.equal(app.byId('recordStart').disabled, false);
  assert.equal(app.byId('recordStop').disabled, true);
  assert.equal(app.byId('recordRefresh').disabled, false);
  assert.equal(app.calls.includes('/recordings'), true);
});

test('health polling recovers from unknown state without losing recording transition', async () => {
  const current = state({recording_active: true});
  const app = harness(current);
  await settle();
  current.recording_stats_valid = false;
  current.recording_active = false;
  await vm.runInContext('loadHealth()', app.context);
  assert.equal(app.calls.includes('/recordings'), false);
  current.recording_stats_valid = true;
  await vm.runInContext('loadHealth()', app.context);
  await settle();
  assert.equal(app.byId('recordedFrames').textContent, 7);
  assert.equal(app.byId('recordStart').disabled, false);
  assert.equal(app.calls.filter(path => path === '/recordings').length, 1);
});

test('older firmware without a validity flag remains compatible', async () => {
  const current = state();
  delete current.recording_stats_valid;
  const app = harness(current);
  await settle();
  assert.equal(app.byId('recordingStatus').textContent, 'Ready');
  assert.equal(app.byId('recordStart').disabled, false);
});

for (const mode of ['fetch', 'http', 'json']) {
  test(`health ${mode} failure clears stale recording controls and recovers`, async () => {
    const app = harness(state());
    await settle();
    assert.equal(app.byId('recordStart').disabled, false);
    app.failHealth(mode);
    await vm.runInContext('loadHealth()', app.context);
    for (const id of ['recordStart', 'recordStop', 'recordRefresh']) {
      assert.equal(app.byId(id).disabled, true);
    }
    assert.equal(app.byId('recordedFrames').textContent, 'Unknown');
    assert.equal(app.byId('health').online, false);
    app.failHealth(null);
    await vm.runInContext('loadHealth()', app.context);
    assert.equal(app.byId('recordStart').disabled, false);
    assert.equal(app.byId('recordedFrames').textContent, 7);
    assert.equal(app.byId('health').online, true);
  });
}
