"""Reproduce the pinned Qt Korean catalog; ordinary builds use the committed QM."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--lrelease', required=True)
    args = parser.parse_args()
    folder = ROOT / 'resources/i18n'
    manifest = json.loads((folder / 'manifest.json').read_text(encoding='utf-8'))
    tool = str(Path(args.lrelease).resolve())
    version = subprocess.check_output([tool, '-version'], text=True).strip()
    if version != 'lrelease version ' + manifest['lrelease_version']:
        raise ValueError('Expected pinned lrelease version, got: ' + version)
    url = (manifest['repository'].replace('https://github.com/', 'https://raw.githubusercontent.com/') +
           '/' + manifest['revision'] + '/' + manifest['source'])
    with urllib.request.urlopen(url, timeout=30) as response:
        source = response.read()
    if hashlib.sha256(source).hexdigest() != manifest['source_sha256']:
        raise ValueError('Qt translation source hash mismatch')
    with tempfile.TemporaryDirectory(prefix='ppm-qt-translations-') as temporary:
        ts = Path(temporary) / 'qtbase_ko.ts'
        qm = Path(temporary) / manifest['output']
        ts.write_bytes(source)
        subprocess.run([tool, *manifest['lrelease_options'], str(ts), '-qm', str(qm)], check=True)
        data = qm.read_bytes()
    if hashlib.sha256(data).hexdigest() != manifest['output_sha256']:
        raise ValueError('Generated Qt catalog hash mismatch')
    (folder / manifest['output']).write_bytes(data)
    print('Verified and reproduced ' + manifest['output'])


if __name__ == '__main__':
    main()
