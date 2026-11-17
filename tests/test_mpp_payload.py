"""MPP distribution inventory, corresponding-source and platform-signing regressions."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from mpp_payload import deploy_reader, verify_reader
from unittest.mock import patch


class MppPayloadTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name) / "reader"
        files = {"bridge.jar": b"bridge", "lib/mpxj.jar": b"mpxj", "runtime/bin/java": bytes.fromhex("cffaedfe") + b"runtime",
                 "sources/mpxj.jar": b"source", "sources/openjdk.tar.gz": b"jdk source"}
        hashes = {}
        for name, data in files.items():
            p = self.root / name
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(data)
            hashes[name] = hashlib.sha256(data).hexdigest()
        self.lock = {"jars": {"mpxj.jar": hashes["lib/mpxj.jar"]},
                     "sources": [{"name": name.split("/", 1)[1], "sha256": digest}
                                 for name, digest in hashes.items() if name.startswith("sources/")]}
        self.manifest = {"protocol": 2, "engine": "MPXJ", "engineVersion": "16.10.0", "files": hashes}
        self.save()

    def save(self):
        (self.root / "manifest.json").write_text(json.dumps(self.manifest))

    def test_valid_and_create_only_copy(self):
        self.assertEqual(verify_reader(self.root, self.lock)["files"], self.manifest["files"])
        with patch("mpp_payload.verify_reader", side_effect=lambda p: verify_reader(p, self.lock)):
            destination = self.root.parent / "copy"
            deploy_reader(self.root, destination)
            self.assertEqual((destination / "sources/openjdk.tar.gz").read_bytes(), b"jdk source")
            with self.assertRaises(FileExistsError):
                deploy_reader(self.root, destination)

    def test_tampered_library(self):
        (self.root / "lib/mpxj.jar").write_bytes(b"changed")
        with self.assertRaisesRegex(ValueError, "checksum"):
            verify_reader(self.root, self.lock)

    def test_unlisted_classpath_file(self):
        (self.root / "lib/unlisted.jar").write_bytes(b"extra")
        with self.assertRaisesRegex(ValueError, "incomplete"):
            verify_reader(self.root, self.lock)

    def test_corresponding_source_required(self):
        (self.root / "sources/openjdk.tar.gz").unlink()
        self.manifest["files"].pop("sources/openjdk.tar.gz")
        self.save()
        with self.assertRaisesRegex(ValueError, "source is missing"):
            verify_reader(self.root, self.lock)

    def test_dependency_lock_survives_manifest_rewrite(self):
        (self.root / "lib/mpxj.jar").write_bytes(b"replaced")
        self.manifest["files"]["lib/mpxj.jar"] = hashlib.sha256(b"replaced").hexdigest()
        self.save()
        with self.assertRaisesRegex(ValueError, "differs from lock"):
            verify_reader(self.root, self.lock)

    def test_protocol_version(self):
        self.manifest["protocol"] = 3
        self.save()
        with self.assertRaisesRegex(ValueError, "protocol/version"):
            verify_reader(self.root, self.lock)

    def test_platform_signing_updates_only_runtime_macho(self):
        (self.root / "runtime/bin/java").write_bytes(bytes.fromhex("cffaedfe") + b"signed runtime")
        result = verify_reader(self.root, self.lock, signing=True)
        self.assertTrue(result["platformSigned"])
        verify_reader(self.root, self.lock)
        (self.root / "bridge.jar").write_bytes(bytes.fromhex("cffaedfe") + b"not runtime")
        with self.assertRaisesRegex(ValueError, "checksum"):
            verify_reader(self.root, self.lock, signing=True)


if __name__ == "__main__":
    unittest.main()
