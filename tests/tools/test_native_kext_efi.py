"""Pure profile/deployment tests; no real USB, root, kext loading or boot."""
import copy
import importlib.util
from pathlib import Path
import plistlib
import sys
import tempfile
import unittest
from unittest.mock import patch

from contextlib import contextmanager
from test_pci_probe_efi import baseline


@contextmanager
def efi_version(version):
    previous = efi.VERSION
    efi.VERSION = version
    try:
        yield
    finally:
        efi.VERSION = previous

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

    def test_compute_profile_is_explicit_and_exact(self):
        source = baseline()
        result = efi.make_profile(source, OFFSET, 64 * 1024 * 1024, compute=True)
        efi.validate_profile(source, result, OFFSET, 64 * 1024 * 1024, compute=True)
        self.assertTrue(result['NVRAM']['Add'][efi.GUID]['boot-args'].endswith(
            'navi48-native-compute=1 navi48-native-risk=1'))
        with self.assertRaises(ValueError):
            efi.validate_profile(source, result, OFFSET, 64 * 1024 * 1024)
        for offset, size in ((OFFSET, SIZE), (OFFSET + 65536, 64 * 1024 * 1024)):
            with self.assertRaises(ValueError):
                efi.make_profile(source, offset, size, compute=True)
        for key in ('navi48-native-compute=1', 'navi48-native-risk=1'):
            changed = copy.deepcopy(result)
            changed['NVRAM']['Add'][efi.GUID]['boot-args'] = changed['NVRAM']['Add'][efi.GUID]['boot-args'].replace(key, '')
            with self.assertRaises(ValueError):
                efi.validate_profile(source, changed, OFFSET, 64 * 1024 * 1024, compute=True)

    def test_previous_native_profile_requires_exact_version_and_shape(self):
        source = baseline()
        old = efi.make_profile(source, OFFSET, SIZE)
        old['Kernel']['Add'][-1] = efi.injection_entry(version='0.1.2')
        efi.validate_profile(source, old, OFFSET, SIZE, version='0.1.2')
        with self.assertRaises(ValueError):
            efi.validate_profile(source, old, OFFSET, SIZE)
        old['Kernel']['Add'][-1]['Enabled'] = False
        with self.assertRaises(ValueError):
            efi.validate_profile(source, old, OFFSET, SIZE, version='0.1.2')

    def test_previous_compute_trials_require_profile_manifest_and_pinned_binary(self):
        source = baseline()
        binaries = {'0.2.0': '34ce08473ac2acedd0a9094420cbd4ebf159bf48b21a0134095715cfd95809f9',
                    '0.2.1': '441140b8f86095f0bd06e7c77d61f90ae20518e0fa40171e35dd270bd49e01da',
                    '0.2.2': '46fe9e475150d275dde9986a98591dc5acd92b20ff506b6853e35cfe0a5de62a',
                    '0.2.3': '61fc8ffc684fce66d8898db4f2b8dd7143b6dea288ecb25d7f1de613c1a4eeec',
                    '0.2.4': '9fe464d7cffac6c0cdd2b7dcf77244cad96d05d1c726b2059f8fc0fead2bef9e',
                    '0.2.5': 'c1e6c4bb565380bb4a7585a212de6b4e90dca4320c532bebb467716ec403ddd0',
                    '0.2.6': 'efcf2e8f823c3be7485a536f6819bfe10cf686d243b207bcacb8c7a361da67c0',
                    '0.2.7': '2409a00710dd276ff8c19848e568fca04f0793326294ef9da70af8a6f116ec37',
                    '0.2.8': '8871d7f167de34ce98d14780c62d3608d9d7530f3a7bc7eaa59bc468e4f8c0bd',
                    '0.2.9': '0bada18712b5bd2e31424ac40ea3996a4d70e277020d4e906a9bad1b4be09d07',
                    '0.2.10': '1ab98748fec072e3420aae1c7384d177900807588d1a53f7edd08d716ca43502'}
        # NOTE: the pinned hash is the DEPLOYED source EFI's binary (what we
        # replace), not the new build's (already pinned by VERSION + report).
        successors = {'0.2.0': '0.2.1', '0.2.1': '0.2.2', '0.2.2': '0.2.3', '0.2.3': '0.2.4',
                      '0.2.4': '0.2.5', '0.2.5': '0.2.6', '0.2.6': '0.2.7', '0.2.7': '0.2.8', '0.2.8': '0.2.9', '0.2.9': '0.2.10', '0.2.10': '0.2.11'}
        for version, binary in binaries.items():
            with self.subTest(version=version):
                old = efi.make_profile(source, OFFSET, 64 * 1024 * 1024, compute=True)
                old['Kernel']['Add'][-1] = efi.injection_entry(compute=True, version=version)
                key = 'OC/Kexts/Navi48Native.kext/Contents/MacOS/Navi48Native'
                hashes = {'OC/config.plist': 'config', key: binary}
                with efi_version(successors[version]):
                    efi.validate_previous_compute_trial(source, old, hashes, hashes.copy(), version)
        old = efi.make_profile(source, OFFSET, 64 * 1024 * 1024, compute=True)
        old['Kernel']['Add'][-1] = efi.injection_entry(compute=True, version='0.2.0')
        key = 'OC/Kexts/Navi48Native.kext/Contents/MacOS/Navi48Native'
        hashes = {'OC/config.plist': 'config', key:
                  '34ce08473ac2acedd0a9094420cbd4ebf159bf48b21a0134095715cfd95809f9'}
        with efi_version('0.2.1'):
            efi.validate_previous_compute_0_2_0(source, old, hashes, hashes.copy())
        for changed in (dict(hashes, extra='file'), {'OC/config.plist': 'config'}, dict(hashes, **{key: 'modified'})):
            with self.assertRaises(ValueError):
                efi.validate_previous_compute_0_2_0(source, old, changed, hashes)
        modified = dict(hashes, **{key: 'modified'})
        with self.assertRaises(ValueError): # even replacing BOTH manifests cannot change the pinned binary
            efi.validate_previous_compute_0_2_0(source, old, modified, modified.copy())
        for mutation in ('version', 'scratch', 'security'):
            changed = copy.deepcopy(old)
            if mutation == 'version': changed['Kernel']['Add'][-1] = efi.injection_entry(compute=True)
            elif mutation == 'scratch': changed['NVRAM']['Add'][efi.GUID]['boot-args'] += ' extra=1'
            else: changed['Misc']['Security']['SecureBootModel'] = 'Default'
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                efi.validate_previous_compute_0_2_0(source, changed, hashes, hashes)

    def test_shell_delta_requires_compute_and_is_exact(self):
        source = baseline()
        result = efi.make_profile(source, OFFSET, 64 * 1024 * 1024, compute=True, shell=True)
        efi.validate_profile(source, result, OFFSET, 64 * 1024 * 1024, compute=True, shell=True)
        self.assertTrue(result['NVRAM']['Add'][efi.GUID]['boot-args'].endswith('navi48-native-shell=1'))
        with self.assertRaises(ValueError):
            efi.validate_profile(source, result, OFFSET, 64 * 1024 * 1024, compute=True)
        with self.assertRaises(ValueError):
            efi.boot_delta(OFFSET, 64 * 1024 * 1024, compute=False, shell=True)
        with self.assertRaises(ValueError):
            efi.make_profile(source, OFFSET, 64 * 1024 * 1024, shell=True)

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


class BootSessionTests(unittest.TestCase):
    modules = {'0.2.0': efi.BUNDLE_ID + ' (0.2.0) AB1F0B3A-865C-33FC-BC6B-5CA0056BBF45',
               '0.2.1': efi.BUNDLE_ID + ' (0.2.1) 62893962-8984-3EE0-B636-40E33C9F073D',
               '0.2.2': efi.BUNDLE_ID + ' (0.2.2) E88B08E7-29B2-3BF1-A836-D7270C52B6AC',
               '0.2.3': efi.BUNDLE_ID + ' (0.2.3) 1E9DDB5A-0841-3DE1-9999-0D854E226E14',
               '0.2.4': efi.BUNDLE_ID + ' (0.2.4) 4F894D5F-FDC9-384C-8688-C6B4BAA68674',
               '0.2.5': efi.BUNDLE_ID + ' (0.2.5) 160033C1-04F7-3DE5-A795-A511A749F8F5',
               '0.2.6': efi.BUNDLE_ID + ' (0.2.6) BAD9777A-7C72-338A-A6B8-01FA4A3D28C3',
               '0.2.7': efi.BUNDLE_ID + ' (0.2.7) 84064BBD-DD39-3B69-9F7E-22863BCFFEAE',
               '0.2.8': efi.BUNDLE_ID + ' (0.2.8) DBD6962B-7594-3B64-9B1A-BAAEAFC07D03',
               '0.2.9': efi.BUNDLE_ID + ' (0.2.9) 809701A2-4A08-39F7-AA30-9CE65ECE4C7B',
               '0.2.10': efi.BUNDLE_ID + ' (0.2.10) C9F3A729-7910-351B-9F27-22996B70461B'}
    def setUp(self):
        self.source = baseline()
        self.args = (self.source['NVRAM']['Add'][efi.GUID]['boot-args'] +
                     efi.boot_delta(OFFSET, 64 * 1024 * 1024, compute=True)).split()
        self.loaded = self.modules['0.2.0']

    def validate(self, source='0.2.0', **kw):
        parameters = {'baseline': self.source, 'boot_args': self.args,
                      'allow_retired': True, 'native_service': '', 'native_instances': 0,
                      'retired_source': source}
        if 'loaded' not in kw:
            kw['loaded'] = self.modules[source]
        parameters.update(kw)
        return efi.validate_boot_session(**parameters)

    def test_reference_boot_keeps_existing_no_experiment_rule(self):
        args = self.source['NVRAM']['Add'][efi.GUID]['boot-args'].split()
        self.assertFalse(self.validate(boot_args=args, loaded='', allow_retired=False))

    def test_exact_retired_compute_boot_allows_only_offline_update(self):
        for source in ('0.2.0', '0.2.1', '0.2.2', '0.2.3', '0.2.4', '0.2.5', '0.2.6', '0.2.7', '0.2.8', '0.2.9', '0.2.10'):
            with self.subTest(source=source):
                self.assertTrue(self.validate(source))
                with self.assertRaises(ValueError):
                    self.validate(source, allow_retired=False)
                other = {'0.2.0': '0.2.1', '0.2.1': '0.2.2', '0.2.2': '0.2.3', '0.2.3': '0.2.4',
                         '0.2.4': '0.2.5', '0.2.5': '0.2.6', '0.2.6': '0.2.7', '0.2.7': '0.2.8', '0.2.8': '0.2.9', '0.2.9': '0.2.10', '0.2.10': '0.2.0'}[source]
                with self.assertRaises(ValueError): # a retired boot never authorizes another source
                    self.validate(other, loaded=self.modules[source])

    def test_existing_or_detached_native_instance_rejected(self):
        for changes in ({'native_service': 'Navi48Native <class Navi48Native>'}, {'native_instances': 1},
                        {'native_instances': None}, {'native_instances': False}):
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                self.validate(**changes)

    def test_wrong_or_other_loaded_experiment_rejected(self):
        for loaded in ('', self.modules['0.2.1'], self.loaded.replace('AB1F', 'AB2F'),
                       self.loaded + '\n' + efi.helpers.BUNDLE_ID, self.loaded + '\ncom.navi48.bringup',
                       self.loaded + '\n' + self.loaded):
            with self.subTest(loaded=loaded), self.assertRaises(ValueError):
                self.validate('0.2.0', loaded=loaded)

    def test_any_changed_boot_parameters_rejected(self):
        for args in (self.args[:-1], self.args + ['extra=1'],
                     [t.replace('risk=1', 'risk=0') for t in self.args],
                     [t.replace('bytes=0x4000000', 'bytes=0x1800000') for t in self.args]):
            with self.subTest(args=args), self.assertRaises(ValueError):
                self.validate(boot_args=args)


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

class RetainedCopyTests(unittest.TestCase):
    def test_retained_boot_requires_forced_flag_and_counts_instances(self):
        source = baseline()
        args = (source['NVRAM']['Add'][efi.GUID]['boot-args'] +
                efi.boot_delta(OFFSET, 64 * 1024 * 1024, compute=True)).split()
        loaded = efi.BUNDLE_ID + ' (0.2.8) DBD6962B-7594-3B64-9B1A-BAAEAFC07D03'
        service = 'Navi48Native <class Navi48Native>'
        # Sans le flag, un service vivant refuse.
        with self.assertRaises(ValueError):
            efi.validate_boot_session(source, args, loaded, True, service, 1, '0.2.8')
        # Avec le flag, tolere mais marque 'retained-forced'.
        self.assertEqual(
            efi.validate_boot_session(source, args, loaded, True, service, 1, '0.2.8', True),
            'retained-forced')
        # Compteur non entier refuse meme avec le flag.
        with self.assertRaises(ValueError):
            efi.validate_boot_session(source, args, loaded, True, service, None, '0.2.8', True)
