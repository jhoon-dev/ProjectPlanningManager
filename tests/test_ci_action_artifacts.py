import importlib.util
import io
from pathlib import Path
import shutil
import tempfile
import unittest
import zipfile

spec = importlib.util.spec_from_file_location("artifact_contract", Path(__file__).parents[1] / "scripts/check-ci-action-artifacts.py")
contract = importlib.util.module_from_spec(spec)
spec.loader.exec_module(contract)


class ArtifactContractTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.addCleanup(self.folder.cleanup)
        self.root = Path(self.folder.name).resolve()
        self.source = self.root / "input"
        self.restored = self.root / "restored"
        contract.prepare(self.source)
        shutil.copytree(self.source, self.restored)

    def remove(self, path):
        self.assertEqual(path.resolve().parent, self.root)
        shutil.rmtree(path)

    def archive(self, prefix=""):
        result = io.BytesIO()
        with zipfile.ZipFile(result, "w") as archive:
            for name, data in contract.snapshot(self.source).items():
                archive.writestr(prefix + name, data)
        return result.getvalue()

    def test_zip_nested_package_unicode_and_binary_roundtrip(self):
        self.assertEqual(len(contract.verify(self.source, self.restored, self.archive())), 4)

    def test_modified_missing_and_extra_downloads_are_rejected(self):
        for change in ("modified", "missing", "extra"):
            with self.subTest(change=change):
                self.remove(self.restored)
                shutil.copytree(self.source, self.restored)
                path = self.restored / "payload.bin"
                if change == "modified":
                    path.write_bytes(b"changed")
                elif change == "missing":
                    path.unlink()
                else:
                    (self.restored / "extra.txt").write_bytes(b"extra")
                with self.assertRaises(RuntimeError):
                    contract.verify(self.source, self.restored)

    def test_changed_zip_root_is_rejected(self):
        with self.assertRaises(RuntimeError):
            contract.verify(self.source, self.restored, self.archive("unexpected-root/"))

    def test_empty_artifact_is_rejected(self):
        self.remove(self.source)
        self.remove(self.restored)
        self.source.mkdir()
        self.restored.mkdir()
        with self.assertRaises(RuntimeError):
            contract.verify(self.source, self.restored)


if __name__ == "__main__":
    unittest.main()
