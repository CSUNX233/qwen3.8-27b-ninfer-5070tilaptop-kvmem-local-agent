"""Package a qualified engine and its notices; no models or runtime DLLs."""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile


def sha_stream(stream):
    digest = hashlib.sha256()
    for chunk in iter(lambda: stream.read(8 * 1024 * 1024), b''):
        digest.update(chunk)
    return digest.hexdigest()


def sha_file(path):
    with path.open('rb') as stream:
        return sha_stream(stream)


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--engine', required=True, type=Path)
    parser.add_argument('--output', type=Path, default=root / 'dist')
    args = parser.parse_args()
    manifest_path = root / 'config/downloads.json'
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    provenance = json.loads((root / 'SOURCE_PROVENANCE.json').read_text(encoding='utf-8'))
    spec = next(item for item in manifest['archives'] if item['id'] == 'engine')
    expected = provenance['engine_binary_sha256']
    actual = sha_file(args.engine)
    if actual != expected or actual != spec['files'][0]['sha256']:
        raise SystemExit('Engine does not match the qualified release SHA-256')
    args.output.mkdir(parents=True, exist_ok=True)
    archive = args.output / spec['name']
    source = provenance['release_source_url']
    print('Packaging qualified engine with licenses and notices...', flush=True)
    with zipfile.ZipFile(archive, 'w', compression=zipfile.ZIP_DEFLATED,
                         compresslevel=6, allowZip64=True) as bundle:
        bundle.write(args.engine, 'ninfer-serve.exe')
        for name in ['LICENSE', 'NOTICE']:
            bundle.write(root / name, name)
        for path in sorted((root / 'licenses').glob('*')):
            if path.is_file():
                bundle.write(path, 'licenses/' + path.name)
        vendor = root / 'engine/third_party'
        for path in sorted(vendor.rglob('*')):
            if path.is_file() and ('LICENSE' in path.name.upper() or path.name.upper() == 'NOTICE'):
                bundle.write(path, 'third-party-notices/' + path.relative_to(vendor).as_posix())
        bundle.writestr('RUNTIME.txt',
            'Engine only; no models or runtime DLLs.\n'
            'Use setup.ps1 from the matching source release for checksum-pinned dependencies.\n'
            f'Source: {source}\nEngine SHA-256: {actual}\n'
            'Windows x64 / sm120a; CUDA 13 compatible NVIDIA driver and x64 VC++ runtime required.\n')
    print('Verifying archived engine and notices...', flush=True)
    with zipfile.ZipFile(archive) as bundle:
        with bundle.open('ninfer-serve.exe') as stream:
            if sha_stream(stream) != actual:
                raise SystemExit('Archived engine hash mismatch')
        if bundle.testzip() is not None:
            raise SystemExit('ZIP integrity check failed')
    if archive.stat().st_size >= 2 * 1024 ** 3:
        raise SystemExit('Archive exceeds GitHub Release asset size limit')
    spec['sha256'] = sha_file(archive)
    manifest_path.write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    (args.output / 'SHA256SUMS').write_text(spec['sha256'] + '  ' + archive.name + '\n', encoding='ascii')
    print(json.dumps({'archive': str(archive), 'bytes': archive.stat().st_size,
                      'sha256': spec['sha256'], 'engine_sha256': actual}), flush=True)


if __name__ == '__main__':
    main()
