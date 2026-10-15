// SPDX-License-Identifier: Apache-2.0
// Exercise the real dsh tool/prompt pipeline, without requesting a model.
import assert from 'node:assert/strict';
import path from 'node:path';
import { createRequire } from 'node:module';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { test } from 'node:test';
import * as plugin from '../dsh/local-agent/tool-grounded-execution.mjs';

const require = createRequire(path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../dsh/package.json'));
const { Context, Service } = await import(pathToFileURL(require.resolve('@deepseek-ai/cordis')).href);
const { SystemPrompt, renderPrompt } = await import(pathToFileURL(require.resolve('@deepseek-ai/dsh-system-prompt')).href);
const { ToolRuntime } = await import(pathToFileURL(require.resolve('@deepseek-ai/dsh-tools')).href);
const { createUserMessage } = await import(pathToFileURL(require.resolve('@deepseek-ai/dsh-llm')).href);
const screenshot = { path: 'D:\\example\\capture.png', url: 'file:///D:/example/scene.html' };
const textContent = [{ type: 'text', text: JSON.stringify(screenshot) }];
const events = [
  { type: 'turn/start' },
  { type: 'tool/call', data: { callId: 'write-ok', name: 'write', arguments: JSON.stringify({ file_path: 'D:\\example\\scene.html' }) } },
  { type: 'tool/result', data: { message: { content: [{ type: 'tool-result', toolCallId: 'write-ok', isError: false }] } } },
  { type: 'tool/call', data: { callId: 'edit-failed', name: 'edit', arguments: { file_path: 'D:\\example\\failed.html' } } },
  { type: 'tool/result', data: { message: { content: [{ type: 'tool-result', toolCallId: 'edit-failed', isError: true }] } } },
];

test('checkpoint only records actual successful modifications and valid locators', () => {
  assert.deepEqual(plugin.recentSuccessfulFiles(events), ['D:\\example\\scene.html']);
  assert.deepEqual(plugin.recentSuccessfulFiles([...events, { type: 'turn/start' }]), []);
  for (const content of [undefined, {}, [], [{ type: 'text', text: 'invalid JSON' }], [{ type: 'text', text: '{}' }]]) {
    assert.equal(plugin.screenshotCheckpoint(content), null);
  }
  const context = plugin.screenshotCheckpoint(textContent, plugin.recentSuccessfulFiles(events));
  assert.ok(context.includes('scene.html')); assert.ok(!context.includes('failed.html'));
  assert.ok(context.includes('does not establish visual correctness'));
  assert.equal(plugin.screenshotCheckpoint([...textContent, { type: 'image' }]), null);
});

test('real post-execute pipeline appends context without altering system prompt or screenshot', async t => {
  const ctx = new Context(); let plan = {}; let fail = false;
  class Projections extends Service {
    constructor(context) { super(context, 'sessionProjections'); }
    stateOf(_session, key) { return key === 'plan' ? plan : undefined; }
  }
  await ctx.plugin(SystemPrompt, {}); await ctx.plugin(ToolRuntime, {}); await ctx.plugin(Projections);
  let fork = await ctx.plugin(plugin);
  t.after(() => ctx.fiber.dispose());
  const toolName = 'mcp__browser__browser_screenshot';
  for (const name of [toolName, 'unrelated']) ctx.tools.register({
    name, description: 'Fixture', parameters: { type: 'object', additionalProperties: false, properties: {} },
    output: { schema: { type: 'object', additionalProperties: false, required: ['path', 'url'], properties: { path: { type: 'string' }, url: { type: 'string' } } },
      render: () => structuredClone(textContent) },
    async execute() { if (fail) throw new Error('screenshot fixture failed'); return structuredClone(screenshot); },
  });
  const agent = { options: { provider: 'ninfer-local' }, session: { header: { agentPreset: 'ninfer-local' }, snapshotEvents: () => events } };
  const prompt = () => ctx.systemPrompt.assemble({ agent }).then(renderPrompt);
  const before = await prompt();
  const execute = (name = toolName, caller = agent, signal = new AbortController().signal) => ctx.tools.execute({ callId: 'fixture-' + Math.random(), name, arguments: {}, agent: caller, signal });
  const result = await execute();
  assert.equal(result.isError, false, JSON.stringify(result)); assert.deepEqual(result.value, screenshot); assert.deepEqual(result.content, textContent);
  assert.equal(result.additionalContexts.length, 1); assert.equal(result.additionalContexts[0].source.plugin, plugin.checkpointSource);
  assert.ok(result.additionalContexts[0].content[0].text.includes('scene.html'));
  assert.equal(await prompt(), before); assert.equal(before.split(plugin.policy).length - 1, 1);
  assert.equal((await execute('unrelated')).additionalContexts, undefined);
  const other = { ...agent, session: { ...agent.session, header: { agentPreset: 'other' } } };
  assert.equal((await execute(toolName, other)).additionalContexts, undefined);
  fail = true; const failed = await execute(); assert.equal(failed.isError, true); assert.equal(failed.additionalContexts, undefined); fail = false;
  plan = { active: true }; assert.equal((await execute()).additionalContexts, undefined); plan = {};
  plan = { wanted: true }; assert.equal((await execute()).additionalContexts, undefined); plan = {};
  const abort = new AbortController(); abort.abort(); assert.equal((await execute(toolName, agent, abort.signal)).additionalContexts, undefined);
  assert.equal((await ctx.tools.execute({ callId: 'fixture-aux', name: toolName, arguments: {}, signal: new AbortController().signal })).additionalContexts, undefined);
  let removePolicy = ctx.on('tools/post-execute', async (_exec, _result, next) => ({ ...await next(), content: [{ type: 'text', text: 'replacement without a saved screenshot' }] }));
  assert.equal((await execute()).additionalContexts, undefined); removePolicy();
  removePolicy = ctx.on('tools/post-execute', async () => ({ kind: 'block', feedback: [{ type: 'text', text: 'fixture blocked' }] }));
  assert.equal((await execute()).additionalContexts, undefined); removePolicy();
  const existing = createUserMessage({ content: [{ type: 'text', text: 'existing checkpoint' }], source: { kind: 'plugin', plugin: plugin.checkpointSource } });
  removePolicy = ctx.on('tools/post-execute', async (_exec, _result, next) => ({ ...await next(), additionalContexts: [existing] }));
  assert.deepEqual((await execute()).additionalContexts, [existing]); removePolicy();
  await fork.dispose(); assert.equal((await execute()).additionalContexts, undefined);
  fork = await ctx.plugin(plugin); assert.equal((await execute()).additionalContexts.length, 1); assert.equal(await prompt(), before);
});
