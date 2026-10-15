// cache_guard.mjs - a local guard proxy that keeps ALREADY-SENT message bytes stable, so the
// engine's prefix cache survives a client that re-renders or re-trims earlier messages every turn
// (the oh-my-pi #3406 pattern; also the "OWUI breaks cache reuse for NInfer" symptom).
//
//   client -> 127.0.0.1:1236 (this) -> 127.0.0.1:1234 (ninfer-serve)
//
// For POST /v1/chat/completions it compares the incoming messages with the ones it forwarded last
// time for the same conversation (key = sha1 of the system instruction block + the first 512 chars
// of the first user message). STRICT (default): an already-closed message is restored ONLY when it
// is the SAME message re-serialised - identical once object keys are sorted, i.e. every value
// (text, tool_call_id, image, ...) is byte-equal. A merely shared text prefix is NOT equality: the
// last token can flip a tool result, a call id or an image, so it is never healed.
//
// When the conversation really changed (a shorter history = compaction, a different role/older
// message, a divergent edit, a re-trim, a changed image), it forwards the request untouched and
// says so in the log. Nothing is ever invented: healed messages are always bytes the client itself
// sent earlier in this session.
//
// Env:
//   GUARD_PORT   listen port                (default 1236)
//   GUARD_UPSTREAM upstream base            (default http://127.0.0.1:1234)
//   GUARD_LOG    log file                   (default cache_guard.log in the current directory)
//   GUARD_HEAL   1|0 enable healing         (default 1)
//   GUARD_STRICT 1|0 strict equality only   (default 1; 0 = unsafe prefix-ratio heuristic)
//   GUARD_RATIO  legacy prefix ratio, used only when GUARD_STRICT=0 (default 0.9)
import http from 'node:http';
import fs from 'node:fs';
import crypto from 'node:crypto';

const PORT = Number(process.env.GUARD_PORT || 1236);
const UP = new URL(process.env.GUARD_UPSTREAM || 'http://127.0.0.1:1234');
const LOG = process.env.GUARD_LOG || 'cache_guard.log';
const HEAL = (process.env.GUARD_HEAL || '1') !== '0';
const STRICT = (process.env.GUARD_STRICT || '1') !== '0';
const RATIO = Number(process.env.GUARD_RATIO || 0.9);

// LOCAL PATCH (2026-10-11): recognise the tool-result pruner's own edit of an ALREADY-SENT
// message (newText = oldHead + MARKER + oldTail) and heal it, so a prune cannot invalidate the
// engine prefix cache. Only bytes the client itself sent earlier are restored; MAX_TOKENS still caps growth.
const PRUNE_MARKER = "\n\n[... tool result middle pruned ...]\n\n";
function isPruneEdit(a, b) {
  if (typeof a !== "string" || typeof b !== "string" || b.length === 0 || a.length <= b.length) return false;
  const i = b.indexOf(PRUNE_MARKER);
  if (i < 0) return false;
  const headPart = b.slice(0, i), tailPart = b.slice(i + PRUNE_MARKER.length);
  return headPart.length > 0 && a.startsWith(headPart) && a.endsWith(tailPart);
}

const log = (s) => { const l = `${new Date().toISOString()} ${s}`; try { fs.appendFileSync(LOG, l + '\n'); } catch {} console.log(l); };

/**
 * Conversation state: system-prompt hash -> [entry] (most recently used first).
 *
 * LOCAL PATCH (2026-10-10): the key is the SYSTEM PROMPT ALONE. dsh's compaction can replace
 * the FIRST USER MESSAGE with a checkpoint, which used to rotate the key and make the guard
 * treat a compacted conversation as brand new -- losing byte stability exactly when the engine
 * is re-prefilling. Branches are now separated by MATCHING (up to GUARD_MAX_CONV_PER_KEY
 * candidates per system prompt, best overlap wins) instead of by the key. Under STRICT healing a
 * wrong pick can only refuse to heal -- it still requires a byte-identical message -- so it can
 * never invent bytes.
 */
const conv = new Map();
const MAX_CONV_PER_KEY = Number(process.env.GUARD_MAX_CONV_PER_KEY || 4);
/** Trailing messages that must match before a rewritten opening is read as a compaction. */
const REANCHOR_MIN_TAIL = Number(process.env.GUARD_REANCHOR_MIN_TAIL || 6);
/** Move one branch to the front of its key's list and cap the list. */
function touch(key, entries, entry) {
  const list = conv.get(key) ?? entries;
  const at = list.indexOf(entry);
  if (at >= 0) list.splice(at, 1);
  list.unshift(entry);
  while (list.length > MAX_CONV_PER_KEY) list.pop();
  conv.set(key, list);
  return list;
}

function textOf(m) {
  if (typeof m.content === 'string') return m.content;
  if (Array.isArray(m.content)) return m.content.map((p) => (typeof p === 'string' ? p : (p && p.text) || '')).join('\n');
  return '';
}
// Healing pins the first-seen bytes, so the prompt can only grow at the front. Stop healing once
// the conversation is close to the engine window (--max-context 262144 for dsh), otherwise the
// harness's own trimming/compaction can no longer keep the request inside it and the turn would
// fail with HTTP 400. 210k still sits above dsh's own compaction trigger (~188.7k).
const MAX_TOKENS = Number(process.env.GUARD_MAX_TOKENS || 210000);

// Token estimate that does not under-count CJK (this deployment thinks in Chinese: ~1 token per
// char, while latin text is ~1 per 4). chars/2.2 as a single ratio was off by ~2x here.
function estTokens(messages) {
  let n = 0;
  for (const m of messages) {
    const s = textOf(m);
    let wide = 0;
    for (const ch of s) {
      const c = ch.codePointAt(0);
      if ((c >= 0x2e80 && c <= 0xa4cf) || (c >= 0xac00 && c <= 0xd7a3) ||
          (c >= 0xf900 && c <= 0xfaff) || (c >= 0xfe30 && c <= 0xfe4f) || (c >= 0xff00 && c <= 0xffef)) wide++;
    }
    n += wide + Math.ceil((s.length - wide) / 4);
  }
  return n;
}

function convKey(messages) {
  // LOCAL PATCH (2026-10-10): the system prompt is the only block compaction never rewrites
  // (dsh's selectCompactableRange starts at the first non-system surface node), so it is the only
  // stable conversation key. The old key appended the first user message's first 512 chars, which
  // rotated the moment compaction replaced that message with a checkpoint.
  const instr = messages.filter((m) => m.role === 'system').map(textOf).join('\n').trim();
  if (!instr) return null;
  return crypto.createHash('sha1').update(instr).digest('hex').slice(0, 16);
}
function sortKeys(o) {
  if (Array.isArray(o)) return o.map(sortKeys);
  if (o && typeof o === 'object') {
    const out = {};
    for (const k of Object.keys(o).sort()) out[k] = sortKeys(o[k]);
    return out;
  }
  return o;
}
// "same message, different serialisation" (tool_calls/args key order, whitespace in nested JSON)
const norm = (m) => JSON.stringify(sortKeys(m));
const msgBytes = (m) => JSON.stringify(m);
function commonPrefix(a, b) {
  const n = Math.min(a.length, b.length);
  let i = 0;
  while (i < n && a.charCodeAt(i) === b.charCodeAt(i)) i++;
  return i;
}
/** How many LEADING messages two lists share byte-for-byte. */
function commonHead(a, b) {
  const n = Math.min(a.length, b.length);
  let i = 0;
  while (i < n && msgBytes(a[i]) === msgBytes(b[i])) i++;
  return i;
}
/** How many TRAILING messages two lists share byte-for-byte. */
function commonTail(a, b) {
  const n = Math.min(a.length, b.length);
  let i = 0;
  while (i < n && msgBytes(a[a.length - 1 - i]) === msgBytes(b[b.length - 1 - i])) i++;
  return i;
}

/** returns { msgs, healed, verdict, detail } */
function guard(messages) {
  const key = convKey(messages);
  if (!key) return { msgs: messages, healed: 0, verdict: 'nokey' };
  const snapshot = messages.map((m) => JSON.parse(JSON.stringify(m)));
  const entries = conv.get(key) ?? [];

  // local patch: pick the live branch this request continues. Score by leading overlap, then by
  // trailing overlap -- a compacted continuation keeps the TAIL and rewrites the OPENING, so it
  // has head<=1 (system only) but a long common tail.
  let branch = null;
  let head = 0;
  let tail = 0;
  for (const e of entries) {
    const h = commonHead(e.messages, snapshot);
    const t = commonTail(e.messages, snapshot);
    if (branch === null || h > head || (h === head && t > tail)) { branch = e; head = h; tail = t; }
  }

  // A compacted continuation keeps the RECENT messages and rewrites the OPENING (the first user
  // message becomes the checkpoint). Re-anchor that branch instead of starting a brand-new one,
  // so byte stability resumes immediately after the one unavoidable re-prefill. head>=2 (=system
  // plus at least one real message) is the bar for a normal continuation; shared-system-prompt
  // strangers therefore cannot hijack each other's branch.
  if (branch !== null && head < 2 && tail >= REANCHOR_MIN_TAIL) {
    branch.messages = snapshot;
    touch(key, entries, branch);
    return { msgs: messages, healed: 0, verdict: 'reanchored', detail: `tail=${tail}/${Math.min(branch.messages.length, snapshot.length)} msgs` };
  }
  if (branch === null || head < 2) {
    // a different conversation that happens to share this system prompt: give it its own branch
    touch(key, entries, { messages: snapshot });
    return { msgs: messages, healed: 0, verdict: 'new' };
  }

  const prev = branch.messages;
  if (snapshot.length < prev.length) {
    // same opening but a shorter history: a real trim/branch switch. Adopt it as the branch state.
    branch.messages = snapshot;
    touch(key, entries, branch);
    return { msgs: messages, healed: 0, verdict: 'shrank', detail: `${prev.length}->${snapshot.length} msgs` };
  }
  const out = messages.map((m) => ({ ...m }));
  let healed = 0, firstDiff = -1;
  // every message except the last one is "closed": its bytes must not move
  for (let i = 0; i < Math.min(prev.length, snapshot.length - 1); i++) {
    if (msgBytes(prev[i]) === msgBytes(snapshot[i])) continue;
    if (firstDiff < 0) firstDiff = i;
    const a = textOf(prev[i]);
    const b = textOf(snapshot[i]);
    const cp = commonPrefix(a, b);
    const minLen = Math.min(a.length, b.length);
    // benign == "the same message, re-serialised": identical once object keys are sorted (e.g. a
    // tool_calls/args blob whose keys moved). Every value must match. A shared text head is NOT
    // semantic equality, so the legacy prefix-ratio heuristic is only used when GUARD_STRICT=0.
    const sameShape = norm(prev[i]) === norm(snapshot[i]);
    const pruneEdit = prev[i].role === snapshot[i].role && isPruneEdit(a, b);
    const benign = prev[i].role === snapshot[i].role &&
      (sameShape || pruneEdit || (!STRICT && minLen > 0 && cp >= RATIO * minLen));
    if (!HEAL || !benign) {
      branch.messages = snapshot;
      touch(key, entries, branch);
      return { msgs: messages, healed, verdict: 'rewrite', detail: `msg#${i} role=${snapshot[i].role} cp=${cp}/${minLen} len ${a.length}->${b.length}` };
    }
    out[i] = JSON.parse(JSON.stringify(prev[i]));   // restore the exact message we sent
    healed++;
  }
  if (healed > 0 && MAX_TOKENS > 0) {
    const est = estTokens(out);
    if (est > MAX_TOKENS) {
      branch.messages = snapshot;
      touch(key, entries, branch);
      return { msgs: messages, healed: 0, verdict: 'cap', detail: `est ${est} tok > ${MAX_TOKENS}` };
    }
  }
  branch.messages = out.map((m) => JSON.parse(JSON.stringify(m)));
  touch(key, entries, branch);
  return { msgs: out, healed, verdict: healed ? 'healed' : 'clean', detail: firstDiff >= 0 ? `firstDiff=#${firstDiff}` : '' };
}

const server = http.createServer((req, res) => {
  const chunks = [];
  req.on('data', (c) => chunks.push(c));
  req.on('end', () => {
    const body = Buffer.concat(chunks);
    // LOCAL DIAGNOSTIC (2026-10-10): dump the raw request dsh actually sends.
    // Enabled only when GUARD_DUMP_BODIES points at a file. Never affects forwarding.
    if (process.env.GUARD_DUMP_BODIES) {
      try { fs.appendFileSync(process.env.GUARD_DUMP_BODIES, JSON.stringify({ at: new Date().toISOString(), url: req.url, bytes: body.length, body: body.toString('utf8') }) + '\n'); } catch { /* ignore */ }
    }
    let outBody = body;
    let note = '';
    const isChat = req.method === 'POST' && /\/v1\/chat\/completions$/.test(req.url || '');
    if (isChat && body.length) {
      try {
        const j = JSON.parse(body.toString('utf8'));
        if (Array.isArray(j.messages) && j.messages.length) {
          const t0 = Date.now();
          const r = guard(j.messages);
          if (r.msgs !== j.messages) { j.messages = r.msgs; outBody = Buffer.from(JSON.stringify(j), 'utf8'); }
          note = `guard=${r.verdict} healed=${r.healed} msgs=${j.messages.length} ${r.detail || ''} (${Date.now() - t0}ms)`;
          // local patch: 'reanchored' is logged too -- it is the signature of a compaction.
          if (r.verdict === 'rewrite' || r.verdict === 'shrank' || r.verdict === 'cap' || r.verdict === 'reanchored' || r.healed) log(note);
        }
      } catch (e) { note = 'guard parse error: ' + e.message; }
    }
    const headers = { ...req.headers };
    delete headers.host;
    headers['content-length'] = String(outBody.length);
    const up = http.request({ hostname: UP.hostname, port: UP.port, method: req.method, path: req.url, headers }, (uRes) => {
      res.writeHead(uRes.statusCode || 502, uRes.headers);
      uRes.pipe(res);
    });
    up.on('error', (e) => { log('upstream error: ' + e.message); if (!res.headersSent) res.writeHead(502, { 'content-type': 'application/json' }); res.end(JSON.stringify({ error: { message: 'guard: upstream ' + e.message } })); });
    up.end(outBody);
  });
});
server.listen(PORT, '127.0.0.1', () => log(`cache_guard listening on 127.0.0.1:${PORT} -> ${UP.href} (heal=${HEAL} strict=${STRICT} ratio=${STRICT ? 'n/a' : RATIO})`));

// local patch: exported for the offline unit test (win\test_cache_guard_key.mjs).
export { guard, convKey, commonHead, commonTail, conv };
