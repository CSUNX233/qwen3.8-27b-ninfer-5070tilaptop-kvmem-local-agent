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
process.env.DSH_HOME = home;
const profile = path.join(home, 'profiles/web');
const cfg = JSON.parse(fs.readFileSync(path.join(root, 'config/local.json'), 'utf8'));

// Apply version-checked DSH & Cordis patches first before importing any DSH modules
const SUPPORTED_PATCH_VERSIONS = {
  'cordis-plugin-loader': '1.0.3',
  'dsh-app-boot': '0.1.5-rc.3',
  'dsh-agent-presets': '0.1.5-rc.3',
  'dsh-typert-loader': '0.1.5-rc.3',
  'dsh-compaction-basic': '0.1.5-rc.3',
  'dsh-compaction-tool-result-pruner': '0.1.5-rc.3',
};
for (const [pkg, expectedVersion] of Object.entries(SUPPORTED_PATCH_VERSIONS)) {
  const folder = path.join(dsh, 'node_modules/@deepseek-ai', pkg);
  const metadata = JSON.parse(fs.readFileSync(path.join(folder, 'package.json'), 'utf8'));
  if (metadata.version !== expectedVersion) {
    throw new Error(`Refusing to patch ${pkg} ${metadata.version}; supported: ${expectedVersion}`);
  }
  const target = path.join(folder, 'lib/index.js');
  const patchSource = path.join(dsh, 'patches', pkg + '.js');
  if (fs.existsSync(patchSource)) {
    const patched = fs.readFileSync(patchSource, 'utf8');
    if (fs.readFileSync(target, 'utf8') !== patched) {
      const backup = target + '.ninfer-original';
      if (!fs.existsSync(backup)) fs.copyFileSync(target, backup);
      fs.writeFileSync(target, patched);
    }
  }
}

// Ensure frontend index.html has layout scope fix & autofocus enhancements
const frontendIndexTarget = path.join(dsh, 'node_modules/@deepseek-ai/dsh-web-frontend/dist/index.html');
const frontendIndexPatch = path.join(dsh, 'patches/dsh-web-frontend-index.html');
if (fs.existsSync(frontendIndexPatch) && fs.existsSync(path.dirname(frontendIndexTarget))) {
  const patchContent = fs.readFileSync(frontendIndexPatch, 'utf8');
  if (!fs.existsSync(frontendIndexTarget) || fs.readFileSync(frontendIndexTarget, 'utf8') !== patchContent) {
    fs.writeFileSync(frontendIndexTarget, patchContent);
  }
}

// Clean up any legacy junctions or fallback symlink directories so readdirp/chokidar
// never encounters Windows NTFS junction realpath (-4094 UNKNOWN) errors.
function removeLegacyFallbackDir(targetPath) {
  try {
    const stat = fs.lstatSync(targetPath);
    if (stat.isSymbolicLink()) {
      try {
        fs.unlinkSync(targetPath);
      } catch {
        fs.rmSync(targetPath, { recursive: true, force: true });
      }
      return;
    }
    if (stat.isDirectory()) {
      fs.rmSync(targetPath, { recursive: true, force: true });
    }
  } catch {}
}

for (const legacyPath of [
  path.join(root, 'runtime/node_modules'),
  path.join(root, 'node_modules'),
  path.join(home, 'node_modules'),
  path.join(home, 'profiles/node_modules'),
  path.join(profile, 'node_modules'),
  path.join(profile, '.dsh-module-fallback'),
]) {
  removeLegacyFallbackDir(legacyPath);
}


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
  { id: 'ui-trajectory', disabled: true },
  { id: 'ui-model-selection', disabled: true },
  { insert: [
    { id: 'web-local', name: moduleURL('local-web/web-provider.mjs') },
    { id: 'todo-completion', name: moduleURL('local-agent/todo-completion.mjs') },
    { id: 'tool-grounded-execution', name: moduleURL('local-agent/tool-grounded-execution.mjs'),
      disabled: cfg.ToolGroundedExecution === false },
    { id: 'edit-guard', name: moduleURL('local-agent/edit-guard.mjs'),
      disabled: cfg.EditGuard === false },
    { id: 'task-ledger', name: moduleURL('local-agent/task-ledger.mjs'),
      disabled: cfg.TaskLedger === false },
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
const owned = new Set(['web', 'web-search-deepseek', 'web-fetch-http', 'ui-trajectory', 'ui-model-selection', 'web-local', 'todo-completion', 'tool-grounded-execution', 'edit-guard', 'task-ledger', 'mcp-browser']);
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
console.log('Configured isolated dsh home and version-checked compaction patches.');
