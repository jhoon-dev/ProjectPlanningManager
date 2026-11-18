import hashlib
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from urllib.error import HTTPError

SPEC = importlib.util.spec_from_file_location(
    'prepare_ci_mpp_reader', Path(__file__).resolve().parents[1] / 'scripts/prepare-ci-mpp-reader.py')
PREPARE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PREPARE)


class LockedDownloadTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.target = Path(self.directory.name) / 'source.jar'
        self.payload = b'locked source archive'
        self.sha = hashlib.sha256(self.payload).hexdigest()

    def test_verified_cache_needs_no_network(self):
        self.target.write_bytes(self.payload)
        with patch.object(PREPARE.urllib.request, 'urlopen') as fetch:
            PREPARE.download('https://example.test/source', self.target, self.sha)
        fetch.assert_not_called()

    def test_rate_limit_retries_then_verifies_content(self):
        self.target.write_bytes(b'corrupted cache')
        limited = HTTPError('https://example.test/source', 429, 'Too Many Requests', {}, None)
        with patch.object(PREPARE.urllib.request, 'urlopen', side_effect=[limited, io.BytesIO(self.payload)]) as fetch, \
                patch.object(PREPARE.time, 'sleep') as delay:
            PREPARE.download('https://example.test/source', self.target, self.sha)
        self.assertEqual(fetch.call_count, 2)
        delay.assert_called_once_with(1)
        self.assertEqual(self.target.read_bytes(), self.payload)

    def test_permanent_http_error_is_not_retried(self):
        missing = HTTPError('https://example.test/source', 404, 'Not Found', {}, None)
        with patch.object(PREPARE.urllib.request, 'urlopen', side_effect=missing) as fetch, \
                patch.object(PREPARE.time, 'sleep') as delay:
            with self.assertRaises(HTTPError):
                PREPARE.download('https://example.test/source', self.target, self.sha)
        fetch.assert_called_once()
        delay.assert_not_called()

    def test_checksum_mismatch_never_replaces_target(self):
        self.target.write_bytes(b'previous archive')
        with patch.object(PREPARE.urllib.request, 'urlopen', return_value=io.BytesIO(b'wrong archive')):
            with self.assertRaisesRegex(ValueError, 'checksum mismatch'):
                PREPARE.download('https://example.test/source', self.target, self.sha)
        self.assertEqual(self.target.read_bytes(), b'previous archive')
        self.assertFalse(self.target.with_suffix('.jar.download').exists())


if __name__ == '__main__':
    unittest.main()
