// SPDX-License-Identifier: Apache-2.0
// Static guidance plus an append-only screenshot checkpoint; no stream
// interception, sampling changes, forced tool calls, or extra model requests.
import { createUserMessage } from '@deepseek-ai/dsh-llm';

export const name = 'local-tool-grounded-execution';
export const inject = ['systemPrompt'];
export const sectionName = 'local:tool-grounded-execution';
export const checkpointSource = name + ':browser-checkpoint';

export const policy = `Tool-grounded execution
For implementation and debugging tasks, turn reasoning into useful evidence and incremental progress:
- Once the next useful action is clear, perform it. Inspect relevant files, check an uncertain API or runtime behavior, or test a hypothesis with an available tool, then reason from the result. Resolve discoverable facts by inspection rather than repeatedly guessing them.
- For authorized implementation, put code into the relevant files once the design is sufficiently grounded. Start with the smallest coherent, testable part and continue toward the complete requested deliverable. Progress bookkeeping alone does not verify a hypothesis or implement a feature.
- After each result, update the approach using new evidence. Revisit a settled decision when new evidence or requirements justify it; carry settled decisions forward otherwise.
- Preserve thorough reasoning for difficult algorithms, interacting constraints, architecture decisions, and risks. This guidance imposes no reasoning-length limit and does not justify skipping necessary analysis, verification, or required tests. Verify the full deliverable before reporting success.
- Follow the active plan-mode, sandbox, approval, and user instructions. In plan mode, gather evidence with permitted read-only tools and present the plan through exit_plan_mode; implementation follows the applicable approval rules. Ask for missing user-owned decisions when necessary. For explanations or self-contained reasoning questions, answer directly without artificial tool use.`;

export function isLocalAgent(agent) {
  if (!agent) return false;
  const preset = agent.session?.header?.agentPreset;
  if (preset && preset !== 'ninfer-local') return false;
  return preset === 'ninfer-local' || agent.options?.provider === 'ninfer-local';
}

// Read a bounded slice of existing events; never rewrite or summarize the log.
// A successful write is evidence of a modification, not proof of completion.
export function recentSuccessfulFiles(events) {
  const succeeded = new Set();
  const files = new Set();
  for (let i = events.length - 1, scanned = 0; i >= 0 && scanned < 256; i--, scanned++) {
    const event = events[i];
    if (event.type === 'turn/start') break;
    if (event.type === 'tool/result') {
      for (const block of event.data?.message?.content ?? []) {
        if (block.type === 'tool-result' && block.isError === false) succeeded.add(block.toolCallId);
      }
    } else if (event.type === 'tool/call' && succeeded.has(event.data?.callId) && ['write', 'edit'].includes(event.data.name)) {
      try {
        const args = typeof event.data.arguments === 'string' ? JSON.parse(event.data.arguments) : event.data.arguments;
        if (typeof args?.file_path === 'string') files.add(args.file_path.slice(0, 512));
      } catch { /* An unknown argument shape must not break a successful tool. */ }
      if (files.size === 3) break;
    }
  }
  return [...files];
}

export function screenshotCheckpoint(content, files = []) {
  if (!Array.isArray(content) || content.some(block => block.type === 'image')) return null;
  for (const block of content) {
    if (block.type !== 'text' || typeof block.text !== 'string' || block.text.length > 8192) continue;
    let value;
    try { value = JSON.parse(block.text); } catch { continue; }
    if (typeof value?.path !== 'string' || !value.path || typeof value.url !== 'string' || !value.url) continue;
    // Only a file locator was returned. Images, page titles and page text are
    // not interpreted as trusted instructions or evidence of visual quality.
    return `Browser verification continuation checkpoint\nThe preceding tool saved a screenshot file; its result did not include image pixels. Continue the current task from the latest verified state. A screenshot path is not a new implementation request. The recent successful file modifications below do not by themselves prove completion. If uncertain about progress, inspect the existing artifact or the last relevant tool result before designing or rewriting anything. Rework remains appropriate when a concrete failed check or new user requirement calls for it. Use DOM/runtime/error checks for unresolved verification; read_image requires an image-capable model route. A saved screenshot alone does not establish visual correctness. Complete the remaining checks and deliver when justified, preserving necessary reasoning and the active plan/permission rules.\nCheckpoint data (locators only): ${JSON.stringify({ screenshot: value.path.slice(0, 1024), page: value.url.slice(0, 1024), recentlyModifiedFiles: files })}`;
  }
  return null;
}

export function apply(ctx) {
  ctx.effect(() => ctx.systemPrompt.section({
    name: sectionName,
    order: ctx.systemPrompt.getSectionOrder('PLAN_POLICY') - 50,
    // A web-profile integration must not affect other presets or auxiliary calls.
    // Keep the text identical across tool steps to preserve prompt-prefix reuse.
    text: context => isLocalAgent(context.agent) ? policy : '',
  }), 'local-tool-grounded-execution.section');

  ctx.on('tools/post-execute', async (exec, result, next) => {
    const decision = await next();
    if (exec.name !== 'mcp__browser__browser_screenshot' || !isLocalAgent(exec.agent) ||
        exec.signal?.aborted || result.isError || decision.kind !== 'accept') return decision;
    if ([...(result.additionalContexts ?? []), ...(decision.additionalContexts ?? [])]
      .some(message => message.source?.plugin === checkpointSource)) return decision;
    const plan = ctx.get('sessionProjections')?.stateOf(exec.agent.session, 'plan');
    if (plan?.active || plan?.wanted === true) return decision;
    // Respect a later policy's replacement, including canonical MCP values.
    const content = decision.content ?? (decision.value === undefined ? result.content : decision.value?.content);
    const files = recentSuccessfulFiles(exec.agent.session?.snapshotEvents?.() ?? []);
    const checkpoint = screenshotCheckpoint(content, files);
    if (!checkpoint) return decision;
    return {
      ...decision,
      additionalContexts: [...(decision.additionalContexts ?? []), createUserMessage({
        content: [{ type: 'text', text: checkpoint }],
        source: { kind: 'plugin', plugin: checkpointSource },
      })],
    };
  });
}
