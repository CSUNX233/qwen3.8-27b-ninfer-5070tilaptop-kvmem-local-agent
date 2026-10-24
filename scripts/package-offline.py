"""Build an isolated Windows bundle with optional models and pinned dependencies.

Does not start services or copy user runtime state. Output is local, never uploaded.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import time
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def sha(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(8 * 1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def copy_file(source, target):
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, target)


def extract_zip(archive, destination, prefix=''):
    with zipfile.ZipFile(archive) as z:
        for info in z.infolist():
            name = info.filename
            if prefix:
                if not name.startswith(prefix):
                    continue
                name = name[len(prefix):]
            if not name:
                continue
            parts = Path(name.replace('\\', '/'))
            if parts.is_absolute() or '..' in parts.parts or ':' in name:
                raise ValueError('Unsafe archive path')
            target = destination / parts
            if info.is_dir():
                target.mkdir(parents=True, exist_ok=True)
            else:
                target.parent.mkdir(parents=True, exist_ok=True)
                with z.open(info) as source, target.open('wb') as output:
                    shutil.copyfileobj(source, output, 4 * 1024 * 1024)


def write_cmd(path, command):
    path.write_bytes(('@echo off\r\n' + command + '\r\nif errorlevel 1 pause\r\n').encode('ascii'))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--variant', choices=['no-models', 'full'])
    p.add_argument('--engine', type=Path)
    p.add_argument('--huihui', type=Path)
    p.add_argument('--gsq', type=Path)
    p.add_argument('--vision', type=Path, help='Huihui text+vision+MTP v3 artifact; CPU vision is opt-in')
    p.add_argument('--dependencies', type=Path, help='Directory with pinned dependency downloads')
    p.add_argument('--dependency-manifest', type=Path)
    p.add_argument('--output', type=Path, required=True, help='New directory, never an existing installation')
    p.add_argument('--archive-only', action='store_true', help='Archive a previously verified built directory')
    args = p.parse_args()
    output = args.output.resolve()
    if args.archive_only:
        if not (output / 'offline-bundle.json').is_file():
            raise SystemExit('No built manifest found')
        archive_bundle(output)
        return
    if not all([args.variant, args.engine, args.dependencies, args.dependency_manifest]):
        p.error('Building requires --variant, --engine, --dependencies and --dependency-manifest')
    if args.variant == 'full' and not all([args.huihui, args.gsq, args.vision]):
        p.error('The full variant requires --huihui, --gsq and --vision')
    if args.variant == 'no-models' and any([args.huihui, args.gsq, args.vision]):
        p.error('The no-models variant must not receive model paths')
    if output.exists():
        raise SystemExit('Output already exists; choose a new directory')
    if output.is_relative_to(ROOT) and output.parts[len(ROOT.parts)] != 'dist':
        raise SystemExit('Use a separate output directory or dist/')
    downloads = json.loads((ROOT / 'config/downloads.json').read_text('utf8'))
    dependencies = json.loads(args.dependency_manifest.read_text('utf8'))
    engine_spec = next(s for s in downloads['archives'] if s['id'] == 'engine')
    if sha(args.engine) != engine_spec['files'][0]['sha256']:
        raise SystemExit('Engine hash mismatch')
    for spec in dependencies:
        if sha(args.dependencies / spec['name']) != spec['sha256']:
            raise SystemExit('Dependency archive hash mismatch: ' + spec['name'])
    output.mkdir(parents=True)
    # Only Git-visible public source, never broad copying from the working installation.
    visible = subprocess.check_output(['git', 'ls-files', '--cached', '--others', '--exclude-standard', '-z'], cwd=ROOT)
    for raw in sorted(set(visible.split(b'\0'))):
        if not raw:
            continue
        rel = Path(raw.decode('utf8'))
        if rel.parts[0] in ('.git', 'runtime', 'models', 'dist') or 'node_modules' in rel.parts:
            raise SystemExit('Unexpected private/runtime path in source file list')
        source = ROOT / rel
        if source.is_file():
            copy_file(source, output / rel)
    copy_file(args.engine, output / 'runtime/engine/ninfer-serve.exe')
    for spec in downloads['archives']:
        if spec['id'] == 'engine':
            continue
        archive = args.dependencies / spec['name']
        # Preserve the original redistributable archive and all accompanying notices.
        copy_file(archive, output / 'dependencies' / spec['name'])
        with zipfile.ZipFile(archive) as z:
            for file in spec['files']:
                matches = [n for n in z.namelist() if Path(n).name == file['name']]
                if len(matches) != 1:
                    raise SystemExit('Ambiguous runtime DLL')
                target = output / 'runtime/engine' / file['name']
                with z.open(matches[0]) as source, target.open('wb') as dest:
                    shutil.copyfileobj(source, dest, 4 * 1024 * 1024)
                if sha(target) != file['sha256']:
                    raise SystemExit('Runtime DLL hash mismatch')
            for info in z.infolist():
                lower = info.filename.lower()
                if not info.is_dir() and ('license' in lower or '/doc/' in lower or '/licenses/' in lower or lower.endswith('readme.txt')):
                    target = output / 'dependency-notices' / spec['id'] / info.filename
                    if '..' in Path(info.filename).parts:
                        raise SystemExit('Unsafe notice path')
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(z.read(info))
    node = next(d for d in dependencies if d['id'] == 'node')
    extract_zip(args.dependencies / node['name'], output / 'runtime/node', 'node-v24.14.1-win-x64/')
    vc = next(d for d in dependencies if d['id'] == 'vc_redist')
    copy_file(args.dependencies / vc['name'], output / 'dependencies' / vc['name'])
    for rel in ['integrations/dsh/node_modules', 'integrations/dsh/local-web/node_modules']:
        source = ROOT / rel
        if not source.is_dir():
            raise SystemExit('Run pinned npm ci first: ' + rel)
        for base, dirs, files in os.walk(source, followlinks=False):
            for name in dirs + files:
                item = Path(base) / name
                if item.is_symlink() or (getattr(item.lstat(), 'st_file_attributes', 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT):
                    raise SystemExit('Dependency link needs explicit review: ' + name)
        shutil.copytree(source, output / rel)
    # Pre-apply only the exact version-checked redistributable patches. No home is copied.
    for name in ('dsh-compaction-basic', 'dsh-compaction-tool-result-pruner'):
        folder = output / 'integrations/dsh/node_modules/@deepseek-ai' / name
        if json.loads((folder / 'package.json').read_text('utf8'))['version'] != '0.1.5-rc.3':
            raise SystemExit('Unsupported dsh patch version')
        if not (folder / 'lib/index.js.ninfer-original').exists():
            copy_file(folder / 'lib/index.js', folder / 'lib/index.js.ninfer-original')
        copy_file(output / ('integrations/dsh/patches/' + name + '.js'), folder / 'lib/index.js')
    models = []
    model_inputs = [
        ('huihui', args.huihui, 'Huihui-Qwen3.8-27B-abliterated-Swift-1.5-GSQ-RCO-IQ3_XXS-mtp.ninfer', 'huihui-qwen3.8-27b-swift1.5-iq3-xxs'),
        ('gsq', args.gsq, 'qwen3.8-27b-gsq-rco-iq3-xxs.ninfer', 'qwen3.8-27b-gsq-rco-iq3-xxs'),
        ('huihui-vision', args.vision, 'Huihui-Qwen3.8-27B-abliterated-Swift-1.5-GSQ-RCO-IQ3_XXS-mtp-vision-bf16.ninfer', 'huihui-qwen3.8-27b-swift1.5-iq3-xxs-vision'),
    ] if args.variant == 'full' else []
    for model_id, source, filename, public_id in model_inputs:
        target = output / 'models' / filename
        print('Copying and hashing model:', model_id, flush=True)
        copy_file(source, target)
        models.append(dict(id=model_id, path='models/' + filename, model_id=public_id, bytes=target.stat().st_size,
            sha256=sha(target), components=['text','vision','mtp'] if model_id == 'huihui-vision' else ['text','mtp'],
            vision_cpu=model_id == 'huihui-vision'))
    shutil.copytree(ROOT / 'licenses/models', output / 'model-notices')
    (output / 'model-notices/CONVERSION-NOTICE.txt').write_text(
        'Converted and integrated by CSUNX233 using ninfer-v3 qwen3_8_27b_gguf on CPU.\n'
        'Quantized tensors copied from the named GGUFs; no retraining/requantization.\n'
        'Huihui Swift lineage is documented in the included Huihui model card.\n'
        'Copyright 2026 UkisAI. Swift Contribution licensed under Swift Open License v1.0.\n'
        'Derivative of Qwen3.8-27B, Copyright 2026 Alibaba Cloud, Apache License 2.0.\n'
        'Huihui, ISTA-DASLab and other contributors retain attribution.\n'
        'The optional vision artifact contains complete Huihui text+MTP weights and the BF16 Qwen vision tower.\n'
        'The no-models variant distributes no model weights.\n'
        'Original GGUF revision was not pinned in the historical conversion record;\n'
        'included cards are source-page snapshots, not historical remote-byte verification.\n', encoding='utf8')
    for doc, name in [('OFFLINE_BUNDLE.md', '00-READ-FIRST.md'), ('INSTALL_PROMPT.md', 'INSTALL-PROMPT.md'),
                      ('TROUBLESHOOTING.md', 'TROUBLESHOOTING.md'), ('RELEASE_NOTES.md', 'WHATS-NEW.md')]:
        # Keep original relative Markdown links valid by using small root entry documents.
        (output / name).write_text(f'# {name}\n\nSee [Chinese instructions](docs/{doc}).\n', encoding='utf8')
    for doc, name in [('OFFLINE_BUNDLE.md', '00-先读安装说明.txt'), ('INSTALL_PROMPT.md', '安装提示词.txt'),
                      ('TROUBLESHOOTING.md', '纠错文档和提示词.txt'), ('RELEASE_NOTES.md', '更新提升说明.txt')]:
        (output / name).write_text((ROOT / 'docs' / doc).read_text('utf8'), encoding='utf-8-sig')
    write_cmd(output / '00-install-vc-runtime.cmd', 'start /wait "" "%~dp0dependencies\\vc_redist.x64.exe" /install')
    choices = [('01-setup-huihui.cmd', 'scripts/setup-bundle.ps1', '-Model huihui'),
        ('01-setup-gsq.cmd', 'scripts/setup-bundle.ps1', '-Model gsq'),
        ('05-use-huihui.cmd', 'scripts/set-bundle-option.ps1', '-Model huihui'),
        ('05-use-gsq.cmd', 'scripts/set-bundle-option.ps1', '-Model gsq'),
        ('09-enable-cpu-vision.cmd', 'scripts/set-bundle-option.ps1', '-Model huihui-vision'),
        ('09-disable-vision-huihui.cmd', 'scripts/set-bundle-option.ps1', '-Model huihui')
    ] if args.variant == 'full' else [('01-setup-your-model.cmd', 'scripts/setup-bundle.ps1', '')]
    for name, script, flags in choices + [
        ('02-start.cmd', 'start.ps1', ''), ('03-stop.cmd', 'stop.ps1', ''),
        ('04-verify.cmd', 'scripts/verify-bundle.ps1', ''),
        ('06-answer-counts.cmd', 'scripts/set-bundle-option.ps1', '-PenaltyScope answer'),
        ('06-carry-over-counts.cmd', 'scripts/set-bundle-option.ps1', '-PenaltyScope carry-over'),
        ('07-kv-nvfp4.cmd', 'scripts/set-bundle-option.ps1', '-KVDType nvfp4'),
        ('07-kv-rk4v4.cmd', 'scripts/set-bundle-option.ps1', '-KVDType rk4v4'),
        ('08-pool-small.cmd', 'scripts/set-bundle-option.ps1', '-PoolPreset small'),
        ('08-pool-default.cmd', 'scripts/set-bundle-option.ps1', '-PoolPreset default')]:
        write_cmd(output / name, 'powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0' + script.replace('/', '\\') + '" ' + flags)
    dependency_urls = '\n'.join(f"{d['id']}: {d['url']}\nSHA-256: {d['sha256']}" for d in dependencies)
    (output / 'dependencies/SOURCES.txt').write_text(dependency_urls +
        '\nNVIDIA driver: https://www.nvidia.com/Download/index.aspx\n'
        'Microsoft Edge: https://www.microsoft.com/edge/download\n'
        'FFmpeg LGPL shared source/build reference:\nhttps://github.com/FFmpeg/FFmpeg/tree/045dea2a34\n'
        'https://github.com/BtbN/FFmpeg-Builds/releases/tag/autobuild-2026-10-14-13-03\n'
        'Node.js source/license: https://nodejs.org/dist/v24.14.1/\n', encoding='utf8')
    print('Hashing bundle files...', flush=True)
    files = []
    known_models = {m['path']: m['sha256'] for m in models}
    for path in sorted(output.rglob('*')):
        if path.is_file():
            rel = path.relative_to(output).as_posix()
            files.append(dict(path=rel, bytes=path.stat().st_size, sha256=known_models.get(rel) or sha(path)))
    manifest = dict(schema=1, version=downloads['version'],
        source_commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
        source_has_uncommitted_changes=bool(subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT)),
        engine_sha256=engine_spec['files'][0]['sha256'], models=models, dependencies=dependencies,
        variant=args.variant, default_model='huihui' if models else None,
        default_vision_cpu=False, default_penalty_scope='answer',
        system_prerequisites=['Windows x64', 'CUDA 13 compatible NVIDIA driver', 'Microsoft Edge', 'x64 VC++ runtime (installer included)'],
        files=files)
    (output / 'offline-bundle.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf8')
    (output / 'SHA256SUMS').write_text(''.join(f"{f['sha256']}  {f['path']}\n" for f in files) + sha(output / 'offline-bundle.json') + '  offline-bundle.json\n', encoding='utf8')
    print(json.dumps(dict(bundle=str(output), files=len(files), bytes=sum(f['bytes'] for f in files), models=models)), flush=True)


def archive_bundle(output):
    archive = output.parent / (output.name + '.zip')
    if archive.exists():
        raise SystemExit('Archive already exists')
    manifest = json.loads((output / 'offline-bundle.json').read_text('utf8'))
    approved = {entry['path']: entry for entry in manifest['files']}
    for name, entry in approved.items():
        path = output / name
        if '..' in Path(name).parts or not path.resolve().is_relative_to(output):
            raise SystemExit('Invalid bundle path')
        if path.stat().st_size != entry['bytes'] or sha(path) != entry['sha256']:
            raise SystemExit('Bundle changed before archive: ' + name)
    print('Creating ZIP64; models are stored to avoid costly recompression...', flush=True)
    last = time.monotonic()
    with zipfile.ZipFile(archive, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=3, allowZip64=True) as z:
        for name in sorted([*approved, 'offline-bundle.json', 'SHA256SUMS']):
            path = output / name
            if path.is_file():
                compression = zipfile.ZIP_STORED if path.suffix.lower() in ('.ninfer', '.zip') else zipfile.ZIP_DEFLATED
                z.write(path, output.name + '/' + path.relative_to(output).as_posix(), compress_type=compression)
                if time.monotonic() - last > 25:
                    print('ZIP progress:', round(archive.stat().st_size / 1024**3, 2), 'GiB', flush=True);last=time.monotonic()
    print('Verifying ZIP CRC...', flush=True)
    with zipfile.ZipFile(archive) as z:
        bad = z.testzip()
        if bad:
            raise SystemExit('ZIP CRC failed: ' + bad)
    digest = sha(archive)
    archive.with_suffix('.zip.sha256').write_text(digest + '  ' + archive.name + '\n', encoding='utf8')
    print(json.dumps(dict(archive=str(archive), bytes=archive.stat().st_size, sha256=digest, zip_crc_passed=True)), flush=True)


if __name__ == '__main__':
    main()
