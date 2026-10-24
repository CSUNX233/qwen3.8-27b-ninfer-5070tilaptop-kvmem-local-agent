// SPDX-License-Identifier: Apache-2.0
// Generate configuration in this checkout's isolated DSH_HOME only.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { createRequire } from 'node:module';
import crypto from 'node:crypto';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const dsh = path.join(root, 'integrations/dsh');
const require = createRequire(path.join(dsh, 'package.json'));
const yaml = require('js-yaml');
const home = path.join(root, 'runtime/dsh-home');
const profile = path.join(home, 'profiles/web');
const cfg = JSON.parse(fs.readFileSync(path.join(root, 'config/local.json'), 'utf8'));
function write(file, text) {
  fs.mkdirSync(path.dirname(file), { recursive: true });
  if (!fs.existsSync(file) || fs.readFileSync(file, 'utf8') !== text) fs.writeFileSync(file, text);
}
function document(file, fallback) {
  return fs.existsSync(file) ? yaml.load(fs.readFileSync(file, 'utf8')) ?? fallback : fallback;
}
function moduleURL(relative) {
  const file = path.join(dsh, relative);
  return pathToFileURL(file).href + '?v=' + crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex').slice(0, 12);
}
write(path.join(profile, 'package.json'), JSON.stringify({
  name: 'dsh-profile-ninfer-web', private: true, dependencies: {},
  dsh: { profile: { bundles: ['@deepseek-ai/dsh-base', '@deepseek-ai/dsh-web-app'], patchReload: 'live' } },
}, null, 2) + '\n');
write(path.join(profile, 'cordis.yml'), '[]\n');
const settingsFile = path.join(home, 'settings.yaml');
const settings = document(settingsFile, {});
settings['llm-pi-ai'] ??= {};
settings['llm-pi-ai'].providers ??= {};
settings['llm-pi-ai'].providers['ninfer-local'] = {
  displayName: 'NInfer · Local KVMem', api: 'openai-completions',
  baseURL: `http://127.0.0.1:${cfg.GuardPort}/v1`, apiKeyEnv: 'NINFER_LOCAL_KEY', streamIdleTimeoutMs: 1800000,
  compat: { supportsDeveloperRole: false, supportsReasoningEffort: true,
    maxTokensField: 'max_tokens', requiresReasoningContentOnAssistantMessages: true },
  models: [{ id: cfg.ModelId, name: `${cfg.ModelId} · KVMem${cfg.VisionCPU === true ? ' · CPU Vision' : ''}`,
    contextWindow: cfg.MaxContext, maxTokens: 73728, input: cfg.VisionCPU === true ? ['text', 'image'] : ['text'],
    reasoningEfforts: { low: 'low', medium: 'medium', xhigh: 'xhigh' } }],
};
settings['agent-default-model'] ??= { provider: 'ninfer-local', model: cfg.ModelId, reasoningEffort: cfg.ReasoningEffort };
if (settings['agent-default-model'].provider === 'ninfer-local') {
  settings['agent-default-model'].model = cfg.ModelId;
  settings['agent-default-model'].reasoningEffort = cfg.ReasoningEffort;
}
settings['agent-presets'] ??= { default: 'ninfer-local' };
write(settingsFile, yaml.dump(settings, { lineWidth: 120, noRefs: true }));
const entries = [
  { id: 'web', config: { searchProvider: 'local-free', fetchProvider: 'local-http' } },
  { id: 'web-search-deepseek', disabled: true },
  { id: 'web-fetch-http', disabled: true },
  { insert: [
    { id: 'web-local', name: moduleURL('local-web/web-provider.mjs') },
    { id: 'todo-completion', name: moduleURL('local-agent/todo-completion.mjs') },
    { id: 'tool-grounded-execution', name: moduleURL('local-agent/tool-grounded-execution.mjs'),
      disabled: cfg.ToolGroundedExecution === false },
    { id: 'mcp-browser', name: '@deepseek-ai/dsh-mcp-client', config: {
      serverName: 'browser', transport: 'stdio', command: process.execPath,
      args: [path.join(dsh, 'local-web/browser-server.mjs')], cwd: path.join(dsh, 'local-web'),
      env: { TEMP: path.join(root, 'runtime/temp'), TMP: path.join(root, 'runtime/temp'), PLAYWRIGHT_SKIP_BROWSER_DOWNLOAD: '1' },
      toolCallTimeoutMs: 60000, failOnStartupError: true,
    } },
  ] },
];
// Preserve unrelated user patch entries; replace only entries owned by this installer.
const patchFile = path.join(profile, 'cordis.patch.yml');
const previous = document(patchFile, []);
const owned = new Set(['web', 'web-search-deepseek', 'web-fetch-http', 'web-local', 'todo-completion', 'tool-grounded-execution', 'mcp-browser']);
const keep = previous.flatMap(entry => {
  if (owned.has(entry.id)) return [];
  if (entry.insert) { const insert = entry.insert.filter(row => !owned.has(row.id)); return insert.length ? [{ ...entry, insert }] : []; }
  return [entry];
});
write(patchFile, yaml.dump([...keep, ...entries], { lineWidth: 120, noRefs: true }));
for (const name of ['agent.cordis.yml', 'preset.yml']) {
  const dest = path.join(home, '.agent-presets/ninfer-local', name);
  if (!fs.existsSync(dest)) write(dest, fs.readFileSync(path.join(root, 'templates/dsh-preset', name), 'utf8'));
}
const instructions = path.join(home, 'AGENTS.md');
if (!fs.existsSync(instructions)) write(instructions, [
  '# Local Agent',
  'Use huashu-nuwa only when the user explicitly requests it. Never load it automatically.',
  'Keep todo_write accurate as verified work finishes; do not mark blocked work complete.',
  'Use browser_action reload for navigation, then read the page in a separate call.',
  '',
].join('\n'));
for (const pkg of ['dsh-compaction-basic', 'dsh-compaction-tool-result-pruner']) {
  const folder = path.join(dsh, 'node_modules/@deepseek-ai', pkg);
  const metadata = JSON.parse(fs.readFileSync(path.join(folder, 'package.json'), 'utf8'));
  if (metadata.version !== '0.1.5-rc.3') throw new Error(`Refusing to patch ${pkg} ${metadata.version}; supported: 0.1.5-rc.3`);
  const target = path.join(folder, 'lib/index.js');
  const patched = fs.readFileSync(path.join(dsh, 'patches', pkg + '.js'), 'utf8');
  if (fs.readFileSync(target, 'utf8') !== patched) {
    const backup = target + '.ninfer-original';
    if (!fs.existsSync(backup)) fs.copyFileSync(target, backup);
    fs.writeFileSync(target, patched);
  }
}
console.log('Configured isolated dsh home and version-checked compaction patches.');
