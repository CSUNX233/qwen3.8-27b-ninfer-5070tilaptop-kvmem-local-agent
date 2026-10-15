import { Server } from '@modelcontextprotocol/sdk/server/index.js';
import { StdioServerTransport } from '@modelcontextprotocol/sdk/server/stdio.js';
import { CallToolRequestSchema, ListToolsRequestSchema } from '@modelcontextprotocol/sdk/types.js';
import { BrowserRuntime } from './browser-runtime.mjs';

const runtime = new BrowserRuntime();
const object = (properties = {}, required = []) => ({ type: 'object', properties, required, additionalProperties: false });
const string = description => ({ type: 'string', description });
export const browserTools = [
  { name: 'browser_open', description: 'Open an HTTP(S) or file:/// URL in a real Edge browser and return visible text, clickable element refs, console errors and GPU renderer. Runs JavaScript; supports local development pages. Starts Edge only when needed.',
    inputSchema: object({ url: string('Full URL; for a local file use file:///D:/folder/page.html.') }, ['url']) },
  { name: 'browser_snapshot', description: 'Read the current browser page and refresh element refs. Use the latest refs for clicks and form fields. For more page text, increase offset; does not navigate.',
    inputSchema: object({ offset: { type: 'integer', minimum: 0 }, maxChars: { type: 'integer', minimum: 1, maximum: 20000 } }) },
  { name: 'browser_action', description: 'Interact with the current page, then return a fresh snapshot. click/fill/press/select require a ref from the latest snapshot. text is field content, a key such as Enter, an option value, or scroll pixels. back/scroll/reload need no ref. Use reload to refresh, then inspect the new page in a separate call.',
    inputSchema: object({ action: { type: 'string', enum: ['click', 'fill', 'press', 'select', 'scroll', 'back', 'reload'] },
      ref: string('Current element ref, such as e3-1.'), text: string('Text, key, option value, or scroll pixel count.') }, ['action']) },
  { name: 'browser_evaluate', description: 'Evaluate a JavaScript expression in the current browser page and return JSON. Useful for DOM, WebGL and application debugging. Use element refs for normal clicks and form filling. Navigation/reload destroys the running script: use browser_open or browser_action(reload/back), then a separate evaluate to inspect the new page. Scripts are never automatically retried.',
    inputSchema: object({ expression: string('JavaScript expression; an async IIFE may be used.') }, ['expression']) },
  { name: 'browser_screenshot', description: 'Save a viewport screenshot to a local PNG and return its path. This text-only model should use browser_snapshot and browser_evaluate to inspect the page.', inputSchema: object() },
  { name: 'browser_status', description: 'Report browser state, GPU renderer and console errors without starting Edge. Set close=true to immediately release browser memory. It also closes automatically after 3 idle minutes.',
    inputSchema: object({ close: { type: 'boolean' } }) },
];

const server = new Server({ name: 'local-edge-browser', version: '1.0.0' }, { capabilities: { tools: {} } });
server.setRequestHandler(ListToolsRequestSchema, () => ({ tools: browserTools }));
let pending = Promise.resolve();
server.setRequestHandler(CallToolRequestSchema, request => {
  const run = pending.then(async () => {
    const args = request.params.arguments ?? {};
    try {
      let value;
      switch (request.params.name) {
        case 'browser_open': value = await runtime.open(args.url); break;
        case 'browser_snapshot': value = await runtime.snapshot(args.offset, args.maxChars); break;
        case 'browser_action': value = await runtime.action(args); break;
        case 'browser_evaluate': value = await runtime.evaluate(args.expression); break;
        case 'browser_screenshot': value = await runtime.screenshot(); break;
        case 'browser_status': value = args.close ? await runtime.close() : runtime.status(); break;
        default: throw new Error('Unknown browser tool.');
      }
      return { content: [{ type: 'text', text: JSON.stringify(value) }] };
    } catch (error) {
      return { isError: true, content: [{ type: 'text', text: String(error) }] };
    } finally { if (runtime.context) runtime.touch(); }
  });
  pending = run.catch(() => {});
  return run;
});

let closing = false;
async function shutdown() {
  if (closing) return;
  closing = true;
  await runtime.close().catch(() => {});
  await server.close().catch(() => {});
  process.exit(0);
}
process.on('SIGINT', shutdown);
process.on('SIGTERM', shutdown);
process.stdin.on('end', shutdown);
await server.connect(new StdioServerTransport());
