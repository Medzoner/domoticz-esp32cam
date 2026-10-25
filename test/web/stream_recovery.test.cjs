const assert = require('node:assert/strict');
const {readFileSync} = require('node:fs');
const {resolve} = require('node:path');
const test = require('node:test');
const vm = require('node:vm');

const script = readFileSync(resolve(__dirname, '../../web/index.js'), 'utf8');
const settle = () => new Promise(resolve => setImmediate(resolve));

function harness() {
  let now = 0, nextTimer = 1, failure = null, aborted = 0;
  const timers = new Map(), elements = new Map(), events = {}, calls = [];
  const state = {chip_temperature_c: 25, uptime_ms: 1000, stream_server: true,
    stream_clients: 1, recording_supported: false};
  const element = () => ({
    hidden: true, append() {}, appendChild() {},
    removeAttribute(name) { delete this[name]; },
    getAttribute(name) { return this[name] ?? null; },
    cloneNode() {
      const copy = element();
      copy.id = this.id;
      copy.hidden = this.hidden;
      return copy;
    },
    replaceWith(node) { elements.set(this.id, node); },
  });
  const byId = id => {
    if (!elements.has(id)) {
      const node = element();
      node.id = id;
      elements.set(id, node);
    }
    return elements.get(id);
  };
  const document = {hidden: false, createElement: element,
    addEventListener: (name, handler) => { events[`document:${name}`] = handler; }};
  const navigator = {onLine: true};
  const context = vm.createContext({
    EspUi: {byId, formatBytes: String, formatDuration: String,
      setConnection: (element, online) => { element.online = online; }},
    location: {hostname: 'camera.test', origin: 'http://camera.test'},
    document, navigator, AbortController, console,
    performance: {now: () => now},
    window: {addEventListener: (name, handler) => { events[`window:${name}`] = handler; }},
    fetch: async (path, options = {}) => {
      calls.push(path);
      if (path === '/health' && failure === 'reject') throw new Error('network lost');
      if (path === '/health' && failure === 'hang') {
        return new Promise((resolve, reject) => options.signal.addEventListener('abort', () => {
          ++aborted;
          reject(new Error('request aborted'));
        }, {once: true}));
      }
      return {ok: true, json: async () => path === '/health' ? state : {files: []}};
    },
    setTimeout: (callback, delay) => {
      const id = nextTimer++;
      timers.set(id, {callback, at: now + delay});
      return id;
    },
    clearTimeout: id => timers.delete(id),
    // Polls are explicit in unit tests; real interval behavior is browser-tested.
    setInterval() {},
  });
  vm.runInContext(script, context);
  return {
    state, calls, byId, context,
    run: code => vm.runInContext(code, context),
    fail: mode => { failure = mode; },
    aborted: () => aborted,
    pendingTimers: () => timers.size,
    nextDelay: () => Math.min(...[...timers.values()].map(timer => timer.at - now)),
    advance(milliseconds) {
      const end = now + milliseconds;
      for (let steps = 0; steps < 10000; ++steps) {
        const entry = [...timers].sort((left, right) => left[1].at - right[1].at)[0];
        if (!entry || entry[1].at > end) { now = end; return; }
        now = entry[1].at;
        timers.delete(entry[0]);
        entry[1].callback();
      }
      throw new Error('timer loop');
    },
    offline() { navigator.onLine = false; events['window:offline'](); },
    online() { navigator.onLine = true; events['window:online'](); },
    visible(visible) { document.hidden = !visible; events['document:visibilitychange'](); },
    frame() { byId('camera').onload(); },
  };
}

async function started() {
  const app = harness();
  await settle();
  app.run('startStream()');
  return app;
}

test('silent EOF after per-frame loads hides stale image and reconnects without error event', async () => {
  const app = await started();
  const original = app.byId('camera').src;
  app.frame(); app.advance(100); app.frame();
  app.advance(11000); // threshold plus the watchdog's one-second polling granularity
  assert.equal(app.byId('camera').hidden, true);
  assert.equal(app.byId('camera').src, undefined);
  assert.equal(app.run('streamWanted'), true);
  app.advance(1000);
  assert.notEqual(app.byId('camera').src, original);
});

test('network probe failure preserves intent and recovery starts a fresh session', async () => {
  const app = await started();
  const original = app.byId('camera').src;
  app.fail('reject');
  await app.run('loadHealth()');
  assert.equal(app.byId('camera').hidden, true);
  assert.equal(app.run('streamWanted'), true);
  app.fail(null);
  await app.run('loadHealth()');
  assert.notEqual(app.byId('camera').src, original);
  assert.equal(app.byId('camera').hidden, false);
});

test('hung health request is aborted and later polling is not permanently locked', async () => {
  const app = await started();
  app.fail('hang');
  const request = app.run('loadHealth()');
  const calls = app.calls.length;
  await app.run('loadHealth()');
  assert.equal(app.calls.length, calls);
  app.advance(4000);
  await request;
  assert.equal(app.aborted(), 1);
  assert.equal(app.run('healthLoading'), false);
  assert.equal(app.byId('camera').hidden, true);
  app.fail(null);
  await app.run('loadHealth()');
  assert.equal(app.byId('camera').hidden, false);
});

test('manual stop cancels retries and network/health recovery cannot restart it', async () => {
  const app = await started();
  app.byId('camera').onerror();
  app.run('stopStream()');
  app.byId('camera').onerror(); // late callback after src removal must be ignored
  assert.equal(app.pendingTimers(), 0);
  assert.equal(app.byId('placeholder').textContent, 'Stream stopped');
  app.advance(60000);
  app.offline(); app.online();
  await settle();
  assert.equal(app.run('streamWanted'), false);
  assert.equal(app.byId('camera').src, undefined);
});

test('offline waits without retry storms and online reconnects once', async () => {
  const app = await started();
  app.offline();
  assert.equal(app.byId('camera').hidden, true);
  assert.equal(app.pendingTimers(), 0);
  app.advance(60000);
  assert.equal(app.byId('camera').src, undefined);
  const generation = app.run('streamGeneration');
  app.online();
  await settle();
  assert.equal(app.run('streamGeneration'), generation + 1);
  assert.equal(app.byId('camera').hidden, false);
});

test('hidden tabs cancel stream timers and reconnect only when visible', async () => {
  const app = await started();
  app.byId('camera').onerror();
  app.visible(false);
  assert.equal(app.pendingTimers(), 0);
  app.advance(60000);
  assert.equal(app.byId('camera').src, undefined);
  app.visible(true);
  await settle();
  assert.match(app.byId('camera').src, /:81\/stream/);
});

test('single-load browser is not periodically disconnected while health stays reachable', async () => {
  const app = await started();
  const original = app.byId('camera').src;
  app.frame();
  app.advance(30000);
  assert.equal(app.byId('camera').src, original);
});

test('first image that never loads triggers a bounded retry', async () => {
  const app = await started();
  const original = app.byId('camera').src;
  app.advance(10000);
  assert.equal(app.byId('camera').hidden, true);
  app.advance(1000);
  assert.notEqual(app.byId('camera').src, original);
});

test('duplicate errors coalesce and exponential backoff remains bounded', async () => {
  const app = await started();
  for (const expected of [1000, 2000, 4000, 8000, 10000, 10000]) {
    app.byId('camera').onerror(); app.byId('camera').onerror();
    assert.equal(app.pendingTimers(), 1);
    assert.equal(app.nextDelay(), expected);
    app.advance(expected);
  }
});

test('healthy multi-frame reception resets reconnect backoff', async () => {
  const app = await started();
  app.byId('camera').onerror(); app.advance(1000);
  app.byId('camera').onerror(); app.advance(2000);
  app.frame(); app.frame();
  app.byId('camera').onerror();
  assert.equal(app.nextDelay(), 1000);
});

test('camera reboot and stream service readiness each refresh a wanted session', async () => {
  const app = await started();
  const original = app.byId('camera').src;
  app.state.uptime_ms = 10;
  await app.run('loadHealth()');
  assert.notEqual(app.byId('camera').src, original);
  app.state.stream_server = false;
  await app.run('loadHealth()');
  assert.equal(app.byId('camera').hidden, true);
  app.state.stream_server = true;
  await app.run('loadHealth()');
  assert.equal(app.byId('camera').hidden, false);
});

test('zero server clients detects a dead single-load session after admission grace', async () => {
  const app = await started();
  app.frame(); app.advance(2000);
  app.state.stream_clients = 0;
  await app.run('loadHealth()');
  assert.equal(app.byId('camera').hidden, true);
  assert.equal(app.run('streamWanted'), true);
});

test('a stopped stream and snapshots do not restart on failed health polling', async () => {
  const app = await started();
  app.run('showStill()');
  const snapshot = app.byId('camera').src;
  app.fail('reject');
  await app.run('loadHealth()');
  app.advance(60000);
  assert.equal(app.byId('camera').src, snapshot);
  assert.equal(app.run('streamWanted'), false);
});

test('late old load cannot masquerade as the new session first frame', async () => {
  const app = await started();
  const previous = app.byId('camera');
  const lateLoad = previous.onload;
  app.run('connectStream()');
  assert.notEqual(app.byId('camera'), previous);
  lateLoad();
  assert.equal(app.run('streamFrameEvents'), 0);
  app.advance(10000);
  assert.equal(app.byId('camera').hidden, true);
});

test('late old errors and loads cannot tear down or mutate a healthy new session', async () => {
  const app = await started();
  const previous = app.byId('camera');
  const lateError = previous.onerror, lateLoad = previous.onload;
  app.run('connectStream()');
  const current = app.byId('camera'), url = current.src;
  app.frame(); app.frame();
  lateError(); lateLoad();
  assert.equal(current.src, url);
  assert.equal(current.hidden, false);
  assert.equal(app.run('streamFrameEvents'), 2);
  assert.equal(app.pendingTimers(), 1);
});

test('late stream callback cannot hide a subsequent still capture', async () => {
  const app = await started();
  const lateError = app.byId('camera').onerror;
  app.run('showStill()');
  const snapshot = app.byId('camera');
  lateError();
  assert.match(snapshot.src, /^\/capture/);
  assert.equal(snapshot.hidden, false);
  snapshot.onerror();
  assert.equal(snapshot.hidden, true);
});
