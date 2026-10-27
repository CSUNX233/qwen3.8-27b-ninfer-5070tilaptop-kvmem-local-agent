// SPDX-License-Identifier: Apache-2.0
// Multi-action task ledger: when the latest user instruction lists several
// ordered actions, remind the model at turn-stop which actions are still
// outstanding. It never executes or rewrites tool calls.
import path from 'node:path';
import { createUserMessage } from '@deepseek-ai/dsh-llm';
import { isLocalAgent } from './tool-grounded-execution.mjs';

export const name = 'local-task-ledger';
export const inject = ['sessionProjections'];

function textBlocks(content) {
  return (content ?? []).filter(block => block?.type === 'text' && typeof block.text === 'string')
    .map(block => block.text).join('\n');
}

function latestUser(events) {
  for (let i = events.length - 1; i >= 0; i--) {
    const event = events[i];
    if (event?.type !== 'user/message' || event.data?.source?.kind !== 'user') continue;
    const text = textBlocks(event.data.content).slice(0, 8000);
    if (text) return { index: i, text };
  }
  return null;
}

function normalPath(value, cwd) {
  if (typeof value !== 'string' || !value || value.length > 2048 || /[\r\n\0]/.test(value)) return null;
  let text = value.trim();
  if (!/^[A-Za-z]:[\\/]/.test(text)) {
    if (!cwd) return null;
    text = path.win32.resolve(cwd, text);
  }
  return path.win32.normalize(text).toLowerCase();
}

function same(a, b) {
  return typeof a === 'string' && typeof b === 'string' && a === b;
}

export function parseLedger(text) {
  if (typeof text !== 'string') return [];
  const actions = [];
  const move = /(?:把|将)?\s*([A-Za-z]:[\\/][^\s，,。]+)\s*(?:重命名为|重命名成|移动到|rename(?:d)? to|move(?:d)? to)\s*([A-Za-z]:[\\/][^\s，,。]+)/i.exec(text);
  if (move) actions.push({ kind: 'move', source: move[1], destination: move[2] });
  const multi = /(?:把|将)\s*([A-Za-z]:[\\/][^\s，,。]+)\s*(?:和|、|,|，)\s*([A-Za-z]:[\\/][^\s，,。]+)\s*(?:里|中|内的)?\s*(?:的)?\s*([^\s，,。]+)\s*(?:全部)?(?:改成|改为|替换为|replace with)\s*([^\s，,。]+)/i.exec(text);
  if (multi) {
    const oldText = multi[3];
    const newText = multi[4];
    actions.push({ kind: 'edit', target: multi[1], oldText, newText });
    actions.push({ kind: 'edit', target: multi[2], oldText, newText });
  } else {
    const edit = /(?:把|将)\s*([^\s，,。]+)\s*(?:全部)?(?:改成|改为|替换为|replace with)\s*([^\s，,。]+)/i.exec(text);
    if (edit) {
      let target = move ? move[2] : null;
      if (!target) {
        const paths = [...text.matchAll(/[A-Za-z]:[\\/][^\s"'`<>|;,)\]}，。；：、！？（）【】《》“”‘’]+/g)]
          .map(match => match[0].replace(/[.,;:!?]+$/, ''));
        if (paths.length) target = paths[paths.length - 1];
      }
      if (target) actions.push({ kind: 'edit', target, oldText: edit[1], newText: edit[2] });
    }
  }
  const write = /(?:新建|创建|new file|create)\s*([A-Za-z]:[\\/][^\s，,。]+|[^\s，,。]+\.[A-Za-z0-9]+)[，,]\s*内容(?:准确)?(?:写为|为)\s*([^。\s，,]+)/i.exec(text);
  if (write) actions.push({ kind: 'write', target: write[1], content: write[2] });
  return actions;
}

export function completedActions(actions, events, cwd) {
  const user = latestUser(events);
  const slice = user ? events.slice(user.index + 1) : events;
  const calls = new Map();
  const success = new Set();
  for (const event of slice) {
    if (event.type === 'tool/call') calls.set(event.data?.callId, event.data);
    if (event.type !== 'tool/result') continue;
    for (const block of event.data?.message?.content ?? []) {
      if (block.type !== 'tool-result' || block.isError !== false) continue;
      success.add(block.toolCallId);
    }
  }
  const parsed = [];
  for (const id of success) {
    const call = calls.get(id);
    if (!call) continue;
    let args = call.arguments;
    if (typeof args === 'string') {
      try { args = JSON.parse(args); } catch { continue; }
    }
    if (!args || typeof args !== 'object') continue;
    parsed.push({ name: call.name, args });
  }
  return actions.map(action => parsed.some(({ name, args }) => {
    if (action.kind === 'move') {
      const source = normalPath(args.source, cwd);
      const destination = normalPath(args.destination, cwd);
      if (name === 'move') {
        return same(source, normalPath(action.source, cwd)) && same(destination, normalPath(action.destination, cwd));
      }
      if (name === 'pwsh' || name === 'bash') {
        const command = String(args.command ?? '');
        return /(?:Move-Item|Rename-Item|\bmv\b|\bren\b)/i.test(command) &&
          command.includes(action.source) && command.includes(action.destination);
      }
      return false;
    }
    if (action.kind === 'edit') {
      return name === 'edit' && same(normalPath(args.file_path, cwd), normalPath(action.target, cwd)) &&
        args.old_string === action.oldText && args.new_string === action.newText;
    }
    if (action.kind === 'write') {
      return name === 'write' && same(normalPath(args.file_path, cwd), normalPath(action.target, cwd)) &&
        (action.content == null || args.content === action.content);
    }
    return false;
  }));
}

function describe(action) {
  if (action.kind === 'move') return `move ${action.source} -> ${action.destination}`;
  if (action.kind === 'edit') return `edit ${action.target}: ${JSON.stringify(action.oldText)} -> ${JSON.stringify(action.newText)}`;
  return `write ${action.target} with exact content ${JSON.stringify(action.content)}`;
}

export function ledgerReminder(actions, completed) {
  const remaining = actions.filter((_, index) => !completed[index]);
  if (!remaining.length) return null;
  const done = actions.filter((_, index) => completed[index]);
  return `Task ledger: the user's latest instruction lists ${actions.length} actions. ` +
    `Completed: ${done.length ? done.map(describe).join('; ') : 'none'}. ` +
    `Remaining (${remaining.length}):\n` +
    remaining.map((action, index) => `${index + 1}. ${describe(action)}`).join('\n') +
    '\nContinue now and complete every remaining action; do not stop after a partial list or treat a completed action as the whole task. ' +
    'Use the normal tools and wait for each tool result; do not claim an action was done until its result arrives.';
}

export function apply(ctx) {
  const reminded = new WeakMap();
  ctx.on('agent/turn-stopping', ({ agent, turn, signal }) => {
    if (signal?.aborted || !isLocalAgent(agent)) return;
    const session = agent.session;
    const plan = ctx.sessionProjections.stateOf(session, 'plan');
    if (plan?.active || plan?.wanted === true) return;
    const events = session.snapshotEvents?.() ?? [];
    const user = latestUser(events);
    if (!user) return;
    const actions = parseLedger(user.text);
    if (actions.length < 2) return;
    const completed = completedActions(actions, events, session.header?.cwd);
    const reminder = ledgerReminder(actions, completed);
    if (!reminder) return;
    const key = turn ?? 'current';
    if (reminded.get(agent) === key) return;
    reminded.set(agent, key);
    agent.inject(createUserMessage({
      content: [{ type: 'text', text: reminder }],
      source: { kind: 'plugin', plugin: name },
    }));
  });
}