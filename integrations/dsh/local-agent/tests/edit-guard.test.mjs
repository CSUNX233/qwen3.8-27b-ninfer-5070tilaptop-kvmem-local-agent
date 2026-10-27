import assert from 'node:assert/strict';
import { mkdtemp, mkdir, writeFile, readFile, rm } from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { test } from 'node:test';
import { Context } from '@deepseek-ai/cordis';
import { SystemPrompt } from '@deepseek-ai/dsh-system-prompt';
import { ToolRuntime } from '@deepseek-ai/dsh-tools';
import LocalFileSystem from '@deepseek-ai/dsh-fs-local';
import * as observations from '@deepseek-ai/dsh-fs-observation-policy';
import * as fsTools from '@deepseek-ai/dsh-tool-fs';
import * as editGuard from '../edit-guard.mjs';

process.env.EDIT_GUARD_LOG = '0';

const GAME = 'const state = { score: 0, lives: 3 };\nconst RESET_KEY = "r";\nfunction resetGame() { state.score = 0; state.lives = 3; }\n';
const WHOLE = 'const state = { score: 0, lives: 3 };\nconst RESET_KEY = "r";\nfunction resetGame() { state.score = 0; state.lives = 3; }';
const WHOLE_NEW = 'const state = { score: 0, lives: 5 };\nconst RESET_KEY = "r";\nfunction resetGame() { state.score = 0; state.lives = 5; }';
const textOf = result => result.content.filter(b => b.type === 'text').map(b => b.text).join('\n');

function readEnvelope(file, source) {
  const lines = source.replace(/\n$/, '').split('\n').map((line, i) => `${i + 1}: ${line}`).join('\n');
  return `<path>${file}</path>\n<type>file</type>\n<content>\n${lines}\n\n(End of file - total ${source.replace(/\n$/, '').split('\n').length} lines)\n</content>`;
}

function eventsFor(file, instruction, source = GAME) {
  return [
    { type: 'user/message', data: { source: { kind: 'user' }, content: [{ type: 'text', text: instruction }] } },
    { type: 'tool/call', data: { callId: 'read-1', name: 'read', arguments: { file_path: file } } },
    { type: 'tool/result', data: { message: { content: [{ type: 'tool-result', toolCallId: 'read-1', isError: false,
      content: [{ type: 'text', text: readEnvelope(file, source) }] }] } } },
  ];
}

test('pending single-file edit is derived from the latest user literal and prior read', () => {
  const file = 'D:\\example\\yoru-corner\\game.js';
  const events = eventsFor(file, "只修改 D:/example/yoru-corner/game.js 这一个文件：把唯一的 'lives: 3' 精确替换为 'lives: 5'。不搜索或修改其他文件。");
  const pending = editGuard.derivePending(events);
  assert.equal(pending.kind, 'edit');
  assert.equal(pending.target, file);
  assert.equal(pending.old, 'lives: 3');
  assert.equal(pending.new, 'lives: 5');
  assert.equal(pending.direct, true);
  assert.ok(pending.source.text.includes('lives: 3'));
});

test('read other file, read target again, write target and broad edit are denied', () => {
  const file = 'D:\\example\\yoru-corner\\game.js';
  const pending = editGuard.derivePending(eventsFor(file, "只修改 D:/example/yoru-corner/game.js 这一个文件：把唯一的 'lives: 3' 精确替换为 'lives: 5'。不搜索或修改其他文件。"));
  const cwd = 'D:\\example';
  const base = { kind: 'edit', pending, cwd };
  assert.match(editGuard.checkCall({ ...base, exec: { name: 'read', arguments: { file_path: 'D:/example/yoru-corner/README.md' } } }), /do not read another file/);
  assert.match(editGuard.checkCall({ ...base, exec: { name: 'read', arguments: { file_path: file } } }), /already been read/);
  assert.match(editGuard.checkCall({ ...base, exec: { name: 'write', arguments: { file_path: file, content: GAME } } }), /whole-file write/);
  assert.match(editGuard.checkCall({ ...base, exec: { name: 'edit', arguments: { file_path: file, old_string: WHOLE, new_string: WHOLE_NEW } } }), /surrounding lines/);
  assert.match(editGuard.checkCall({ ...base, exec: { name: 'edit', arguments: { file_path: file, old_string: 'lives: 3\n', new_string: 'lives: 5' } } }), /trailing newline|does not match/);
  assert.equal(editGuard.checkCall({ ...base, exec: { name: 'edit', arguments: { file_path: file, old_string: 'lives: 3', new_string: 'lives: 5' } } }), null);
});

test('non-direct multi-file instruction and explicit switch/new-file tasks do not trigger the edit checks', () => {
  const file = 'D:\\example\\yoru-corner\\game.js';
  const loose = editGuard.derivePending(eventsFor(file, "把 'lives: 3' 改成 'lives: 5'，并检查另一个文件。"));
  assert.equal(loose.direct, false);
  assert.equal(editGuard.checkCall({ pending: loose, cwd: 'D:\\example',
    exec: { name: 'edit', arguments: { file_path: file, old_string: WHOLE, new_string: WHOLE_NEW } } }), null);
  const created = editGuard.derivePending([
    { type: 'user/message', data: { source: { kind: 'user' }, content: [{ type: 'text', text: '请新建 D:/39572/harbor/help.txt，内容准确为 RESET=R。这是新文件。' }] } },
  ]);
  assert.equal(created.kind, 'new');
  assert.equal(editGuard.checkCall({ pending: created, cwd: 'D:\\example', exec: { name: 'write', arguments: { file_path: 'D:/39572/harbor/help.txt', content: 'RESET=R' } } }), null);
  const switched = editGuard.derivePending([
    { type: 'user/message', data: { source: { kind: 'user' }, content: [{ type: 'text', text: '刚才那个目录先不动。现在明确切换到 D:/39572/harbor/。请先读取新目录下的这个文件。' }] } },
  ]);
  assert.equal(switched.kind, 'switch');
  assert.equal(editGuard.checkCall({ pending: switched, cwd: 'D:\\example', exec: { name: 'read', arguments: { file_path: 'D:/39572/harbor/help.txt' } } }), null);
});

test('multi-file instructions step aside instead of enforcing one target', () => {
  const pending = editGuard.derivePending([
    { type: 'user/message', data: { source: { kind: 'user' }, content: [{ type: 'text',
      text: "只改这两个文件：D:/p/a.js 的 'x' 改为 'y'，D:/p/b.js 的 'm' 改为 'n'。" }] } },
  ]);
  assert.equal(pending.multi, true);
  assert.equal(editGuard.checkCall({ pending, cwd: 'D:\\example',
    exec: { name: 'edit', arguments: { file_path: 'D:/p/a.js', old_string: 'x', new_string: 'y' } } }), null);
});

test('near-miss known path is caught before a write, while a genuinely new path is allowed', () => {
  const file = 'D:\\example\\yoru-corner\\game.js';
  const events = eventsFor(file, "只修改 D:/example/yoru-corner/game.js 这一个文件：把唯一的 'lives: 3' 精确替换为 'lives: 5'。");
  const near = editGuard.checkCall({ pending: null, cwd: 'D:\\example', events,
    exec: { name: 'write', arguments: { file_path: 'D:/example/yori-corner/game.js', content: 'x' } } });
  assert.match(near, /near-miss/);
  const fresh = editGuard.checkCall({ pending: null, cwd: 'D:\\example', events,
    exec: { name: 'write', arguments: { file_path: 'D:/example/new-project/game.js', content: 'x' } } });
  assert.equal(fresh, null);
});

test('post-edit verification accepts the exact saved result and flags a mismatch', () => {
  const file = 'D:\\example\\yoru-corner\\game.js';
  const pending = editGuard.derivePending(eventsFor(file,
    "只修改 D:/example/yoru-corner/game.js 这一个文件：把唯一的 'lives: 3' 精确替换为 'lives: 5'。"));
  const exec = { name: 'edit', arguments: { file_path: file } };
  assert.equal(editGuard.verifySaved({ exec, pending,
    result: { isError: false, value: { after: GAME.replace('lives: 3', 'lives: 5') } } }), null);
  assert.match(editGuard.verifySaved({ exec, pending,
    result: { isError: false, value: { after: GAME } } }), /does not match/);
});

test('repeated identical denials escalate instead of looping silently', () => {
  assert.equal(editGuard.escalate('x', 2), 'x');
  assert.match(editGuard.escalate('x', 3), /Do not retry/);
});

test('new-file content is checked exactly, including a trailing newline', () => {
  const target = 'D:/39572/harbor/help.txt';
  const pending = editGuard.derivePending([
    { type: 'user/message', data: { source: { kind: 'user' }, content: [{ type: 'text',
      text: '请新建 D:/39572/harbor/help.txt，文件内容准确写为 RESET=R。这是新文件。' }] } },
  ]);
  assert.equal(pending.content, 'RESET=R');
  const base = { pending, cwd: 'D:\\example' };
  assert.equal(editGuard.checkCall({ ...base, exec: { name: 'write', arguments: { file_path: target, content: 'RESET=R' } } }), null);
  assert.match(editGuard.checkCall({ ...base, exec: { name: 'write', arguments: { file_path: target, content: 'RESET=R\n' } } }), /exact content|trailing newline/);
  assert.match(editGuard.verifySaved({ exec: { name: 'write', arguments: { file_path: target } }, pending,
    result: { isError: false, value: { after: 'RESET=R\n' } } }), /New-file verification/);
  assert.equal(editGuard.verifySaved({ exec: { name: 'write', arguments: { file_path: target } }, pending,
    result: { isError: false, value: { after: 'RESET=R' } } }), null);
});

async function fixture(t) {
  const dir = await mkdtemp(path.join(os.tmpdir(), 'edit-guard-test-'));
  const file = path.join(dir, 'game.js');
  await writeFile(file, GAME);
  const events = [];
  const ctx = new Context();
  t.after(async () => {
    await ctx.fiber.dispose();
    await rm(path.resolve(dir), { recursive: true, force: true });
  });
  await ctx.plugin(SystemPrompt, {});
  await ctx.plugin(ToolRuntime, {});
  await ctx.plugin(LocalFileSystem, { cwd: dir });
  await ctx.plugin(observations);
  await ctx.plugin(fsTools, {});
  await ctx.plugin(editGuard);
  const agent = { options: { provider: 'ninfer-local' },
    session: { header: { agentPreset: 'ninfer-local', cwd: dir }, snapshotEvents: () => events } };
  let calls = 0;
  const run = (name, args) => ctx.tools.execute({ callId: `edit-guard-${++calls}`, name, arguments: args, agent,
    signal: new AbortController().signal });
  return { ctx, dir, file, events, agent, run };
}

test('runtime: a broad edit is denied before the file changes, then the exact edit succeeds', async t => {
  const f = await fixture(t);
  f.events.push({ type: 'user/message', data: { source: { kind: 'user' }, content: [{ type: 'text',
    text: `只修改 ${f.file} 这一个文件：把唯一的 'lives: 3' 精确替换为 'lives: 5'。不搜索或修改其他文件。` }] } });
  const read = await f.run('read', { file_path: f.file });
  assert.equal(read.isError, false);
  f.events.push({ type: 'tool/call', data: { callId: 'fixture-read', name: 'read', arguments: { file_path: f.file } } });
  f.events.push({ type: 'tool/result', data: { message: { content: [{ type: 'tool-result', toolCallId: 'fixture-read',
    isError: false, content: read.content }] } } });
  const denied = await f.run('edit', { file_path: f.file, old_string: WHOLE, new_string: WHOLE_NEW });
  assert.equal(denied.isError, true);
  assert.match(textOf(denied), /Execution check/);
  assert.match(textOf(denied), /surrounding lines/);
  assert.equal(await readFile(f.file, 'utf8'), GAME);
  const accepted = await f.run('edit', { file_path: f.file, old_string: 'lives: 3', new_string: 'lives: 5' });
  assert.equal(accepted.isError, false);
  assert.equal(await readFile(f.file, 'utf8'), GAME.replace('lives: 3', 'lives: 5'));
});

test('runtime: a new-file request allows write to the new path', async t => {
  const f = await fixture(t);
  const created = path.join(f.dir, 'help.txt');
  f.events.push({ type: 'user/message', data: { source: { kind: 'user' }, content: [{ type: 'text',
    text: `先不要修改 ${f.file}。请新建 ${created}，文件内容准确写为 RESET=R。这是新文件。` }] } });
  const result = await f.run('write', { file_path: created, content: 'RESET=R' });
  assert.equal(result.isError, false);
  assert.equal(await readFile(created, 'utf8'), 'RESET=R');
});