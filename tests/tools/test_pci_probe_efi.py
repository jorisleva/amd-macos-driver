"""Pure tests: no mounted-volume writes, boot edits, or kext loading."""
import copy
import importlib.util
from pathlib import Path
import plistlib
import sys
import tempfile
import unittest
from unittest.mock import patch

TOOLS = Path(__file__).resolve().parents[2] / 'tools'
sys.path.insert(0, str(TOOLS))
SPEC = importlib.util.spec_from_file_location('pci_efi', TOOLS / 'prepare-pci-probe-efi.py')
efi = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(efi)


def baseline():
    return {'Kernel': {'Add': [{'BundlePath': p, 'Enabled': True} for p in sorted(efi.BASE_KEXTS)],
                       'Force': [], 'Patch': [{'Enabled': True, 'Replace': b'cpu-patch'}]},
            'NVRAM': {'Add': {efi.GUID: {'boot-args': '-v navi48bringup=0 rdna4-off=1',
                                       'csr-active-config': b'\0\0\0\0'}},
                      'Delete': {efi.GUID: ['boot-args']}, 'WriteFlash': False},
            'Misc': {'Boot': {'LauncherOption': 'Disabled'}, 'Security': {'SecureBootModel': 'Disabled'}},
            'PlatformInfo': {'Generic': {'SystemSerialNumber': 'TEST-ONLY-NOT-A-SERIAL'}}}


class ProfileTests(unittest.TestCase):
    def test_two_profiles_preserve_source(self):
        original = baseline()
        serialized = plistlib.dumps(original)
        for enabled in (False, True):
            profile = efi.make_profile(original, enabled)
            self.assertEqual(plistlib.dumps(original), serialized)
            self.assertTrue(profile['Kernel']['Add'][-1]['Enabled'])
            self.assertEqual(profile['Kernel']['Add'][-1]['Arch'], 'x86_64')
            self.assertEqual(profile['Kernel']['Add'][-1]['MinKernel'], '25.6.0')
            self.assertEqual(profile['Kernel']['Add'][-1]['MaxKernel'], '25.6.0')
            self.assertTrue(profile['NVRAM']['Add'][efi.GUID]['boot-args'].endswith('navi48-pci-probe=' + str(int(enabled))))
            efi.validate_profile(original, plistlib.loads(plistlib.dumps(profile)), enabled)

    def test_rejects_unexpected_baseline(self):
        mutations = [
            lambda c: c['Kernel']['Add'].append({'BundlePath': 'Navi48Bringup.kext', 'Enabled': False}),
            lambda c: c['Kernel']['Add'].append(copy.deepcopy(c['Kernel']['Add'][0])),
            lambda c: c['Kernel']['Add'][0].update(Enabled=False),
            lambda c: c['Kernel']['Force'].append({'BundlePath': 'NVIDIA.kext'}),
            lambda c: c['NVRAM'].update(WriteFlash=True),
            lambda c: c['NVRAM']['Delete'][efi.GUID].clear(),
            lambda c: c['NVRAM']['Add'][efi.GUID].update({'csr-active-config': b'\x03\x08\0\0'}),
            lambda c: c['Misc']['Boot'].update(LauncherOption='Full'),
        ]
        for mutate in mutations:
            c = baseline()
            mutate(c)
            with self.assertRaises(ValueError):
                efi.make_profile(c, True)

    def test_rejects_conflicting_boot_arguments(self):
        for args in ('navi48bringup=0', 'navi48bringup=1 rdna4-off=1',
                     'navi48bringup=0 navi48bringup=1 rdna4-off=1',
                     'navi48bringup=0 rdna4-off=1 rdna4-off=0',
                     'navi48bringup=0 rdna4-off=1 navi48-pci-probe=0',
                     'navi48bringup=0 rdna4-off=1 navi48-pci-probe'):
            c = baseline()
            c['NVRAM']['Add'][efi.GUID]['boot-args'] = args
            with self.assertRaises(ValueError):
                efi.make_profile(c, False)

    def test_rejects_additional_configuration_changes(self):
        original = baseline()
        mutations = [
            lambda p: p['NVRAM'].update(WriteFlash=True),
            lambda p: p['Kernel']['Patch'].clear(),
            lambda p: p['PlatformInfo']['Generic'].update(SystemSerialNumber='CHANGED'),
            lambda p: p['Misc']['Security'].update(SecureBootModel='Default'),
            lambda p: p['Kernel']['Add'][0].update(Enabled=False),
            lambda p: p['NVRAM']['Add'][efi.GUID].update({'csr-active-config': b'\xff' * 4}),
            lambda p: p['NVRAM']['Add'][efi.GUID].update({'boot-args': '-wegnoegpu'}),
        ]
        for mutate in mutations:
            profile = efi.make_profile(original, True)
            mutate(profile)
            with self.assertRaises(ValueError):
                efi.validate_profile(original, profile, True)

    def test_rejects_injection_changes_and_type_confusion(self):
        original = baseline()
        for update in ({'Arch': 'Any'}, {'MinKernel': ''}, {'MaxKernel': ''},
                       {'ExecutablePath': 'Contents/MacOS/Navi48Bringup'},
                       {'Enabled': False}, {'Enabled': 1}, {'PlistPath': '../Info.plist'}):
            p = efi.make_profile(original, True)
            p['Kernel']['Add'][-1].update(update)
            with self.assertRaises(ValueError):
                efi.validate_profile(original, p, True)
        p = efi.make_profile(original, True)
        p['Kernel']['Add'].clear()
        with self.assertRaises(ValueError):
            efi.validate_profile(original, p, True)
        with self.assertRaises(ValueError):
            efi.make_profile(original, 'yes')

    def test_off_cannot_be_classified_as_on(self):
        c = baseline()
        with self.assertRaises(ValueError):
            efi.validate_profile(c, efi.make_profile(c, False), True)


class CopyAndGuardTests(unittest.TestCase):
    def test_copy_preserves_hidden_files_and_refuses_overwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'source'
            (source / 'OC').mkdir(parents=True)
            (source / 'OC/config.plist').write_bytes(b'private-config')
            (source / 'OC/._config.plist').write_bytes(b'appledouble')
            result = efi.copy_verified(source, root / 'destination')
            self.assertEqual(len(result), 2)
            self.assertEqual(result, efi.tree_hashes(source))
            with self.assertRaises(FileExistsError):
                efi.copy_verified(source, root / 'destination')

    def test_appledouble_written_after_main_files(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'source'
            source.mkdir()
            (source / 'config').write_bytes(b'config')
            (source / '._config').write_bytes(b'sidecar')
            real_copy = efi.shutil.copyfileobj

            def simulate_fat32(src, dst):
                real_copy(src, dst)
                target = Path(dst.name)
                if not target.name.startswith('._'):
                    target.with_name('._' + target.name).unlink(missing_ok=True)

            with patch.object(efi.shutil, 'copyfileobj', side_effect=simulate_fat32):
                result = efi.copy_verified(source, root / 'destination')
            self.assertEqual(result, efi.tree_hashes(root / 'destination'))

    def test_rejects_source_symlinks_and_empty_trees(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with self.assertRaises(ValueError):
                efi.tree_hashes(root)
            (root / 'link').symlink_to('/tmp')
            with self.assertRaises(ValueError):
                efi.copy_verified(root, root / 'destination')
            self.assertFalse((root / 'destination').exists())

    def test_stage_path_never_points_at_active_efi(self):
        info = {'MountPoint': '/Volumes/OPENCORE', 'FilesystemType': 'msdos', 'BusProtocol': 'USB', 'Internal': False}
        path = efi.stage_destination(Path('/Volumes/OPENCORE'), 'pci-probe-unit-test-nonexistent', info)
        self.assertEqual(path.parent, Path('/Volumes/OPENCORE/PROFILS-TAHOE'))
        for name in ('EFI', '../EFI', 'pci-probe-../EFI', 'pci-probe-', '/EFI'):
            with self.assertRaises(ValueError):
                efi.stage_destination(Path('/Volumes/OPENCORE'), name, info)
        for key, value in [('MountPoint', '/'), ('FilesystemType', 'apfs'), ('BusProtocol', 'PCI'), ('Internal', True)]:
            wrong = dict(info, **{key: value})
            with self.assertRaises(ValueError):
                efi.stage_destination(Path('/Volumes/OPENCORE'), 'pci-probe-test', wrong)
        with self.assertRaises(ValueError):
            efi.stage_destination(Path('/Volumes/OTHER'), 'pci-probe-test', info)


class SymbolTests(unittest.TestCase):
    def test_present_names_are_not_a_link_qualification(self):
        result = efi.symbol_check(' U _a\n U _b\n', 'Symbols for kernel: \n0000 T _a\nSymbols for pci: \n0001 D _b\n')
        self.assertEqual(result['imports_found'], 2)
        self.assertFalse(result['kernel_link_validated'])
        self.assertEqual(result['symbol_owners'], ['kernel', 'pci'])

    def test_apple_nm_undefined_names(self):
        result = efi.symbol_check('_IOLog\n__ZN11IOPCIDevice9metaClassE\n',
                                 'Symbols for kernel:\n0000 T _IOLog\n'
                                 'Symbols for pci:\n0001 S __ZN11IOPCIDevice9metaClassE\n')
        self.assertEqual(result['imports_found'], 2)
        with self.assertRaises(ValueError):
            efi.symbol_check('_missing\n', 'Symbols for kernel:\n0000 T _IOLog\n')

    def test_rejects_missing_or_undefined_exports(self):
        for imports, exports in [(' U _a', 'Symbols for k:\n0000 U _a'),
                                 (' U _a', 'Symbols for k:\n0000 T _b'),
                                 ('', 'Symbols for k:\n0000 T _a')]:
            with self.assertRaises(ValueError):
                efi.symbol_check(imports, exports)


if __name__ == '__main__':
    unittest.main()
