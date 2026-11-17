"""Build a separate, offline MPP reader from locked MPXJ libraries and a local JDK21.

No software is downloaded or executed from an imported document. Run this at build time.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def build(jdk, libraries, output, sources):
    lock = json.loads((ROOT / "packaging/mpp-reader/engine-lock.json").read_text())
    release = (jdk / "release").read_text()
    if 'JAVA_VERSION="21.' not in release:
        raise ValueError("Use a JDK21 build matching the target OS and architecture")
    if lock["javaRuntimeVersion"] not in release or 'IMPLEMENTOR="Eclipse Adoptium"' not in release:
        raise ValueError("JDK and locked complete runtime source must match")
    suffix = ".exe" if (jdk / "bin/java.exe").exists() else ""
    output.mkdir(parents=True, exist_ok=True)
    (output / "lib").mkdir(exist_ok=True)
    notices = output / "licenses"
    notices.mkdir(exist_ok=True)
    for name, expected in lock["jars"].items():
        source = libraries / name
        if sha(source) != expected:
            raise ValueError("MPXJ dependency differs from lock: " + name)
        if source.resolve() != (output / "lib" / name).resolve():
            shutil.copyfile(source, output / "lib" / name)
        with zipfile.ZipFile(source) as archive:
            for item in archive.namelist():
                if item.startswith("META-INF/") and item.rsplit("/", 1)[-1].upper().startswith(("LICENSE", "NOTICE")) and not item.endswith("/"):
                    (notices / (name + "-" + item.replace("/", "_"))).write_bytes(archive.read(item))
    shutil.copyfile(ROOT / "tests/fixtures/mpp-public/LICENSE-MPXJ.txt", notices / "MPXJ-LGPL-2.1.txt")
    shutil.copyfile(ROOT / "tools/mpp-reader/MppBridge.java", output / "MppBridge.java")
    shutil.copyfile(ROOT / "packaging/mpp-reader/NOTICE.txt", output / "NOTICE.txt")
    with tempfile.TemporaryDirectory(prefix="ppm-mpp-build-") as temporary:
        classes = Path(temporary)
        subprocess.run([str(jdk / ("bin/javac" + suffix)), "--release", "21", "-encoding", "UTF-8", "-cp",
                        str(output / "lib/*"), "-d", str(classes), str(output / "MppBridge.java")], check=True)
        subprocess.run([str(jdk / ("bin/jar" + suffix)), "--create", "--file", str(output / "bridge.jar"),
                        "-C", str(classes), "."], check=True)
    runtime = output / "runtime"
    if not runtime.exists():
        subprocess.run([str(jdk / ("bin/jlink" + suffix)), "--add-modules",
                        "java.base,java.desktop,java.logging,java.xml,java.sql,jdk.unsupported,jdk.charsets",
                        "--strip-debug", "--no-header-files", "--no-man-pages", "--output", str(runtime)], check=True)
    (output / "sources").mkdir(exist_ok=True)
    for item in lock["sources"]:
        source = sources / item["name"]
        if sha(source) != item["sha256"]:
            raise ValueError("Corresponding source differs from lock: " + item["name"])
        if source.resolve() != (output / "sources" / item["name"]).resolve():
            shutil.copyfile(source, output / "sources" / item["name"])
    manifest = {"protocol": 2, "engine": "MPXJ", "engineVersion": "16.10.0", "javaMajor": 21,
                "javaRelease": release, "upstream": lock["upstream"], "sources": lock["sources"],
                "files": {p.relative_to(output).as_posix(): sha(p) for p in sorted(output.rglob("*"))
                          if p.is_file() and p.name != "manifest.json"}}
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return {"output": str(output), "files": len(manifest["files"]), "engineVersion": "16.10.0"}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--jdk", required=True, type=Path)
    parser.add_argument("--libraries", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--sources", required=True, type=Path, help="Complete OpenJDK and MPXJ source archives from engine-lock.json")
    args = parser.parse_args()
    print(json.dumps(build(args.jdk.resolve(), args.libraries.resolve(), args.output.resolve(), args.sources.resolve()), indent=2))
