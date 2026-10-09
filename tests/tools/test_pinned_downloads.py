"""Offline regressions for dependency verification, including Python 3.9 hosts."""
import hashlib
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('pinned_downloads', ROOT / 'tools/pinned_downloads.py')
downloads = importlib.util.module_from_spec(spec)
spec.loader.exec_module(downloads)


class PinnedDownloads(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def test_digest_without_python311_file_digest(self):
        payload = bytes(range(256)) * 8193  # Exercise more than one read chunk.
        path = self.root / 'input.bin'
        path.write_bytes(payload)
        with patch.object(hashlib, 'file_digest', create=True,
                          side_effect=AssertionError('Python 3.11-only API used')):
            self.assertEqual(downloads.digest(path), hashlib.sha256(payload).hexdigest())

    def test_empty_digest(self):
        path = self.root / 'empty.bin'
        path.touch()
        self.assertEqual(downloads.digest(path), hashlib.sha256(b'').hexdigest())

    def test_valid_offline_cache(self):
        payload = b'pinned dependency\x00\xff'
        (self.root / 'input.bin').write_bytes(payload)
        item = {'file': 'input.bin', 'sha256': hashlib.sha256(payload).hexdigest(),
                'url': 'https://example.invalid/input.bin'}
        self.assertEqual(downloads.fetch(item, self.root, offline=True), self.root / 'input.bin')

    def test_corrupt_offline_cache_rejected(self):
        (self.root / 'input.bin').write_bytes(b'changed')
        item = {'file': 'input.bin', 'sha256': '0' * 64,
                'url': 'https://example.invalid/input.bin'}
        with self.assertRaisesRegex(ValueError, 'Corrupt cached input'):
            downloads.fetch(item, self.root, offline=True)

    def test_missing_offline_cache_rejected(self):
        item = {'file': 'absent.bin', 'sha256': '0' * 64,
                'url': 'https://example.invalid/absent.bin'}
        with self.assertRaisesRegex(FileNotFoundError, 'Offline input missing'):
            downloads.fetch(item, self.root, offline=True)


if __name__ == '__main__':
    unittest.main()
