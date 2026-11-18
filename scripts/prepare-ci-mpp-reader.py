"""Fetch hash-locked MPP reader inputs for CI, then build the offline reader."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time
import urllib.error
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def download(url, path, expected):
    if path.is_file() and digest(path) == expected:
        return
    temporary = path.with_suffix(path.suffix + '.download')
    request = urllib.request.Request(url, headers={'User-Agent': 'ProjectPlanningManager-CI'})
    for attempt in range(5):
        try:
            with urllib.request.urlopen(request, timeout=90) as response, temporary.open('wb') as output:
                while chunk := response.read(1024 * 1024):
                    output.write(chunk)
            break
        except (urllib.error.URLError, TimeoutError, ConnectionError) as error:
            if isinstance(error, urllib.error.HTTPError) and error.code not in (408, 429, 500, 502, 503, 504):
                raise
            if attempt == 4:
                raise
            print('Retrying MPP source download: ' + path.name, flush=True)
            time.sleep(2 ** attempt)
    if digest(temporary) != expected:
        temporary.unlink()
        raise ValueError('MPP source checksum mismatch: ' + path.name)
    temporary.replace(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--cache', type=Path, default=ROOT / '.cache/mpp-reader-inputs')
    parser.add_argument('--jdk', type=Path, default=Path(os.environ.get('JAVA_HOME', '')))
    args = parser.parse_args()
    lock = json.loads((ROOT / 'packaging/mpp-reader/engine-lock.json').read_text())
    libraries = args.cache / 'lib'
    sources = args.cache / 'sources'
    wheels = args.cache / 'wheels'
    for folder in (libraries, sources, wheels):
        folder.mkdir(parents=True, exist_ok=True)
    if not all((libraries / name).is_file() and digest(libraries / name) == expected
               for name, expected in lock['jars'].items()):
        subprocess.run([sys.executable, '-m', 'pip', 'download', '--no-deps', '--only-binary=:all:',
                        'mpxj==' + lock['engineVersion'], '--dest', str(wheels)], check=True)
        wheel = next(wheels.glob('mpxj-' + lock['engineVersion'] + '-*.whl'))
        with zipfile.ZipFile(wheel) as archive:
            for name, expected in lock['jars'].items():
                data = archive.read('mpxj/lib/' + name)
                if hashlib.sha256(data).hexdigest() != expected:
                    raise ValueError('MPXJ jar differs from engine lock: ' + name)
                (libraries / name).write_bytes(data)
    for source in lock['sources']:
        download(source['url'], sources / source['name'], source['sha256'])
    subprocess.run([sys.executable, str(ROOT / 'scripts/prepare-mpp-reader.py'),
                    '--jdk', str(args.jdk.resolve()), '--libraries', str(libraries.resolve()),
                    '--sources', str(sources.resolve()), '--output', str(args.output.resolve())], check=True)


if __name__ == '__main__':
    main()
