"""Check the public deliverable without a GPU, private data, or model downloads."""
from pathlib import Path
import json
import re
import subprocess
import sys
from urllib.parse import unquote

root = Path(__file__).resolve().parents[1]
errors = []
docs = [root/'README.md', root/'README.en.md', *list((root/'docs').rglob('*.md')),
        root/'benchmarks/README.md', root/'integrations/README.md']
for doc in docs:
    for target in re.findall(r'\[[^\]]*\]\(([^)]+)\)', doc.read_text(encoding='utf-8')):
        if '://' in target or target.startswith('#'):
            continue
        path = unquote(target.split('#', 1)[0])
        if path and not (doc.parent/path).exists():
            errors.append(f'{doc.relative_to(root)}: missing {path}')
manifest = json.loads((root/'config/downloads.json').read_text(encoding='utf-8'))
assert manifest['version'] == '0.1.0'
for archive in manifest['archives']:
    assert archive['url'].startswith('https://')
    assert re.fullmatch('[0-9a-f]{64}', archive['sha256'])
    for file in archive['files']:
        assert Path(file['name']).name == file['name']
        assert re.fullmatch('[0-9a-f]{64}', file['sha256'])
package = json.loads((root/'integrations/dsh/package.json').read_text())
lock = json.loads((root/'integrations/dsh/package-lock.json').read_text())
assert package['dependencies'] == lock['packages']['']['dependencies']
assert package['dependencies']['@deepseek-ai/dsh'] == '0.1.5-rc.3'
# Scan candidate tracked publication files only. Runtime state is intentionally excluded.
tracked = subprocess.check_output(['git', 'ls-files', '--cached', '--others', '--exclude-standard', '-z'], cwd=root)
files = {Path(item.decode('utf-8')) for item in tracked.split(b'\0') if item}
for rel in files:
    p = root/rel
    if rel.parts[0] in ('runtime', 'models', 'dist') or 'node_modules' in rel.parts:
        errors.append(f'Private/large runtime path included: {rel}')
    if p.is_file() and p.stat().st_size > 95*1024*1024:
        errors.append(f'File too large for source repository: {rel}')
    if p.is_file() and p.stat().st_size < 5*1024*1024:
        data = p.read_bytes()
        if b'\0' in data:
            continue
        # Report path only; never print a possible credential.
        if re.search(rb'\b(?:ghp_[A-Za-z0-9]{25,}|github_pat_[A-Za-z0-9_]{35,}|hf_[A-Za-z0-9]{25,})\b|-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----', data):
            errors.append(f'Possible credential needs review: {rel}')
subprocess.run([sys.executable, '-m', 'py_compile', str(root/'benchmarks/run.py')], check=True)
if errors:
    print('\n'.join(errors));raise SystemExit(1)
print(f'PASS: {len(docs)} public documents, pinned downloads, lockfile, {len(files)} candidate files; no runtime/model paths or credential patterns found.')
