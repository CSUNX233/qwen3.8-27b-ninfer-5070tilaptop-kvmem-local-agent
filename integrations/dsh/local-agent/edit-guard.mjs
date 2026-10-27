// SPDX-License-Identifier: Apache-2.0
// Pre-execution guard for explicit single-file exact replacements, plus
// post-execution verification. Reads only existing session events and the
// pending call. Never rewrites arguments, scans the disk, or adds a model request.
import path from 'node:path';
import { appendFileSync, mkdirSync, renameSync, statSync } from 'node:fs';
import os from 'node:os';
import { isLocalAgent } from './tool-grounded-execution.mjs';

export const name = 'local-edit-guard';
export const inject = ['tools'];

const fileTools = new Set(['read', 'edit', 'write']);
const maxEvents = 512;
const logDir = process.env.EDIT_GUARD_LOG_DIR || path.join(os.tmpdir(), 'ninfer-edit-guard');
const logMaxBytes = 2 * 1024 * 1024;
const directPattern = /只改|只修改|仅修改|只替换|仅替换|不要修改其他|不修改其他|不搜索或修改其他|直接做这一个修改|只改这一个文件|唯一|single|only|do not modify|don't modify|exact replacement|do not search|don't search/i;
const replacePattern = /改成|改为|替换|replace|change .{0,60} to|→|->/i;
const newPattern = /新建|创建|new file|create (?:a )?new file|write (?:a )?new|create .{0,40}file/i;
const switchPattern = /切换|switch to|now use|改用|换成/i;
const renamePattern = /重命名|rename|移动目录|move (?:the )?directory/i;
const readPattern = /读取|打开|查看|\bread\b|inspect|check/i;
const quotedPattern = /'([^'\r\n]{1,400})'|"([^"\r\n]{1,400})"|`([^`\r\n]{1,400})`|“([^”\r\n]{1,400})”|‘([^’\r\n]{1,400})’/g;
const pathPattern = /(?:file:\/\/\/|[A-Za-z]:[\\/])[^\s"'`<>|;,)\]}，。；：、！？（）【】《》“”‘’]+/g;

function textBlocks(content) {
  return (content ?? []).filter(block => block?.type === 'text' && typeof block.text === 'string')
    .map(block => block.text).join('\n');
}

function eventUserText(event) {
  if (event?.type !== 'user/message' || event.data?.source?.kind !== 'user') return null;
  const text = textBlocks(event.data.content);
  return text ? text.slice(0, 8000) : null;
}

function normalPath(value, cwd) {
  if (typeof value !== 'string' || !value || value.length > 2048 || /[\r\n\0]/.test(value)) return null;
  let text = value.trim();
  if (text.startsWith('file:')) {
    try {
      const url = new URL(text);
      if (url.host) return null;
      text = decodeURIComponent(url.pathname).replace(/^\/([A-Za-z]:)/, '$1');
    } catch { return null; }
  }
  if (!/^[A-Za-z]:[\\/]/.test(text)) {
    if (!cwd || /^(?:[a-z]+:|[\\/])/i.test(text)) return null;
    text = path.win32.resolve(cwd, text);
  }
  return path.win32.normalize(text);
}

function samePath(a, b) {
  return typeof a === 'string' && typeof b === 'string' && a.toLowerCase() === b.toLowerCase();
}

function quotedLiterals(text) {
  const values = [];
  for (const match of text.matchAll(quotedPattern)) {
    const value = match.slice(1).find(item => item != null);
    if (value == null || /^[A-Za-z]:[\\/]/.test(value) || values.includes(value)) continue;
    values.push(value);
    if (values.length >= 8) break;
  }
  return values;
}

function explicitPaths(text) {
  const values = [];
  for (const match of text.slice(0, 8000).matchAll(pathPattern)) {
    const value = match[0].replace(/[.,;:!?]+$/, '');
    if (!values.includes(value)) values.push(value);
    if (values.length >= 8) break;
  }
  return values;
}

function explicitContent(text) {
  const patterns = [
    /文件内容(?:准确)?(?:写为|写成|为)[:：]?\s*([^\s。；，,;\r\n]+)/,
    /content\s+(?:exactly|is)\s*["'`]?([^"'`\r\n]+?)["'`]?(?:\s|$)/i,
  ];
  for (const pattern of patterns) {
    const match = pattern.exec(text);
    if (match && match[1]) return match[1].replace(/^["'`]|["'`]$/g, '').trim();
  }
  return null;
}

function fileLike(value) {
  const last = value.replace(/[\\/]+$/, '').split(/[\\/]/).pop() ?? '';
  return /\.[A-Za-z0-9]{1,12}$/.test(last);
}

function parseReadEnvelope(text) {
  if (typeof text !== 'string') return null;
  const pathMatch = /^<path>([^\r\n]+)<\/path>/m.exec(text);
  if (!pathMatch) return null;
  const contentMatch = /<content>\r?\n([\s\S]*?)\r?\n<\/content>/.exec(text);
  if (!contentMatch) return { path: pathMatch[1].trim(), text: '', complete: false };
  const body = contentMatch[1];
  const rawLines = body.split(/\r?\n/);
  const lines = [];
  let numbered = 0;
  let capped = false;
  let truncated = false;
  for (const line of rawLines) {
    if (/^\((?:Output capped|Showing lines|End of file)/.test(line)) {
      capped ||= /Output capped|Showing lines/.test(line);
      continue;
    }
    if (/\.\.\. \(line truncated to \d+ chars\)$/.test(line)) truncated = true;
    const match = /^(\d+): ?(.*)$/.exec(line);
    if (match) { numbered++; lines.push(match[2]); } else lines.push(line);
  }
  const useLines = numbered > 0 && numbered >= lines.length - 1 ? lines : rawLines;
  return { path: pathMatch[1].trim(), text: useLines.join('\n'), complete: !capped && !truncated };
}

function knownPaths(events) {
  const calls = new Map();
  const paths = new Map();
  const add = value => {
    if (typeof value === 'string' && /^[A-Za-z]:[\\/]/.test(value)) {
      const normalized = path.win32.normalize(value);
      paths.set(normalized.toLowerCase(), normalized);
    }
  };
  for (const event of events) {
    if (event.type === 'tool/call') calls.set(event.data?.callId, event.data);
    if (event.type === 'user/message' && event.data?.source?.kind === 'user') {
      for (const value of explicitPaths(textBlocks(event.data.content))) add(value);
    }
    if (event.type !== 'tool/result') continue;
    for (const block of event.data?.message?.content ?? []) {
      if (block.type !== 'tool-result' || block.isError !== false) continue;
      const call = calls.get(block.toolCallId);
      if (call && ['read', 'edit', 'write'].includes(call.name)) add(call.arguments?.file_path);
      for (const item of block.content ?? []) {
        if (item.type !== 'text') continue;
        const match = /^<path>([^\r\n]+)<\/path>/.exec(item.text);
        if (match) add(match[1].trim());
      }
    }
  }
  return paths;
}

function closeName(a, b) {
  if (a === b) return true;
  if (Math.min(a.length, b.length) < 4 || Math.max(a.length, b.length) > 255 ||
      Math.abs(a.length - b.length) > 2) return false;
  let previous = Array.from({ length: b.length + 1 }, (_, i) => i);
  for (let i = 1; i <= a.length; i++) {
    const row = [i];
    for (let j = 1; j <= b.length; j++) {
      row[j] = Math.min(row[j - 1] + 1, previous[j] + 1,
        previous[j - 1] + (a[i - 1] === b[j - 1] ? 0 : 1));
    }
    if (Math.min(...row) > 2) return false;
    previous = row;
  }
  return previous[b.length] <= 2;
}

function relatedPath(attempted, candidate) {
  const a = attempted.toLowerCase().split('\\');
  const b = candidate.toLowerCase().split('\\');
  if (a.length !== b.length || a[0] !== b[0]) return false;
  let different = 0;
  for (let i = 1; i < a.length; i++) {
    if (a[i] === b[i]) continue;
    if (++different > 1 || !closeName(a[i], b[i])) return false;
  }
  return different === 1;
}

function nearMissNote(callPath, events) {
  const known = knownPaths(events);
  if (known.has(callPath.toLowerCase())) return null;
  const candidates = [...known.values()].filter(candidate => relatedPath(callPath, candidate));
  if (candidates.length !== 1) return null;
  return 'Execution check: this path is not a known path in this session and is a near-miss of ' +
    `${JSON.stringify(candidates[0])}. The tool was not run and no arguments were rewritten. ` +
    'Use the exact path from a successful read/write or the latest user instruction. If this is an ' +
    'intentional new file or directory switch, state it in the user instruction.';
}

function findSource(events, userIndex, old, explicitFile) {
  const calls = new Map();
  let best = null;
  let fallback = null;
  for (let i = 0; i < events.length; i++) {
    const event = events[i];
    if (event.type === 'tool/call') calls.set(event.data?.callId, event.data);
    if (event.type !== 'tool/result') continue;
    for (const block of event.data?.message?.content ?? []) {
      if (block.type !== 'tool-result' || block.isError !== false) continue;
      const call = calls.get(block.toolCallId);
      if (!call || call.name !== 'read') continue;
      for (const item of block.content ?? []) {
        if (item.type !== 'text') continue;
        const parsed = parseReadEnvelope(item.text);
        if (!parsed) continue;
        const afterUser = i > userIndex;
        if (old && parsed.text.includes(old) && (afterUser || !best)) best = parsed;
        if (explicitFile && samePath(parsed.path, explicitFile) && (afterUser || !fallback)) fallback = parsed;
      }
    }
  }
  return best ?? fallback;
}

function detectIntent(text) {
  const isNew = newPattern.test(text);
  const isSwitch = switchPattern.test(text);
  const isReplace = replacePattern.test(text);
  if (isNew && !isReplace) return 'new';
  if (isSwitch && !isReplace) return 'switch';
  if (isReplace || renamePattern.test(text)) return 'edit';
  if (readPattern.test(text)) return 'read';
  return null;
}

export function derivePending(events) {
  const slice = (Array.isArray(events) ? events : []).slice(-maxEvents);
  let userIndex = -1;
  let userText = null;
  for (let i = slice.length - 1; i >= 0; i--) {
    const text = eventUserText(slice[i]);
    if (text) { userIndex = i; userText = text; break; }
  }
  if (userIndex < 0) return null;
  const intent = detectIntent(userText);
  if (!intent) return null;
  const literals = quotedLiterals(userText);
  const paths = explicitPaths(userText);
  const explicitFile = [...paths].reverse().find(fileLike) ?? null;
  if (intent === 'new') {
    return { kind: 'new', target: paths.at(-1) ?? null, content: literals[0] ?? explicitContent(userText),
      direct: true, text: userText };
  }
  if (intent === 'read' || intent === 'switch') {
    return { kind: intent, target: paths.at(-1) ?? null, direct: false, text: userText };
  }
  if (literals.length < 2) return null;
  const fileTargets = [...new Set(paths.filter(fileLike))];
  if (fileTargets.length >= 2) {
    return { kind: 'edit', multi: true, targets: fileTargets, old: literals[0], new: literals[1],
      direct: false, text: userText };
  }
  const source = findSource(slice, userIndex, literals[0], explicitFile);
  return { kind: 'edit', target: source?.path ?? explicitFile, old: literals[0], new: literals[1],
    source, direct: directPattern.test(userText), text: userText };
}

function countOccurrences(source, needle) {
  if (!needle) return 0;
  let count = 0;
  let index = 0;
  while ((index = source.indexOf(needle, index)) !== -1) { count++; index += needle.length; }
  return count;
}

function feedback(pending, reason) {
  const replacement = pending.old || pending.new
    ? `Required exact replacement: old_string=${JSON.stringify(pending.old)} -> new_string=${JSON.stringify(pending.new)}\n`
    : '';
  return `Execution check: ${reason} The tool was not run and no arguments were rewritten.\n` +
    `Target file: ${pending.target}\n` + replacement +
    'Use the edit tool on that target file. Replace only that exact occurrence once; do not include surrounding lines, ' +
    'do not add a trailing newline, do not read other files, and do not use write. If the user explicitly changed the ' +
    'directory, file, or task in a later message, follow that later instruction.';
}

export function escalate(text, count) {
  return count >= 3
    ? text + `\nThis exact call has been denied ${count} times. Do not retry it; stop and report the exact target and requested change to the user.`
    : text;
}

function newFeedback(pending, reason) {
  return `Execution check: ${reason} The tool was not run and no arguments were rewritten.\n` +
    `Pending new file: ${pending.target}\n` +
    (pending.content != null ? `Required exact content: ${JSON.stringify(pending.content)} (no trailing newline)\n` : '') +
    'Create that new file with write and that exact content; do not modify the source file. ' +
    'If the user explicitly changed the directory or task in a later message, follow that later instruction.';
}

export function checkCall({ exec, pending, cwd, events = [] }) {
  if (exec.signal?.aborted || !fileTools.has(exec.name)) return null;
  const callPath = normalPath(exec.arguments?.file_path, cwd);
  if (!callPath) return null;
  if (!pending) {
    return exec.name === 'edit' || exec.name === 'write' ? nearMissNote(callPath, events) : null;
  }
  if (pending.multi) return null;
  const target = normalPath(pending.target, cwd);
  const onTarget = target ? samePath(callPath, target) : false;

  if (pending.kind === 'new') {
    if ((exec.name === 'edit' || exec.name === 'write') && target && !onTarget) {
      return newFeedback(pending, `the pending task is to create ${pending.target}, not to modify another file.`);
    }
    if (exec.name === 'write' && target && onTarget && pending.content != null) {
      const content = exec.arguments?.content;
      if (typeof content === 'string' && content !== pending.content) {
        return newFeedback(pending, 'the new file content does not exactly match the requested content; ' +
          'do not add or remove characters, including a trailing newline.');
      }
    }
    return null;
  }
  if (pending.kind === 'read' || pending.kind === 'switch') {
    if (exec.name === 'edit' || exec.name === 'write') {
      return feedback({ ...pending, old: '', new: '' },
        'the latest user request is to read or switch, not to modify files.');
    }
    return null;
  }
  if (pending.kind !== 'edit' || !pending.direct) {
    return exec.name === 'edit' || exec.name === 'write' ? nearMissNote(callPath, events) : null;
  }

  if (exec.name === 'read') {
    if (!target || !pending.source) return null;
    if (onTarget) {
      return feedback(pending, 'the target file has already been read in this session; perform the pending edit now.');
    }
    return feedback(pending, 'this single-file edit already has its target and source content; do not read another file.');
  }

  if (exec.name === 'write') {
    if (target && onTarget) {
      return feedback(pending, 'the target file already exists; a whole-file write would overwrite it.');
    }
    return null;
  }

  if (exec.name !== 'edit') return null;
  if (target && !onTarget) {
    return feedback(pending, 'the edit path is not the verified target of the pending single-file edit.');
  }
  if (!onTarget || !pending.source || pending.source.complete === false) return null;

  const old = exec.arguments?.old_string;
  const neu = exec.arguments?.new_string;
  if (typeof old !== 'string' || typeof neu !== 'string' || !old) return null;
  const source = pending.source.text;
  const count = countOccurrences(source, old);
  const valid = count === 1 && source.replace(old, neu) === source.replace(pending.old, pending.new);
  if (valid) return null;
  if (old !== pending.old && old.includes(pending.old)) {
    return feedback(pending, 'the old_string includes surrounding lines instead of only the requested exact literal.');
  }
  if (old !== pending.old && old.trimEnd() !== old && old.trimEnd() === pending.old) {
    return feedback(pending, 'the old_string adds a trailing newline that the requested exact literal does not have.');
  }
  if (count === 0) {
    const line = source.split(/\r?\n/).find(text => text.includes(pending.old));
    return feedback(pending, 'the old_string does not match the current file exactly.' +
      (line ? ` Current text containing the requested literal: ${JSON.stringify(line)}` : ''));
  }
  if (count > 1) {
    return feedback(pending, 'the old_string matches more than once; it must identify only the requested occurrence.');
  }
  return feedback(pending, 'the edit does not produce exactly the requested single replacement.');
}

export function verifySaved({ exec, pending, result }) {
  if (!pending || exec.signal?.aborted || result?.isError) return null;
  if (pending.kind === 'new' && exec.name === 'write' && pending.content != null &&
      samePath(exec.arguments?.file_path, pending.target)) {
    const after = result.value?.after;
    if (typeof after !== 'string') return null;
    const actual = after.replace(/\r\n?/g, '\n');
    if (actual === pending.content) return null;
    return 'New-file verification: the saved content is ' + JSON.stringify(actual.slice(0, 200)) +
      ' but the requested exact content is ' + JSON.stringify(pending.content) +
      '. Re-write the file with exactly that content; do not add a trailing newline.';
  }
  if (pending.kind !== 'edit' || pending.multi || !pending.direct ||
      !pending.source?.complete || exec.name !== 'edit') return null;
  if (!samePath(exec.arguments?.file_path, pending.target)) return null;
  const after = result.value?.after;
  if (typeof after !== 'string') return null;
  const source = pending.source.text.replace(/\r\n?/g, '\n');
  const expected = source.replace(pending.old, pending.new).replace(/\n+$/, '');
  const actual = after.replace(/\r\n?/g, '\n').replace(/\n+$/, '');
  if (expected === actual) return null;
  const expectedLines = expected.split('\n');
  const actualLines = actual.split('\n');
  let index = 0;
  while (index < expectedLines.length && index < actualLines.length &&
         expectedLines[index] === actualLines[index]) index++;
  return 'Edit verification: the saved file does not match the requested exact replacement. ' +
    `First differing line ${index + 1}: expected ${JSON.stringify((expectedLines[index] ?? '').slice(0, 200))}; ` +
    `actual ${JSON.stringify((actualLines[index] ?? '').slice(0, 200))}. ` +
    'Re-read the target and correct only the requested change; do not broaden the edit.';
}

function logDecision(exec, pending, issue, count) {
  if (process.env.EDIT_GUARD_LOG === '0') return;
  try {
    mkdirSync(logDir, { recursive: true });
    const file = path.join(logDir, 'decisions.jsonl');
    try {
      if (statSync(file).size > logMaxBytes) renameSync(file, file + '.1');
    } catch { /* first write or missing file */ }
    appendFileSync(file, JSON.stringify({
      time: new Date().toISOString(), tool: exec.name, path: exec.arguments?.file_path ?? null,
      pending: pending ? { kind: pending.kind, target: pending.target ?? null,
        direct: !!pending.direct, multi: !!pending.multi } : null,
      denied: !!issue, count, reason: issue ? issue.slice(0, 240) : null,
    }) + '\n');
  } catch { /* diagnostics must never break a tool call */ }
}

export function apply(ctx) {
  const sessions = new WeakMap();
  const stateOf = session => {
    let state = sessions.get(session);
    if (!state) { state = { denies: new Map() }; sessions.set(session, state); }
    return state;
  };
  const signature = exec => [exec.name, normalPath(exec.arguments?.file_path, exec.agent?.session?.header?.cwd) ?? '',
    String(exec.arguments?.old_string ?? '').slice(0, 80),
    String(exec.arguments?.new_string ?? '').slice(0, 80)].join('|');
  const bump = (state, key) => {
    const count = (state.denies.get(key) ?? 0) + 1;
    state.denies.set(key, count);
    if (state.denies.size > 64) state.denies.delete(state.denies.keys().next().value);
    return count;
  };

  ctx.on('tools/pre-execute', async (exec, next) => {
    if (!isLocalAgent(exec.agent) || !fileTools.has(exec.name)) return next();
    const session = exec.agent?.session;
    const events = session?.snapshotEvents?.() ?? [];
    const pending = derivePending(events);
    let issue = pending ? checkCall({ exec, pending, cwd: session?.header?.cwd, events }) : null;
    const decision = await next();
    if (decision.kind !== 'allow' || !issue) return decision;
    const count = bump(stateOf(session), signature(exec));
    issue = escalate(issue, count);
    logDecision(exec, pending, issue, count);
    return { kind: 'deny', reason: issue };
  });

  ctx.on('tools/post-execute', async (exec, result, next) => {
    const decision = await next();
    if (!isLocalAgent(exec.agent) || !fileTools.has(exec.name) || exec.signal?.aborted ||
        decision.kind !== 'accept' || Object.hasOwn(decision, 'content') ||
        Object.hasOwn(decision, 'value')) return decision;
    const session = exec.agent?.session;
    const events = session?.snapshotEvents?.() ?? [];
    const pending = derivePending(events);
    const mismatch = verifySaved({ exec, pending, result });
    if (!mismatch) return decision;
    logDecision(exec, pending, mismatch, 0);
    return { kind: 'block', feedback: [{ type: 'text', text: mismatch }] };
  });
}