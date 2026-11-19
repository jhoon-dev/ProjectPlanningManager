"""Check Node 24 action downloads and the REST ZIP contract used by release tooling."""
import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import re
import subprocess
import unicodedata
import zipfile


def prepare(directory):
    directory.mkdir(parents=True, exist_ok=True)
    inner = io.BytesIO()
    with zipfile.ZipFile(inner, "w") as archive:
        archive.writestr("bundle/fixture.txt", "CI artifact contract; not a distribution package\n")
    files = {
        "status.json": b'{"fixture":"ci-action-runtime-contract","signed":false}\n',
        "nested/\ud55c\uae00.txt": "UTF-8 \uc0b0\ucd9c\ubb3c\nCRLF\r\n".encode("utf-8"),
        "payload.bin": bytes(range(256)) * 4,
        "bundle.zip": inner.getvalue(),
    }
    for name, data in files.items():
        path = directory / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)


def snapshot(directory):
    return {
        unicodedata.normalize("NFC", path.relative_to(directory).as_posix()): path.read_bytes()
        for path in directory.rglob("*") if path.is_file()
    }


def verify(expected, downloaded, archive_bytes=None):
    original = snapshot(expected)
    if not original or snapshot(downloaded) != original:
        raise RuntimeError("Downloaded artifact paths or bytes differ from the input")
    if archive_bytes is not None:
        with zipfile.ZipFile(io.BytesIO(archive_bytes)) as archive:
            files = [entry for entry in archive.infolist() if not entry.is_dir()]
            restored = {unicodedata.normalize("NFC", entry.filename): archive.read(entry) for entry in files}
        if len(restored) != len(files) or restored != original:
            raise RuntimeError("REST artifact ZIP paths or bytes differ from the input")
    return {name: hashlib.sha256(data).hexdigest() for name, data in original.items()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    create = commands.add_parser("prepare")
    create.add_argument("directory", type=Path)
    check = commands.add_parser("verify")
    check.add_argument("expected", type=Path)
    check.add_argument("downloaded", type=Path)
    check.add_argument("--artifact-id", required=True)
    args = parser.parse_args()
    if args.command == "prepare":
        prepare(args.directory)
        return
    repository = os.environ["GITHUB_REPOSITORY"]
    if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", repository) or not re.fullmatch(r"[1-9][0-9]*", args.artifact_id):
        raise RuntimeError("Invalid artifact repository or ID")
    archive_bytes = subprocess.check_output([
        "gh", "api", f"repos/{repository}/actions/artifacts/{args.artifact_id}/zip"
    ])
    hashes = verify(args.expected, args.downloaded, archive_bytes)
    print(json.dumps({"status": "PASS", "files": hashes, "rest_zip": True}, indent=2))


if __name__ == "__main__":
    main()
