// SPDX-License-Identifier: Apache-2.0
// Real HTTP cancellation/streaming tests; no model, GPU or private sessions.
import assert from 'node:assert/strict';
import http from 'node:http';
import fs from 'node:fs';
import { once } from 'node:events';
import { fileURLToPath } from 'node:url';
import { test } from 'node:test';

const deadline = (promise, ms = 2000) => new Promise((resolve, reject) => {
  const timer = setTimeout(() => reject(new Error('HTTP test timed out')), ms);
  promise.then(value => { clearTimeout(timer); resolve(value); }, error => { clearTimeout(timer); reject(error); });
});
const deferred = () => { let resolve; const promise = new Promise(r => { resolve = r; }); return { promise, resolve }; };

test('guard propagates cancellation and preserves completed responses', async t => {
  const cases = new Map();
  const upstream = http.createServer((req, res) => {
    const chunks = [];
    req.on('data', chunk => chunks.push(chunk));
    req.on('end', () => {
      const state = cases.get(req.url);
      assert.ok(state, req.url);
      state.body = Buffer.concat(chunks).toString();
      state.started.resolve();
      res.once('close', () => state.closed.resolve({ complete: res.writableFinished }));
      if (state.mode === 'headers') return;
      if (state.mode === 'before-error') { req.socket.destroy(); return; }
      res.writeHead(200, { 'content-type': 'text/event-stream', 'x-fixture': 'preserved' });
      res.write('data: {"fixture":1}\n\n');
      if (state.mode === 'stream') return;
      if (state.mode === 'after-error') { setImmediate(() => res.destroy()); return; }
      res.end('data: [DONE]\n\n');
    });
  });
  upstream.listen(0, '127.0.0.1');
  await once(upstream, 'listening');
  const savedEnv = Object.fromEntries(['GUARD_PORT', 'GUARD_UPSTREAM', 'GUARD_LOG', 'GUARD_DUMP_BODIES'].map(k => [k, process.env[k]]));
  process.env.GUARD_PORT = '0';
  process.env.GUARD_UPSTREAM = 'http://127.0.0.1:' + upstream.address().port;
  // Node can treat NUL as a normal filename on Windows. Use a unique fixture
  // log and remove only that file after closing the servers.
  const logFile = new URL('.cache-guard-test-' + process.pid + '-' + Date.now() + '.log', import.meta.url);
  process.env.GUARD_LOG = fileURLToPath(logFile);
  delete process.env.GUARD_DUMP_BODIES;
  const { server, guard } = await import('../cache_guard.mjs');
  if (!server.listening) await once(server, 'listening');
  const port = server.address().port;
  t.after(async () => {
    server.closeAllConnections(); upstream.closeAllConnections();
    await Promise.all([new Promise(r => server.close(r)), new Promise(r => upstream.close(r))]);
    fs.rmSync(logFile, { force: true });
    for (const [key, value] of Object.entries(savedEnv)) {
      if (value === undefined) delete process.env[key]; else process.env[key] = value;
    }
  });
  let index = 0;
  function fixture(mode) {
    const path = '/fixture-' + index++;
    const state = { mode, started: deferred(), closed: deferred() };
    cases.set(path, state);
    return { path, state };
  }
  function send(path, onResponse = () => {}) {
    const body = JSON.stringify({ fixture: 'unchanged', messages: [{ role: 'user', content: 'check' }] });
    const request = http.request({ hostname: '127.0.0.1', port, path, method: 'POST',
      headers: { 'content-type': 'application/json', 'content-length': Buffer.byteLength(body) } }, onResponse);
    request.on('error', () => {});
    request.end(body);
    return { request, body };
  }
  for (const mode of ['headers', 'stream']) {
    await t.test('client stop while awaiting ' + mode + ' closes upstream', async () => {
      const { path, state } = fixture(mode);
      const responseReceived = deferred();
      const { request } = send(path, response => { response.on('error', () => {}); response.resume(); responseReceived.resolve(); });
      await deadline(state.started.promise);
      if (mode === 'stream') await deadline(responseReceived.promise);
      request.destroy();
      assert.deepEqual(await deadline(state.closed.promise), { complete: false });
    });
  }
  await t.test('normal completion retains bytes, headers and the forwarded body', async () => {
    for (let i = 0; i < 2; i++) {
      const { path, state } = fixture('normal');
      const done = deferred();
      const { body } = send(path, response => {
        let text = '';
        response.setEncoding('utf8'); response.on('data', chunk => { text += chunk; });
        response.on('end', () => done.resolve({ text, header: response.headers['x-fixture'], status: response.statusCode }));
      });
      assert.deepEqual(await deadline(done.promise), { text: 'data: {"fixture":1}\n\ndata: [DONE]\n\n', header: 'preserved', status: 200 });
      assert.equal(state.body, body);
      assert.deepEqual(await deadline(state.closed.promise), { complete: true });
    }
  });
  await t.test('upstream failure before headers produces 502', async () => {
    const { path } = fixture('before-error'); const done = deferred();
    send(path, response => {
      let text = ''; response.on('data', chunk => { text += chunk; });
      response.on('end', () => done.resolve({ status: response.statusCode, text }));
    });
    const result = await deadline(done.promise);
    assert.equal(result.status, 502); assert.ok(JSON.parse(result.text).error.message.startsWith('guard: upstream'));
  });
  await t.test('truncated upstream stream terminates the client without a fake successful ending', async () => {
    const { path } = fixture('after-error'); const closed = deferred();
    send(path, response => {
      let text = '';
      response.on('data', chunk => { text += chunk; }); response.on('error', () => {});
      response.on('close', () => closed.resolve({ complete: response.complete, text }));
    });
    const result = await deadline(closed.promise);
    assert.equal(result.complete, false); assert.ok(!result.text.includes('[DONE]')); assert.ok(!result.text.includes('guard: upstream'));
  });
  await t.test('an appended checkpoint leaves the already-sent message prefix unchanged', () => {
    const prefix = [
      { role: 'system', content: 'Stable fixture policy' },
      { role: 'user', content: 'Finish the existing file' },
      { role: 'assistant', content: null, tool_calls: [{ id: 'capture', type: 'function', function: { name: 'browser_screenshot', arguments: '{}' } }] },
    ];
    assert.deepEqual(guard(structuredClone(prefix)).msgs, prefix);
    const withCheckpoint = [...prefix,
      { role: 'tool', tool_call_id: 'capture', content: '{"path":"capture.png"}' },
      { role: 'user', content: 'Continue verification of the existing artifact.' },
    ];
    const forwarded = guard(structuredClone(withCheckpoint)).msgs;
    assert.deepEqual(forwarded, withCheckpoint); assert.deepEqual(forwarded.slice(0, prefix.length), prefix);
  });
});
