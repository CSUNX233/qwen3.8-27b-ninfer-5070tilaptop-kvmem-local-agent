// SPDX-License-Identifier: Apache-2.0
// Static guidance through dsh's prompt registry; no stream interception,
// sampling changes, forced tool calls, or extra model requests.
export const name = 'local-tool-grounded-execution';
export const inject = ['systemPrompt'];
export const sectionName = 'local:tool-grounded-execution';

export const policy = `Tool-grounded execution
For implementation and debugging tasks, turn reasoning into useful evidence and incremental progress:
- Once the next useful action is clear, perform it. Inspect relevant files, check an uncertain API or runtime behavior, or test a hypothesis with an available tool, then reason from the result. Resolve discoverable facts by inspection rather than repeatedly guessing them.
- For authorized implementation, put code into the relevant files once the design is sufficiently grounded. Start with the smallest coherent, testable part and continue toward the complete requested deliverable. Progress bookkeeping alone does not verify a hypothesis or implement a feature.
- After each result, update the approach using new evidence. Revisit a settled decision when new evidence or requirements justify it; carry settled decisions forward otherwise.
- Preserve thorough reasoning for difficult algorithms, interacting constraints, architecture decisions, and risks. This guidance imposes no reasoning-length limit and does not justify skipping necessary analysis, verification, or required tests. Verify the full deliverable before reporting success.
- Follow the active plan-mode, sandbox, approval, and user instructions. In plan mode, gather evidence with permitted read-only tools and present the plan through exit_plan_mode; implementation follows the applicable approval rules. Ask for missing user-owned decisions when necessary. For explanations or self-contained reasoning questions, answer directly without artificial tool use.`;

export function apply(ctx) {
  ctx.effect(() => ctx.systemPrompt.section({
    name: sectionName,
    order: ctx.systemPrompt.getSectionOrder('PLAN_POLICY') - 50,
    // A web-profile integration must not affect other presets or auxiliary calls.
    // Keep the text identical across tool steps to preserve prompt-prefix reuse.
    text: context => {
      const agent = context.agent;
      if (!agent) return '';
      const preset = agent.session?.header?.agentPreset;
      if (preset && preset !== 'ninfer-local') return '';
      // The headless entry point supplies a model route without a preset header.
      return preset === 'ninfer-local' || agent.options?.provider === 'ninfer-local' ? policy : '';
    },
  }), 'local-tool-grounded-execution.section');
}
