"""Verify and copy the offline, replaceable MPP reader and corresponding source."""
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil

ROOT = Path(__file__).resolve().parents[1]


def verify_reader(directory, lock=None, signing=False):
    directory = Path(directory).resolve()
    lock = lock or json.loads((ROOT / "packaging/mpp-reader/engine-lock.json").read_text())
    manifest = json.loads((directory / "manifest.json").read_text())
    if manifest.get("protocol") != 2 or manifest.get("engine") != "MPXJ" or manifest.get("engineVersion") != "16.10.0":
        raise ValueError("MPP reader protocol/version mismatch")
    files = manifest.get("files", {})
    actual = {p.relative_to(directory).as_posix() for p in directory.rglob("*") if p.is_file() and p.name != "manifest.json"}
    if not files or len(files) > 2000 or set(files) != actual:
        raise ValueError("MPP reader inventory is incomplete")
    for name, expected in files.items():
        relative = PurePosixPath(name)
        path = directory / name
        if relative.is_absolute() or ".." in relative.parts or "\\" in name or not path.resolve().is_relative_to(directory):
            raise ValueError("MPP reader path escapes bundle")
        content = path.read_bytes()
        observed = hashlib.sha256(content).hexdigest()
        if observed != expected:
            if not signing or not name.startswith("runtime/") or content[:4] not in {bytes.fromhex(v) for v in ("feedface", "cefaedfe", "feedfacf", "cffaedfe", "cafebabe", "bebafeca", "cafebabf", "bfbafeca")}:
                raise ValueError("MPP reader checksum mismatch: " + name)
            files[name] = observed
    for name, expected in lock["jars"].items():
        if files.get("lib/" + name) != expected:
            raise ValueError("MPP reader dependency differs from lock: " + name)
    for source in lock["sources"]:
        if files.get("sources/" + source["name"]) != source["sha256"]:
            raise ValueError("MPP reader corresponding source is missing")
    if signing:
        manifest["platformSigned"] = True
        (directory / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return manifest


def deploy_reader(source, destination):
    verify_reader(source)
    shutil.copytree(source, destination, symlinks=True)
    verify_reader(destination)
