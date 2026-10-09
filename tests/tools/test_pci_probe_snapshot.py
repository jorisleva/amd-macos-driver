import copy
import json
from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from pci_probe_snapshot import compare_snapshot, unsigned

PROVIDER = json.loads((ROOT / 'tests/pci-probe/target-registry.json').read_text())['properties_hex']
OBSERVED = json.loads((ROOT / 'tests/tools/fixtures/pci-probe-on-snapshot.json').read_text())['snapshot']
RESOURCES = ('BAR0', 'BAR2', 'BAR4', 'BAR5', 'ROM')


class SnapshotTests(unittest.TestCase):
    def test_actual_signed_xml_snapshot(self):
        result = compare_snapshot(OBSERVED, PROVIDER)
        self.assertTrue(result['matched'])
        self.assertEqual(result['signed_xml_fields_normalized'], sorted(k + '.RegistryFlags' for k in RESOURCES))
        self.assertEqual(result['normalized_snapshot']['BAR0']['RegistryFlags'], 0xc2070010)
        self.assertEqual(result['normalized_snapshot']['BAR0']['Base'], 0x440000000)
        self.assertEqual(result['normalized_snapshot']['PCIBDF'], 0x70000)

    def test_unsigned_32_representation_also_matches(self):
        snapshot = copy.deepcopy(OBSERVED)
        for key in RESOURCES:
            snapshot[key]['RegistryFlags'] += 1 << 32
        result = compare_snapshot(snapshot, PROVIDER)
        self.assertTrue(result['matched'])
        self.assertEqual(result['signed_xml_fields_normalized'], [])

    def test_corruption_of_every_field_is_rejected(self):
        for key, value in OBSERVED.items():
            for field in value if type(value) is dict else (None,):
                snapshot = copy.deepcopy(OBSERVED)
                if field is None:
                    snapshot[key] += 1
                else:
                    snapshot[key][field] += 1
                with self.subTest(key=key, field=field), self.assertRaises(ValueError):
                    compare_snapshot(snapshot, PROVIDER)

    def test_addresses_not_truncated_to_32_bits(self):
        for key in ('BAR0', 'BAR2'):
            snapshot = copy.deepcopy(OBSERVED)
            snapshot[key]['Base'] &= 0xffffffff
            with self.assertRaises(ValueError):
                compare_snapshot(snapshot, PROVIDER)
        snapshot = copy.deepcopy(OBSERVED)
        snapshot['BAR0']['Length'] += 1 << 32
        with self.assertRaises(ValueError):
            compare_snapshot(snapshot, PROVIDER)

    def test_rejects_out_of_width_or_wrong_types(self):
        for value in (True, False, 1.0, '1', None, -(1 << 31) - 1, 1 << 32, 0xffffffffc2070010):
            with self.subTest(value=value), self.assertRaises(ValueError):
                unsigned(value, 32)
        self.assertEqual(unsigned(-1, 32), 0xffffffff)
        self.assertEqual(unsigned(-1, 64), 0xffffffffffffffff)
        for value in (-(1 << 63) - 1, 1 << 64):
            with self.assertRaises(ValueError):
                unsigned(value, 64)
        snapshot = copy.deepcopy(OBSERVED)
        snapshot['SchemaVersion'] = True
        with self.assertRaises(ValueError):
            compare_snapshot(snapshot, PROVIDER)

    def test_missing_or_extra_schema_fields(self):
        for key in OBSERVED:
            snapshot = copy.deepcopy(OBSERVED)
            del snapshot[key]
            with self.assertRaises(ValueError):
                compare_snapshot(snapshot, PROVIDER)
        snapshot = copy.deepcopy(OBSERVED)
        snapshot['BAR1'] = copy.deepcopy(snapshot['BAR0'])
        with self.assertRaises(ValueError):
            compare_snapshot(snapshot, PROVIDER)
        snapshot = copy.deepcopy(OBSERVED)
        snapshot['BAR0']['extra'] = 0
        with self.assertRaises(ValueError):
            compare_snapshot(snapshot, PROVIDER)

    def test_provider_identity_and_lengths(self):
        for key in PROVIDER:
            provider = dict(PROVIDER)
            provider[key] = '00'
            with self.assertRaises(ValueError):
                compare_snapshot(OBSERVED, provider)
        for data in ('', PROVIDER['assigned-addresses'] * 2, '00' * 20):
            provider = dict(PROVIDER, **{'assigned-addresses': data})
            with self.assertRaises(ValueError):
                compare_snapshot(OBSERVED, provider)
        provider = dict(PROVIDER, **{'device-id': '51750000'})
        with self.assertRaises(ValueError):
            compare_snapshot(OBSERVED, provider)

    def test_bad_provider_resources(self):
        original = list(struct.iter_unpack('<IIIII', bytes.fromhex(PROVIDER['assigned-addresses'])))
        mutations = []
        duplicate = list(original); duplicate[1] = duplicate[0]; mutations.append(duplicate)
        mixed = list(original); mixed[1] = (mixed[1][0] ^ 0x10000,) + mixed[1][1:]; mutations.append(mixed)
        overlap = list(original); overlap[1] = (overlap[1][0],) + original[0][1:]; mutations.append(overlap)
        overflow = list(original); overflow[0] = (overflow[0][0], 0xffffffff, 0xfffffff0, 0, 32); mutations.append(overflow)
        mutations.append(original[1:])  # missing BAR0
        for records in mutations:
            raw = b''.join(struct.pack('<IIIII', *r) for r in records)
            with self.assertRaises(ValueError):
                compare_snapshot(OBSERVED, dict(PROVIDER, **{'assigned-addresses': raw.hex()}))


if __name__ == '__main__':
    unittest.main()
