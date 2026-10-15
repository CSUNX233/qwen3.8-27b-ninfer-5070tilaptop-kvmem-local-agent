import { Service } from "@deepseek-ai/cordis";
import z from "@deepseek-ai/schemastery";
import { freezeMessage } from "@deepseek-ai/dsh-llm";
import { deepFreeze } from "@deepseek-ai/dsh-util-values";

import { createHash } from "node:crypto";

// ---------------------------------------------------------------------------
// LOCAL PATCH (2026-10-10): prune tool results AT OCCURRENCE + collapse byte-identical duplicates.
// The provider's prefix cache is only worth anything if an already-sent message is
// never rewritten; pruning on arrival fixes the result before its first send.
// Switches: DSH_PRUNE_AT_OCCURRENCE=0 / DSH_DEDUPE_TOOL_RESULTS=0 (both default ON).
// ---------------------------------------------------------------------------
const PRUNE_AT_OCCURRENCE = (process.env.DSH_PRUNE_AT_OCCURRENCE ?? "1") !== "0";
const DEDUPE_TOOL_RESULTS = (process.env.DSH_DEDUPE_TOOL_RESULTS ?? "1") !== "0";
const DEDUPE_MIN_CHARS = Number(process.env.DSH_DEDUPE_MIN_CHARS ?? 512);
const DEDUPE_PREVIEW_CHARS = Number(process.env.DSH_DEDUPE_PREVIEW_CHARS ?? 400);
/** Concatenate the text of a content block list. */
function patchTextOf(blocks) {
  let out = "";
  for (const block of blocks ?? []) {
    if (block && block.type === "text" && typeof block.text === "string") out += block.text;
  }
  return out;
}
/** sha1 of one tool result's text. */
function patchDigest(text) {
  return createHash("sha1").update(text).digest("hex");
}
/** The pointer that replaces a redundant duplicate result. */
function patchPointerText(earlierCallId, preview) {
  return "[... duplicate tool result collapsed ...]\n" +
    "This call returned exactly the same bytes as earlier call " + earlierCallId +
    "; the earlier result is kept in the conversation. Preview:\n" + preview + "\n";
}

//#region lib/types/config.js
/** Configuration resolution for deterministic tool-result pruning. */
/** Fixed marker substituted for every removed middle span. */
const PRUNE_MARKER = "\n\n[... tool result middle pruned ...]\n\n";
/** Low-friction defaults for coding-agent tool output. */
const DEFAULTS = deepFreeze({
	thresholdChars: 8192,
	headChars: 4096,
	tailChars: 1024
});
const CONFIG_KEYS = new Set([
	"thresholdChars",
	"headChars",
	"tailChars"
]);
/**
* Count Unicode code points without splitting surrogate pairs.
* @param text - text to measure.
* @returns the Unicode code-point count.
*/
function codePointLength(text) {
	return Array.from(text).length;
}
/**
* Resolve and validate pruning budgets.
* @param config - raw plugin configuration.
* @returns a detached deeply immutable configuration.
*/
function resolveConfig(config = {}) {
	for (const key of Object.keys(config)) if (!CONFIG_KEYS.has(key)) throw new Error(`ToolResultPruneConfig: unknown key "${key}" (allowed: thresholdChars, headChars, tailChars)`);
	const resolved = {
		thresholdChars: config.thresholdChars ?? DEFAULTS.thresholdChars,
		headChars: config.headChars ?? DEFAULTS.headChars,
		tailChars: config.tailChars ?? DEFAULTS.tailChars
	};
	assertPositiveInteger("thresholdChars", resolved.thresholdChars);
	assertNonNegativeInteger("headChars", resolved.headChars);
	assertNonNegativeInteger("tailChars", resolved.tailChars);
	const emittedChars = resolved.headChars + codePointLength(PRUNE_MARKER) + resolved.tailChars;
	if (emittedChars > resolved.thresholdChars) throw new Error(`ToolResultPruneConfig: headChars + marker + tailChars (${emittedChars}) must be at most thresholdChars (${resolved.thresholdChars})`);
	return deepFreeze(structuredClone(resolved));
}
function assertPositiveInteger(name, value) {
	if (!Number.isInteger(value) || value <= 0) throw new Error(`ToolResultPruneConfig: ${name} (${value}) must be a positive integer`);
}
function assertNonNegativeInteger(name, value) {
	if (!Number.isInteger(value) || value < 0) throw new Error(`ToolResultPruneConfig: ${name} (${value}) must be a non-negative integer`);
}
//#endregion
//#region lib/types/index.js
/**
* Replay-safe, model-free tool-result pruning service.
*
* @module @deepseek-ai/dsh-compaction-tool-result-pruner
*/
/** Deterministic head/middle/tail pruning for current tool-result surface nodes. */
var ToolResultPruner = class extends Service {
	static inject = ["tokenMeter"];
	static Config = z.object({
		thresholdChars: z.number().step(1).min(1).default(DEFAULTS.thresholdChars),
		headChars: z.number().step(1).min(0).default(DEFAULTS.headChars),
		tailChars: z.number().step(1).min(0).default(DEFAULTS.tailChars)
	});
	/** Resolved and immutable character budgets. */
	config;
	constructor(ctx, config = {}) {
				super(ctx, "toolResultPruner");
		this._patchBusy = false;
		if (PRUNE_AT_OCCURRENCE || DEDUPE_TOOL_RESULTS) {
			ctx.on("session/event", (session, event) => {
				if (event?.type === "tool/result") this._onToolResult(session, event);
			});
		}
		this.config = resolveConfig(config);
	}
	/**
	* Measure text content in Unicode code points; non-text blocks cost zero.
	* @param blocks - tool-result content to measure.
	* @returns total Unicode code points across text blocks.
	*/
	measureContent(blocks) {
		let chars = 0;
		for (const block of blocks) if (block.type === "text") chars += codePointLength(block.text);
		return chars;
	}
	/**
	* Replace an over-budget text middle while retaining rich-block order.
	* Text slicing is by Unicode code point, not UTF-16 code unit, so a retained
	* boundary cannot split a surrogate pair. Grapheme clusters may still split.
	* @param blocks - original tool-result content.
	* @returns pruned content, or `null` when the text is within budget.
	*/
	pruneContent(blocks) {
		const totalChars = this.measureContent(blocks);
		if (totalChars <= this.config.thresholdChars) return null;
		const removedStart = this.config.headChars;
		const removedEnd = totalChars - this.config.tailChars;
		const pruned = [];
		let consumed = 0;
		let markerInserted = false;
		for (const block of blocks) {
			if (block.type !== "text") {
				pruned.push(block);
				continue;
			}
			const points = Array.from(block.text);
			const blockStart = consumed;
			const blockEnd = blockStart + points.length;
			const headEnd = Math.min(points.length, Math.max(0, removedStart - blockStart));
			const tailStart = Math.min(points.length, Math.max(0, removedEnd - blockStart));
			const marker = blockStart < removedEnd && blockEnd > removedStart && !markerInserted ? PRUNE_MARKER : "";
			if (marker.length > 0) markerInserted = true;
			const text = points.slice(0, headEnd).join("") + marker + points.slice(tailStart).join("");
			if (text.length > 0) pruned.push({
				...block,
				text
			});
			consumed = blockEnd;
		}
		/* v8 ignore next -- totalChars > threshold and valid budgets guarantee a removed text span. */
		if (!markerInserted) throw new Error("tool-result prune: failed to locate the removed text span");
		const charsAfter = this.measureContent(pruned);
		/* v8 ignore next -- config validation fixes the emitted head + marker + tail budget. */
		if (charsAfter > this.config.thresholdChars || charsAfter >= totalChars) throw new Error("tool-result prune: replacement must be smaller and within threshold");
		return pruned;
	}
	/**
	* Prune every over-budget tool result from one stable current-surface snapshot.
	* Each replacement preserves the complete event data except for `content`,
	* cites the shadowed node so replay can recover the replacement input, and is
	* immediately preceded by a `compaction/prune` shadow-price event pricing the
	* shadowed node through the injected token meter, so pure consumers can
	* subtract it without per-node state.
	* @param session - session whose current surface is rewritten.
	* @returns landed replacements and aggregate Unicode-code-point savings.
	* @throws when the session rejects a replacement; replacements committed
	* earlier in the pass remain durable.
	*/
		/** LOCAL PATCH (2026-10-10): replace one tool/result in place with shorter content. */
	_patchReplace(session, seq, content) {
		const event = session.eventAt(seq);
		const original = session.deriveEventMessage(event);
		const charsBefore = this.measureContent(original.content);
		const charsAfter = this.measureContent(content);
		const message = freezeMessage({
			...original,
			content
		});
		session.append("compaction/prune", {
			shadowedRange: {
				start: seq,
				end: seq
			},
			shadowedSeqs: [seq],
			shadowedTokenCount: this.ctx.tokenMeter.estimateMessage(original)
		});
		session.append("tool/result", {
			...event.data,
			message
		}, {
			surfaceOp: {
				op: "replace",
				startSeq: seq,
				endSeq: seq
			},
			sourceEventSeqs: [seq]
		});
		return {
			charsBefore,
			charsAfter
		};
	}
	/** LOCAL PATCH (2026-10-10): occurrence-time hook. Never throws into the agent loop. */
	_onToolResult(session, event) {
		if (this._patchBusy) return;
		this._patchBusy = true;
		try {
			let did = null;
			if (DEDUPE_TOOL_RESULTS) did = this._patchDedupe(session, event.seq);
			if (did === null && PRUNE_AT_OCCURRENCE) did = this._patchPruneOne(session, event.seq);
			if (did !== null) try {
				this.ctx.logger?.info?.(("tool-prune[local]: seq=" + event.seq + " " + did.charsBefore + "->" + did.charsAfter + " chars (" + did.kind + ")"));
			} catch {}
		} catch (error) {
			try {
				this.ctx.logger?.warn?.(("tool-prune[local] skipped: " + (error?.message ?? error)));
			} catch {}
		} finally {
			this._patchBusy = false;
		}
	}
	/** LOCAL PATCH (2026-10-10): prune this one result if it is over budget. */
	_patchPruneOne(session, seq) {
		const event = session.eventAt(seq);
		if (event?.type !== "tool/result") return null;
		const original = session.deriveEventMessage(event);
		if (original === null) return null;
		const content = this.pruneContent(original.content);
		if (content === null) return null;
		return {
			...this._patchReplace(session, seq, content),
			kind: "prune"
		};
	}
	/** LOCAL PATCH (2026-10-10): collapse this result into a pointer when an earlier one is byte-identical. */
	_patchDedupe(session, seq) {
		const event = session.eventAt(seq);
		if (event?.type !== "tool/result") return null;
		const original = session.deriveEventMessage(event);
		if (original === null) return null;
		const text = patchTextOf(original.content);
		if (text.length < DEDUPE_MIN_CHARS) return null;
		const nodes = [...session.surface.nodes];
		const selfIdx = nodes.indexOf(seq);
		if (selfIdx <= 0) return null;
		const digest = patchDigest(text);
		for (let i = 0; i < selfIdx; i += 1) {
			const earlier = session.eventAt(nodes[i]);
			if (earlier?.type !== "tool/result") continue;
			const earlierMessage = session.deriveEventMessage(earlier);
			if (earlierMessage === null) continue;
			const earlierText = patchTextOf(earlierMessage.content);
			if (earlierText.length !== text.length) continue;
			if (patchDigest(earlierText) !== digest) continue;
			const callId = earlier.data?.message?.source?.callId ?? "(earlier call)";
			return {
				...this._patchReplace(session, seq, [{
					type: "text",
					text: patchPointerText(callId, text.slice(0, DEDUPE_PREVIEW_CHARS))
				}]),
				kind: "dedupe:" + callId
			};
		}
		return null;
	}

	pruneSession(session) {
		const candidates = [];
		for (const seq of [...session.surface.nodes]) {
			const event = session.eventAt(seq);
			/* v8 ignore next -- surface seqs are validated contiguous log references. */
			if (event?.type === "tool/result") candidates.push({
				seq,
				event
			});
		}
		const pruned = [];
		let charsRemoved = 0;
		for (const { seq, event } of candidates) {
			const result = event.data.message.content[0];
			const content = this.pruneContent(result.content);
			if (content === null) continue;
			const charsBefore = this.measureContent(result.content);
			const charsAfter = this.measureContent(content);
			const message = freezeMessage({
				...event.data.message,
				content: [{
					...result,
					content
				}]
			});
			session.append("compaction/prune", {
				shadowedRange: {
					start: seq,
					end: seq
				},
				shadowedSeqs: [seq],
				shadowedTokenCount: this.ctx.tokenMeter.estimateMessage(event.data.message)
			});
			const replacement = session.append("tool/result", {
				...event.data,
				message
			}, {
				surfaceOp: {
					op: "replace",
					startSeq: seq,
					endSeq: seq
				},
				sourceEventSeqs: [seq]
			});
			pruned.push({
				originalSeq: seq,
				replacementSeq: replacement.seq,
				callId: event.data.message.source.callId,
				charsBefore,
				charsAfter
			});
			charsRemoved += charsBefore - charsAfter;
		}
		return {
			pruned,
			charsRemoved
		};
	}
};
//#endregion
export { DEFAULTS, PRUNE_MARKER, ToolResultPruner, ToolResultPruner as default, codePointLength, resolveConfig };
