"""Pure profile/deployment tests; no real USB, root, kext loading or boot."""
import copy
import importlib.util
from pathlib import Path
import plistlib
import sys
import tempfile
import unittest
from unittest.mock import patch

from test_pci_probe_efi import baseline

TOOLS = Path(__file__).resolve().parents[2] / 'tools'
sys.path.insert(0, str(TOOLS))
SPEC = importlib.util.spec_from_file_location('native_efi', TOOLS / 'prepare-native-kext-efi.py')
efi = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(efi)
OFFSET, SIZE = 64 * 1024 * 1024, 24 * 1024 * 1024


class ProfileTests(unittest.TestCase):
    def test_only_native_injection_and_three_args_change(self):
        source = baseline()
        before = plistlib.dumps(source)
        result = efi.make_profile(source, OFFSET, SIZE)
        self.assertEqual(plistlib.dumps(source), before)
        self.assertEqual(result['Kernel']['Add'][-1], efi.injection_entry())
        self.assertTrue(result['NVRAM']['Add'][efi.GUID]['boot-args'].endswith(
            'navi48-native-platform=1 navi48-native-scratch-offset=0x4000000 navi48-native-scratch-bytes=0x1800000'))
        efi.validate_profile(source, plistlib.loads(plistlib.dumps(result)), OFFSET, SIZE)

    def test_bad_candidates_fail_without_modifying_source(self):
        source = baseline()
        before = plistlib.dumps(source)
        for offset, size in ((True, SIZE), (-1, SIZE), (OFFSET + 1, SIZE), (OFFSET, 4096),
                             (OFFSET, SIZE + 1), (256 * 1024 * 1024, SIZE), (1 << 64, SIZE), (OFFSET, '24')):
            with self.assertRaises(ValueError):
                efi.make_profile(source, offset, size)
        self.assertEqual(plistlib.dumps(source), before)

    def test_unexpected_baseline_rejected(self):
        for extra in ('navi48-native-platform=0', 'navi48-native-scratch-offset=0x4000000',
                      'navi48-pci-probe=0', 'rdna4-off=0'):
            source = baseline()
            source['NVRAM']['Add'][efi.GUID]['boot-args'] += ' ' + extra
            with self.assertRaises(ValueError):
                efi.make_profile(source, OFFSET, SIZE)
        source = baseline()
        source['NVRAM']['Add'][efi.GUID]['csr-active-config'] = b'\xff' * 4
        with self.assertRaises(ValueError):
            efi.make_profile(source, OFFSET, SIZE)

    def test_unrelated_changes_rejected(self):
        source = baseline()
        mutations = [lambda p: p['NVRAM'].update(WriteFlash=True),
                     lambda p: p['Kernel']['Patch'].clear(),
                     lambda p: p['PlatformInfo']['Generic'].update(SystemSerialNumber='CHANGED'),
                     lambda p: p['Misc']['Security'].update(SecureBootModel='Default'),
                     lambda p: p['Kernel']['Add'][-1].update(Enabled=1),
                     lambda p: p['Kernel']['Add'][-1].update(MaxKernel=''),
                     lambda p: p['Kernel']['Add'].append(copy.deepcopy(p['Kernel']['Add'][-1])),
                     lambda p: p['NVRAM']['Add'][efi.GUID].update({'boot-args': 'navi48-native-platform=1'})]
        for mutate in mutations:
            profile = efi.make_profile(source, OFFSET, SIZE)
            mutate(profile)
            with self.assertRaises(ValueError):
                efi.validate_profile(source, profile, OFFSET, SIZE)

    def test_trial_volume_requires_exact_identity(self):
        info = {'MountPoint': str(efi.TRIAL), 'VolumeName': 'PROBE1401', 'VolumeUUID': efi.TRIAL_UUID,
                'FilesystemType': 'msdos', 'BusProtocol': 'USB', 'Internal': False, 'WritableVolume': True}
        efi.validate_trial(efi.TRIAL, info)
        for key, value in [('MountPoint', '/'), ('VolumeName', 'OPENCORE'), ('VolumeUUID', 'OTHER'),
                           ('FilesystemType', 'apfs'), ('BusProtocol', 'PCI'), ('Internal', True),
                           ('WritableVolume', False)]:
            with self.assertRaises(ValueError):
                efi.validate_trial(efi.TRIAL, dict(info, **{key: value}))
        with self.assertRaises(ValueError):
            efi.validate_trial(Path('/Volumes/OPENCORE'), info)


class SwapTests(unittest.TestCase):
    def fixture(self, root):
        root = root.resolve() # macOS /var -> /private/var
        volume, reference, profile = root / 'usb', root / 'reference', root / 'profile'
        for folder, payload in ((volume / 'EFI', b'old-trial'), (reference, b'reference'), (profile, b'native')):
            (folder / 'OC').mkdir(parents=True)
            (folder / 'OC/config.plist').write_bytes(payload)
            (folder / 'OC/._config.plist').write_bytes(b'sidecar')
        return volume, reference, profile, efi.tree_hashes(reference), efi.tree_hashes(volume / 'EFI')

    def deploy(self, fixture, validate=lambda path: None):
        volume, reference, profile, ref_hashes, current = fixture
        with patch.object(efi.subprocess, 'run'):
            return efi.deploy_verified(profile, volume, reference, ref_hashes, current, 'native-unit-test', validate)

    def test_swap_preserves_reference_and_entire_previous_efi(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = self.fixture(Path(directory))
            volume, reference, profile, ref_hashes, current = fixture
            result = self.deploy(fixture)
            self.assertTrue(result['backup_verified'])
            self.assertEqual(efi.tree_hashes(volume / 'EFI'), efi.tree_hashes(profile))
            self.assertEqual(efi.tree_hashes(Path(result['backup'])), current)
            self.assertEqual(efi.tree_hashes(reference), ref_hashes)
            with self.assertRaises(ValueError):
                self.deploy(fixture)  # no overwrite of an existing backup

    def test_changed_efi_rejected_before_staging(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = self.fixture(Path(directory))
            (fixture[0] / 'EFI/OC/config.plist').write_bytes(b'changed')
            with self.assertRaises(ValueError):
                self.deploy(fixture)
            self.assertFalse((fixture[0] / 'EFI.NEXT-native-unit-test').exists())

    def test_second_rename_failure_restores_old_efi(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = self.fixture(Path(directory))
            original = Path.rename

            def fail_activation(path, target):
                if path.name == 'EFI.NEXT-native-unit-test':
                    raise OSError('simulated activation failure')
                return original(path, target)

            with patch.object(Path, 'rename', fail_activation), self.assertRaises(OSError):
                self.deploy(fixture)
            self.assertEqual(efi.tree_hashes(fixture[0] / 'EFI'), fixture[4])
            self.assertEqual(efi.tree_hashes(fixture[1]), fixture[3])

    def test_post_activation_validation_failure_rolls_back(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = self.fixture(Path(directory))

            def reject(path):
                if path.name == 'EFI':
                    raise ValueError('simulated validation failure')

            with self.assertRaises(ValueError):
                self.deploy(fixture, reject)
            self.assertEqual(efi.tree_hashes(fixture[0] / 'EFI'), fixture[4])
            self.assertEqual(efi.tree_hashes(fixture[1]), fixture[3])

    def test_reference_cannot_be_inside_trial_volume(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = self.fixture(Path(directory))
            volume, _, profile, _, current = fixture
            with self.assertRaises(ValueError):
                efi.deploy_verified(profile, volume, volume / 'EFI', current, current,
                                    'native-unit-test', lambda path: None)


if __name__ == '__main__':
    unittest.main()
