"""Synthetic, GPU-free regressions for the linked firmware verifier."""
import hashlib
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('navi48_binary', ROOT / 'tools/navi48_binary.py')
binary = importlib.util.module_from_spec(spec)
spec.loader.exec_module(binary)


def fixture():
    data = bytearray(512)
    struct.pack_into('<8I', data, 0, 0xfeedfacf, 0x01000007, 3, 11, 1, 72, 0, 0)
    struct.pack_into('<II16sQQQQIIII', data, 32, 0x19, 72, b'__TEXT', 0x1000,
                     1024, 0, len(data), 7, 5, 0, 0)
    return data


class MachOKext(unittest.TestCase):
    def test_segments_and_file_mapping(self):
        data = fixture()
        segments = binary.file_segments(data)
        self.assertEqual(segments, [(0x1000, 0, 512)])
        self.assertEqual(binary.at_address(data, segments, 0x1004, 8), data[4:12])

    def test_zerofill_not_treated_as_file_bytes(self):
        data = fixture()
        with self.assertRaisesRegex(ValueError, 'not backed'):
            binary.at_address(data, binary.file_segments(data), 0x1200, 8)

    def test_invalid_header(self):
        for field, value in ((0, 0), (4, 0x0100000c), (12, 2)):
            with self.subTest(field=field):
                data = fixture()
                struct.pack_into('<I', data, field, value)
                with self.assertRaisesRegex(ValueError, 'Expected'):
                    binary.file_segments(data)
        with self.assertRaisesRegex(ValueError, 'Truncated'):
            binary.file_segments(b'')

    def test_malformed_commands_and_segments(self):
        for field, value in ((36, 0), (36, 80), (16, 2), (80, 9999)):
            with self.subTest(field=field):
                data = fixture()
                struct.pack_into('<I', data, field, value)
                with self.assertRaises(ValueError):
                    binary.file_segments(data)

    def verify_fixture(self, mutation=None):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            payload = b'\x00firmware\xff'
            (root / 'sample.bin').write_bytes(payload)
            data = fixture()
            struct.pack_into('<Q', data, 112, len(payload))
            data[128:128 + len(payload)] = payload
            symbols = ['0000000000001070 S _fw_sample_size', '0000000000001080 S _fw_sample']
            for i, name in enumerate(binary.OPTIONAL_BLOBS):
                symbols.append(f'{0x1000 + 160 + 8 * i:016x} S _fw_{name}_size')
            if mutation:
                mutation(data)
            executable = root / 'kext'
            executable.write_bytes(data)
            with patch.object(binary.subprocess, 'check_output', return_value='\n'.join(symbols)):
                return binary.verify_firmwares(executable, root, {'sample.bin': hashlib.sha256(payload).hexdigest()})

    def test_valid_firmware_and_empty_optional_blobs(self):
        report = self.verify_fixture()
        self.assertEqual(report['firmwares']['sample.bin']['bytes'], 10)
        self.assertTrue(all(size == 0 for size in report['optional_blob_sizes'].values()))

    def test_corrupt_firmware_refused(self):
        with self.assertRaisesRegex(ValueError, 'bytes mismatch'):
            self.verify_fixture(lambda data: data.__setitem__(128, 1))

    def test_corrupt_size_refused(self):
        with self.assertRaisesRegex(ValueError, 'size mismatch'):
            self.verify_fixture(lambda data: struct.pack_into('<Q', data, 112, 999999))

    def test_private_shader_blob_refused(self):
        with self.assertRaisesRegex(ValueError, 'private Apple'):
            self.verify_fixture(lambda data: struct.pack_into('<Q', data, 160, 64))


if __name__ == '__main__':
    unittest.main()
