// KVMem retrieval probe: does a mid-document needle survive when it is OUTSIDE the ring's
// Device-resident window and can only come back through retrieval?
//
//   node kvmem_retrieve_needle.mjs <port> <fillerKB> <needlePercent> [maxTokens]
//
// The document is one long user message:
//   PREFIX NOTE (needle ALPHA-... near the very start, always inside the attention sink)
//   filler, with the MIDDLE NOTE (needle BETA-...) inserted at <needlePercent> of the filler
//   a short question that names both needles
//
// The question is at the end, so the scorer's query (the tail of the last prefill chunk) contains
// both needle strings -- the needle PAGE is the strong content match. If the insert point sits
// further back than the resident window, the only way BETA can be answered is retrieval.
//
// Verdict regexes use a distinctive 4-digit suffix so filler prose can never fake a hit.
import http from 'node:http';

const port = Number(process.argv[2] ?? 18211);
const fillerKB = Number(process.argv[3] ?? 160);
const needlePercent = Number(process.argv[4] ?? 50);
const maxTokens = Number(process.argv[5] ?? 128);

const ALPHA = 'ALPHA-7731';
const BETA = 'BETA-4412';

const PARA =
  'The retrieval subsystem stores key value pages in a bounded device pool and demotes cold ' +
  'pages to a pinned host arena, where a page can be brought back only when the resident mask ' +
  'admits it. Each page holds sixty four tokens of quantised attention state. ';

const prefixPart = `PREFIX NOTE. The prefix secret code is ${ALPHA}. It is stated once, here.\n`;
const middlePart = `\nMIDDLE NOTE. The middle secret code is ${BETA}. It is stated once, here.\n`;

const targetChars = fillerKB * 1024;
const filler = PARA.repeat(Math.ceil(targetChars / PARA.length)).slice(0, targetChars);
const cut = Math.floor((filler.length * needlePercent) / 100);
const body =
  prefixPart + filler.slice(0, cut) + middlePart + filler.slice(cut) +
  `\nQuestion: report the two secret codes from the context above.\n` +
  `Answer with exactly two lines and nothing else:\n` +
  `PREFIX=<code or MISSING>\nMIDDLE=<code or MISSING>\n`;

const payload = JSON.stringify({
  model: 'qwen3.8-27b-gsq-rco-uncensored',
  messages: [{ role: 'user', content: body }],
  temperature: 0,
  top_p: 1,
  max_tokens: maxTokens,
  stream: false,
});

const started = Date.now();
const request = http.request(
  { host: '127.0.0.1', port, path: '/v1/chat/completions', method: 'POST',
    headers: { 'content-type': 'application/json', 'content-length': Buffer.byteLength(payload) } },
  (response) => {
    let text = '';
    response.on('data', (chunk) => { text += chunk; });
    response.on('end', () => {
      const seconds = ((Date.now() - started) / 1000).toFixed(1);
      console.log(`HTTP ${response.statusCode} after ${seconds}s, prompt_chars=${body.length}, ` +
                  `fillerKB=${fillerKB}, needlePercent=${needlePercent}`);
      let answer = text;
      try {
        const parsed = JSON.parse(text);
        answer = parsed?.choices?.[0]?.message?.content ?? JSON.stringify(parsed).slice(0, 4000);
        console.log('usage=' + JSON.stringify(parsed?.usage ?? {}));
        console.log('finish=' + (parsed?.choices?.[0]?.finish_reason ?? '?'));
      } catch { /* raw body below */ }
      console.log('----ANSWER----');
      console.log(answer);
      console.log('----VERDICT----');
      console.log('alpha_present=' + /ALPHA-?7731/i.test(answer) +
                  ' beta_present=' + /BETA-?4412/i.test(answer) +
                  ' alpha_missing=' + /PREFIX\s*=\s*MISSING/i.test(answer) +
                  ' beta_missing=' + /MIDDLE\s*=\s*MISSING/i.test(answer));
    });
  });
request.on('error', (error) => { console.log('REQUEST FAILED: ' + error.message); process.exitCode = 1; });
request.write(payload);
request.end();
