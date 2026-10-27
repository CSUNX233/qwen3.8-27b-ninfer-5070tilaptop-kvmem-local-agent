import assert from 'node:assert/strict';
import { test } from 'node:test';
import * as ledger from '../task-ledger.mjs';

const pureTwoFile = '把 D:/p/src/settings.js 和 D:/p/src/index.js 里的 volume 全部改成 sound，两个文件都要改。';
const complex = '先把 D:/p/src/retry_cli.py 重命名为 D:/p/src/retry_tool.py，然后把 default=2 改成 default=4，再新建 D:/p/NOTES.md，内容准确为 migrated。只做这三件事。';

test('two-file instruction becomes two edit actions', () => {
  const actions = ledger.parseLedger(pureTwoFile);
  assert.equal(actions.length, 2);
  assert.deepEqual(actions[0], { kind: 'edit', target: 'D:/p/src/settings.js', oldText: 'volume', newText: 'sound' });
  assert.deepEqual(actions[1], { kind: 'edit', target: 'D:/p/src/index.js', oldText: 'volume', newText: 'sound' });
});

test('move + edit + write instruction becomes three ordered actions', () => {
  const actions = ledger.parseLedger(complex);
  assert.equal(actions.length, 3);
  assert.equal(actions[0].kind, 'move');
  assert.equal(actions[1].kind, 'edit');
  assert.equal(actions[1].target, 'D:/p/src/retry_tool.py');
  assert.equal(actions[1].oldText, 'default=2');
  assert.equal(actions[1].newText, 'default=4');
  assert.equal(actions[2].kind, 'write');
  assert.equal(actions[2].content, 'migrated');
});

function event(name, args, callId) {
  return [
    { type: 'tool/call', data: { callId, name, arguments: args } },
    { type: 'tool/result', data: { message: { content: [{ type: 'tool-result', toolCallId: callId,
      isError: false, content: [{ type: 'text', text: 'ok' }] }] } } },
  ];
}

test('completed actions are matched from successful tool results', () => {
  const actions = ledger.parseLedger(complex);
  const base = [{ type: 'user/message', data: { source: { kind: 'user' },
    content: [{ type: 'text', text: complex }] } }];
  const two = [...base,
    ...event('move', { source: 'D:/p/src/retry_cli.py', destination: 'D:/p/src/retry_tool.py' }, 'm1'),
    ...event('edit', { file_path: 'D:/p/src/retry_tool.py', old_string: 'default=2', new_string: 'default=4' }, 'e1')];
  assert.deepEqual(ledger.completedActions(actions, two, 'D:/p'), [true, true, false]);
  const reminder = ledger.ledgerReminder(actions, [true, true, false]);
  assert.match(reminder, /Remaining \(1\)/);
  assert.match(reminder, /NOTES\.md/);
  assert.match(reminder, /migrated/);
  const three = [...two,
    ...event('write', { file_path: 'D:/p/NOTES.md', content: 'migrated' }, 'w1')];
  assert.deepEqual(ledger.completedActions(actions, three, 'D:/p'), [true, true, true]);
  assert.equal(ledger.ledgerReminder(actions, [true, true, true]), null);
});

test('failed results and unrelated edits do not complete an action', () => {
  const actions = ledger.parseLedger(complex);
  const base = [{ type: 'user/message', data: { source: { kind: 'user' },
    content: [{ type: 'text', text: complex }] } }];
  const failed = [
    { type: 'tool/call', data: { callId: 'm1', name: 'move', arguments: {
      source: 'D:/p/src/retry_cli.py', destination: 'D:/p/src/retry_tool.py' } } },
    { type: 'tool/result', data: { message: { content: [{ type: 'tool-result', toolCallId: 'm1',
      isError: true, content: [] }] } } },
  ];
  assert.deepEqual(ledger.completedActions(actions, [...base, ...failed], 'D:/p'), [false, false, false]);
});