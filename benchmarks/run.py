"""Synthetic API reproduction fixtures. Never read a user's dsh session or files."""
import argparse
import json
from pathlib import Path
import time
import urllib.request

PROSE = ('Write a detailed technical guide of at least 2000 words explaining how an immutable paged '
         'KV cache can share a prefix while keeping a private writable tail. Discuss reference counts, '
         'copy-on-write, asynchronous transfer lifetimes, cancellation, and recovery after a tool call. '
         'Use concrete examples and avoid repeating the same sentences.')
CODE = ('Implement a Python 3.12 bounded asyncio worker pool. Include ordered results, a configurable '
        'concurrency limit, graceful cancellation, per-item errors, and an example demonstrating all '
        'these cases. Return a complete detailed Python implementation with explanatory comments.')
CODES = ('H7K2-ALPHA', 'M9P4-BETA', 'T3R8-GAMMA')
TOOL = dict(type='function', function=dict(name='read_file', description='Read a UTF-8 local file.',
    parameters=dict(type='object', properties=dict(path=dict(type='string')), required=['path'], additionalProperties=False)))


class Runner:
    def __init__(self, args):
        self.args = args
        self.output = args.output / args.label
        self.output.mkdir(parents=True, exist_ok=True)
        self.opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))

    def generate(self, messages, limit, label, **extra):
        body = dict(model=self.args.model, messages=messages, max_tokens=limit, temperature=0,
                    seed=1234, reasoning_effort='none', stream=True, stream_options={'include_usage': True})
        body.update(extra)
        request = urllib.request.Request(self.args.url.rstrip('/') + '/v1/chat/completions',
            json.dumps(body).encode(), {'Content-Type': 'application/json'})
        start, first = time.perf_counter(), None
        message, calls, final = dict(role='assistant', content=''), {}, {}
        with self.opener.open(request, timeout=3600) as response:
            for line in response:
                if not line.startswith(b'data: '):
                    continue
                payload = line[6:].strip()
                if payload == b'[DONE]':
                    break
                event = json.loads(payload)
                if 'error' in event:
                    raise RuntimeError(event['error'])
                for key in ('usage', 'timings'):
                    if event.get(key):
                        final[key] = event[key]
                for choice in event.get('choices', []):
                    if choice.get('finish_reason'):
                        final['finish_reason'] = choice['finish_reason']
                    delta = choice.get('delta', {})
                    if any(delta.get(key) for key in ('content', 'reasoning_content', 'tool_calls')) and first is None:
                        first = time.perf_counter() - start
                    for key in ('content', 'reasoning_content'):
                        if delta.get(key):
                            message[key] = message.get(key, '') + delta[key]
                    for call in delta.get('tool_calls', []):
                        merged = calls.setdefault(call['index'], dict(type='function', function=dict(name='', arguments='')))
                        if call.get('id'):
                            merged['id'] = call['id']
                        for key in ('name', 'arguments'):
                            merged['function'][key] += call.get('function', {}).get(key, '')
        if calls:
            message['tool_calls'] = list(calls.values())
        final.update(message=message, wall_seconds=time.perf_counter()-start, ttft_seconds=first)
        (self.output / (label + '.json')).write_text(json.dumps(final, ensure_ascii=False, indent=2), encoding='utf-8')
        print(label, json.dumps({k: v for k, v in final.items() if k != 'message'}), flush=True)
        return final

    def document(self):
        records = [f'Entry {i}: amber cedar silver orbit valley; archived reference only.\n' for i in range(3500)]
        for index, name, code in zip((4, 1700, -4), ('HEAD_CODE', 'MIDDLE_CODE', 'TAIL_CODE'), CODES):
            records[index] = name + ' = ' + code + '\n' + records[index]
        return [dict(role='system', content='You are validating retained document references. '
                     'Use the document exactly. Never invent the three named codes.'),
                dict(role='user', content='Local cache regression dataset 20261013.\n' + ''.join(records) +
                     '\nList HEAD_CODE, MIDDLE_CODE, TAIL_CODE exactly, then give a numbered '
                     'technical explanation of immutable cached pages and private tail copies.')]

    def smoke(self):
        result = self.generate([dict(role='user', content='Calculate 12347*891. Return only the exact integer.')], 32, 'math')
        assert result['message']['content'].strip() == '11001177', result['message']
        history = [dict(role='user', content='Read verification.txt using read_file, then report its code.')]
        call = self.generate(history, 128, 'tool-call', tools=[TOOL],
            tool_choice=dict(type='function', function=dict(name='read_file')))
        assert call['message'].get('tool_calls'), call['message']
        history.append(call['message'])
        for item in call['message']['tool_calls']:
            assert Path(json.loads(item['function']['arguments'])['path']).name == 'verification.txt'
            history.append(dict(role='tool', tool_call_id=item['id'], content='VERIFICATION_CODE = GPU-STATE-7392'))
        answer = self.generate(history, 128, 'tool-result', tools=[TOOL])
        assert 'GPU-STATE-7392' in answer['message']['content'], answer['message']

    def short(self):
        self.generate([dict(role='user', content='Explain binary search with a small example.')], 96, 'warmup')
        for index in range(2):
            self.generate([dict(role='user', content=PROSE)], 768, f'prose-{index}')
            self.generate([dict(role='user', content=CODE)], 1024, f'code-{index}')

    def long(self):
        messages = self.document()
        self.generate(messages, 512, 'cold')
        self.generate(messages, 512, 'warm')
        history = messages + [dict(role='assistant', content='The document has been retained.'),
            dict(role='user', content='Give HEAD_CODE, MIDDLE_CODE, TAIL_CODE exactly, one per line.')]
        result = self.generate(history, 256, 'recall')
        assert all(code in result['message']['content'] for code in CODES), result['message']

    def agent(self):
        history = self.document()
        history[0]['content'] += (' For this memory stress task, follow the exact requested output format. '
                                  'Do not add Markdown numbering, bullets, or summaries.')
        document = history[-1]['content'].split('\nList HEAD_CODE', 1)[0]
        history[-1]['content'] = document + (
            '\nFirst print HEAD_CODE, MIDDLE_CODE, TAIL_CODE exactly. Then write 3000 copies of this exact '
            'line, one copy per line: amber cedar silver orbit valley; archived reference only.\n'
            'Do not number the lines. Do not shorten, summarize, use ellipses or code. '
            'Do not use any tool yet; a later turn will ask you to read tool-evidence.txt.')
        generated = 0
        for index in range(4):
            answer = self.generate(history, 22528, f'long-{index}', tools=[TOOL], presence_penalty=0)
            text = answer['message']['content']
            assert not any(token in text for token in ('<|im_end|>', '<|im_start|>', '<|endoftext|>'))
            history.append(answer['message'])
            generated += answer['usage']['completion_tokens']
            if generated >= 22000:
                break
            history.append(dict(role='user', content='Continue writing the same exact line repeatedly. Write at least '
                '3000 further copies without numbering, code, ellipses or commentary. Do not use tools yet.'))
        assert generated >= 22000, ('Fixture did not exceed the recent KV window', generated)
        history.append(dict(role='user', content='Now use read_file to read the entire tool-evidence.txt file. '
            'After reading it, report its verification code and all three named document codes.'))
        call = self.generate(history, 256, 'tool-call', tools=[TOOL],
                             tool_choice=dict(type='function', function=dict(name='read_file')))
        usage = call['usage']
        assert usage['prompt_tokens'] - usage['prompt_tokens_details']['cached_tokens'] <= 256, usage
        assert call['message'].get('tool_calls'), 'Missing structured tool call'
        history.append(call['message'])
        evidence = self.output / 'tool-evidence.txt'
        evidence.write_text('TOOL_CODE = LOCAL-KV-OK-731\n', encoding='utf-8')
        for item in call['message']['tool_calls']:
            assert Path(json.loads(item['function']['arguments'])['path']).name == evidence.name
            history.append(dict(role='tool', tool_call_id=item['id'], content=evidence.read_text(encoding='utf-8')))
        history.append(dict(role='user', content='Report TOOL_CODE and HEAD_CODE, MIDDLE_CODE, TAIL_CODE exactly.'))
        for label in ['tool-result'] + [f'followup-{index}' for index in range(3)]:
            result = self.generate(history, 256, label, tools=[TOOL])
            assert all(code in result['message']['content'] for code in ('LOCAL-KV-OK-731', *CODES)), result['message']
            history.append(result['message'])
            if label != 'followup-2':
                history.append(dict(role='user', content='Repeat the four verification codes exactly, one per line.'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mode', choices=('smoke', 'short', 'long', 'agent'), default='short')
    parser.add_argument('--url', default='http://127.0.0.1:1234')
    parser.add_argument('--model', default='qwen3.8-27b-gsq-rco-iq3-xxs')
    parser.add_argument('--label', default='run')
    parser.add_argument('--output', type=Path, default=Path(__file__).resolve().parents[1] / 'runtime/benchmarks')
    args = parser.parse_args()
    if not args.label or Path(args.label).name != args.label:
        parser.error('--label must be one directory name')
    runner = Runner(args)
    getattr(runner, args.mode)()


if __name__ == '__main__':
    main()
