import copy
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from native_core_audit import (LOG_BEFORE, PLATFORM_IMPORTS, PLATFORM_SYMBOL_PREFIX,
                               PLATFORM_DEPENDENCIES, audit_dependencies, audit_symbols,
                               audit_platform_symbols, isolate_logging, harden_access_header,
                               validate_psp_patch, object_sections, validate_manifest, verify_firmware_bytes)


def object_image(payload=b'ABCD\0\0\0\0' + struct.pack('<Q', 4), name=b'__const', flags=0):
    # One file-backed section in one x86_64 relocatable Mach-O segment.
    header = struct.pack('<8I', 0xfeedfacf, 0x01000007, 3, 1, 1, 152, 0, 0)
    segment = struct.pack('<II16s4Q4I', 0x19, 152, b'', 0, len(payload), 184, len(payload), 7, 7, 1, 0)
    section = struct.pack('<16s16sQQ8I', name, b'__DATA', 0, len(payload), 184, 3, 0, 0, flags, 0, 0, 0)
    return header + segment + section + payload


class NativeCoreAuditTests(unittest.TestCase):
    def setUp(self):
        self.manifest = json.loads((ROOT / 'native/Navi48FirmwareCore/manifest.json').read_text())

    def test_reviewed_manifest(self):
        validate_manifest(self.manifest)
        self.assertEqual(len(self.manifest['sources']), 9)
        self.assertEqual(len(self.manifest['headers']), 15)

    def test_extra_source_and_driver_rejected(self):
        for field, value in [('sources', 'Navi48Bringup.cpp'), ('sources', 'amd/native_s1c.cpp'),
                             ('headers', 'apple/gfx_tlb83.h'), ('headers', '../escape.h'),
                             ('headers', '/tmp/foreign.h'), ('headers', 'amd\\bad.h'),
                             ('headers', 'other.cpp')]:
            with self.subTest(value=value):
                m = copy.deepcopy(self.manifest)
                m[field].append(value)
                m['upstream_sha256'][value] = 'a' * 64
                with self.assertRaises(ValueError):
                    validate_manifest(m)

    def test_missing_duplicate_and_bad_hash(self):
        for mutation in ('missing', 'duplicate', 'hash', 'product', 'kind'):
            m = copy.deepcopy(self.manifest)
            if mutation == 'missing': del m['upstream_sha256']['psp.cpp']
            elif mutation == 'duplicate': m['headers'].append(m['headers'][0])
            elif mutation == 'hash': m['upstream_sha256']['psp.cpp'] = 'xyz'
            else: m[mutation] = 'KEXT'
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                validate_manifest(m)

    def test_platform_manifest_does_not_broaden_firmware_boundary(self):
        self.assertEqual(tuple(self.manifest['platform_controller']['allowed_kernel_imports']), PLATFORM_IMPORTS)
        self.assertEqual(len(self.manifest['allowed_kernel_imports']), 13)
        for mutation in ('missing', 'source', 'import', 'product', 'kind'):
            m = copy.deepcopy(self.manifest)
            if mutation == 'missing': del m['platform_controller']
            elif mutation == 'source': m['platform_controller']['sources'].append('Navi48Bringup.cpp')
            elif mutation == 'import': m['platform_controller']['allowed_kernel_imports'].append('_configWrite32')
            else: m['platform_controller'][mutation] = 'KEXT'
            with self.subTest(mutation=mutation), self.assertRaises(ValueError): validate_manifest(m)

    def test_platform_symbol_boundary(self):
        methods = ('7acquireEP9IOServiceS2_RKNS_15PlatformRequestE', '7releaseEv', '8snapshotEv', '10revalidateEv')
        defined = [PLATFORM_SYMBOL_PREFIX + name for name in methods]
        result = audit_platform_symbols(PLATFORM_IMPORTS, defined, PLATFORM_IMPORTS)
        self.assertEqual(result['kernel_import_count'], 15)
        self.assertFalse(result['kernel_link_validated'])
        self.assertFalse(result['hardware_authorized'])
        for extra in ('_unknown', '__ZN11IOPCIDevice21extendedConfigWrite32Eyj',
                      '__ZN9n48native11bind_accessEv', '_IOLog'):
            with self.assertRaises(ValueError): audit_platform_symbols(PLATFORM_IMPORTS + (extra,), defined, PLATFORM_IMPORTS)
        for extra in ('_start', '_stop', '_kmod_info', '_fw_demo', '_Navi48NativeClient'):
            with self.assertRaises(ValueError): audit_platform_symbols(PLATFORM_IMPORTS, defined + [extra], PLATFORM_IMPORTS)
        with self.assertRaises(ValueError): audit_platform_symbols([], defined, PLATFORM_IMPORTS)
        with self.assertRaises(ValueError): audit_platform_symbols(PLATFORM_IMPORTS, defined[:-1], PLATFORM_IMPORTS)
        with self.assertRaises(ValueError): audit_platform_symbols(PLATFORM_IMPORTS, defined, PLATFORM_IMPORTS + ('_unknown',))
        with self.assertRaises(ValueError): audit_symbols(PLATFORM_IMPORTS, ['_fw_demo'], self.manifest['allowed_kernel_imports'])

    def test_platform_compiler_dependency_boundary(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d).resolve()
            source, sdk = root / 'source', root / 'sdk'
            paths = [source / p for p in PLATFORM_DEPENDENCIES] + [sdk / 'Headers/IOKit/IOService.h']
            self.assertEqual(audit_dependencies(paths, source, sdk, PLATFORM_DEPENDENCIES), sorted(PLATFORM_DEPENDENCIES))
            for extra in ('MappedAccess.cpp', 'amd/psp_v14_0.cpp', 'Navi48Bringup.cpp'):
                with self.assertRaises(ValueError): audit_dependencies(paths + [source / extra], source, sdk, PLATFORM_DEPENDENCIES)

    def test_logging_change_is_narrow_and_explicit(self):
        original = '// preserved notice\n' + LOG_BEFORE + '\n// preserved macros\n'
        changed, patch = isolate_logging(original)
        self.assertTrue(changed.startswith('// preserved notice\n'))
        self.assertTrue(changed.endswith('\n// preserved macros\n'))
        self.assertIn('#include "NativeLog.hpp"', changed)
        self.assertNotIn('#include "n48log.h"', changed)
        self.assertIn('--- a/amd/amdgpu_log.h', patch)
        self.assertIn('+++ b/amd/amdgpu_log.h', patch)
        self.assertNotIn('-// preserved', patch)
        for invalid in ('missing context', LOG_BEFORE * 2, changed):
            with self.assertRaises(ValueError): isolate_logging(invalid)

    def test_hardened_access_replacement(self):
        changed, patch = harden_access_header('namespace amdgpu {\nstruct DeviceContext {\n};\n}\n')
        self.assertIn('#include "AmdGpuAccess.hpp"', changed)
        self.assertNotIn('struct DeviceContext', changed)
        self.assertIn('--- a/amd/amdgpu_regs.h', patch)
        with self.assertRaises(ValueError): harden_access_header('unknown interface')

    def test_psp_patch_boundary_and_expected_hash(self):
        text = (ROOT / 'native/Navi48FirmwareCore/patches/0003-psp-access-errors.patch').read_text()
        validate_psp_patch(text)
        for bad in (text.replace('b/amd/psp_v14_0.cpp', 'b/../../escape.cpp'),
                    text + '\n--- a/other.cpp\n+++ b/other.cpp\n',
                    text + 'Index: ../../escape.cpp\n', text + '*** /tmp/foreign.cpp\n'):
            with self.assertRaises(ValueError): validate_psp_patch(bad)
        m = copy.deepcopy(self.manifest)
        del m['patched_psp_sha256']
        with self.assertRaises(ValueError): validate_manifest(m)

    def test_compiler_dependency_boundary(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d).resolve()
            source, sdk = root / 'source', root / 'sdk'
            valid = [source / 'psp.cpp', sdk / 'Headers/IOKit/IOLib.h']
            self.assertEqual(audit_dependencies(valid, source, sdk, {'psp.cpp'}), ['psp.cpp'])
            for foreign in (root / 'upstream/Navi48Bringup.hpp', source / 'apple/hook.hpp',
                            source / '../foreign.h'):
                with self.assertRaises(ValueError):
                    audit_dependencies(valid + [foreign], source, sdk, {'psp.cpp'})
            with self.assertRaises(ValueError): audit_dependencies([], source, sdk, {'psp.cpp'})

    def test_non_loadable_object(self):
        self.assertEqual(object_sections(object_image()), [(0, 184, 16)])
        for offset, value in ((0, 0xcafebabe), (4, 0x0100000c), (12, 11), (12, 2),
                              (20, 4096), (32, 0xc), (32, 5), (32, 0x1a), (32, 0x80000028), (36, 7),
                              (96, 2), (168, 9), (168, 10), (168, 0x16), (152, 4096)):
            data = bytearray(object_image())
            struct.pack_into('<I', data, offset, value)
            with self.subTest(offset=offset, value=value), self.assertRaises(ValueError):
                object_sections(data)
        for cut in (0, 31, 32, 100, 183, 190):
            with self.assertRaises(ValueError): object_sections(object_image()[:cut])

    def test_automatic_initializers_rejected(self):
        for name in (b'__mod_init_func', b'__mod_term_func', b'__init_offsets'):
            with self.assertRaises(ValueError): object_sections(object_image(name=name))

    def test_import_boundary(self):
        allowed = self.manifest['allowed_kernel_imports']
        result = audit_symbols(allowed, ['_fw_demo'], allowed)
        self.assertEqual(result['kernel_import_count'], 13)
        self.assertFalse(result['kernel_link_validated'])
        for symbol in ('_unknown', '__ZN6amdgpu11sysmem_allocEv', '_gN48Pf540On',
                       '__ZN11IOPCIDevice15setMemoryEnableEb'):
            with self.assertRaises(ValueError): audit_symbols(allowed + [symbol], ['_fw_demo'], allowed)
        for symbol in ('_start', '_stop', '_kmod_info', '__realmain', '_AppleHardwareHook',
                       '_Navi48NativeClient', '_Navi48Bringup'):
            with self.assertRaises(ValueError): audit_symbols(allowed, ['_fw_demo', symbol], allowed)
        with self.assertRaises(ValueError): audit_symbols([], ['_fw_demo'], allowed)
        with self.assertRaises(ValueError): audit_symbols(allowed, [], allowed)

    def test_linked_firmware_content_and_size(self):
        listing = '0000000000000000 S _fw_demo\n0000000000000008 S _fw_demo_size\n'
        expected = {'demo.bin': hashlib.sha256(b'ABCD').hexdigest()}
        data = object_image()
        with tempfile.TemporaryDirectory() as d:
            (Path(d) / 'demo.bin').write_bytes(b'ABCD')
            result = verify_firmware_bytes(data, object_sections(data), listing, d, expected)
            self.assertEqual(result['demo.bin']['bytes'], 4)
            for bad in (object_image(b'ABCE\0\0\0\0' + struct.pack('<Q', 4)),
                        object_image(b'ABCD\0\0\0\0' + struct.pack('<Q', 5)),
                        object_image(b'ABCD\0\0\0\0' + struct.pack('<Q', 0))):
                with self.assertRaises(ValueError):
                    verify_firmware_bytes(bad, object_sections(bad), listing, d, expected)
            for bad in ('', listing + '0000000000000000 S _fw_shadercache\n', listing + listing,
                        listing.replace('0000000000000000 S', '0000000000001000 S')):
                with self.assertRaises(ValueError):
                    verify_firmware_bytes(data, object_sections(data), bad, d, expected)
            with self.assertRaises(ValueError):
                verify_firmware_bytes(data, object_sections(data), listing, d, {'demo.bin': '0' * 64})


if __name__ == '__main__':
    unittest.main()
