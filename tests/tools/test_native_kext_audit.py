"""Static audit tests, no installation, IORegistry writes or GPU commands."""
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from native_kext_audit import VERSION, audit_defined, audit_sources


class NativeKextAuditTests(unittest.TestCase):
    def test_current_diagnostic_version_identity(self):
        self.assertEqual(VERSION, '0.2.6')
        facts = audit_sources(ROOT / 'kexts/Navi48Native')
        self.assertTrue(facts['user_clients_disabled'])
        self.assertFalse(facts['apple_graphics_personalities'])

    def test_diagnostic_publisher_must_be_linked(self):
        symbols = {'__start', '__stop', '_kmod_info', '__realmain', '__antimain',
                   '__ZN12Navi48Native5startEP9IOService',
                   '__ZN12Navi48Native20recordBootDiagnosticERKNS_6ActionEi',
                   '__ZN9n48native15IOKitController7acquireEP9IOServiceS2_RKNS_15PlatformRequestE',
                   '__ZN6amdgpu8psp_initERNS_13DeviceContextERNS_10PSPContextE',
                   '__ZN10n48compute8psp_initERNS_13DeviceContextERNS_10PSPContextE',
                   '__ZN9n48native9DmaBuffer8allocateEtest'}
        self.assertTrue(audit_defined(symbols)['boot_diagnostic_publisher_linked'])
        symbols.remove('__ZN12Navi48Native20recordBootDiagnosticERKNS_6ActionEi')
        with self.assertRaisesRegex(ValueError, 'recordBootDiagnostic'):
            audit_defined(symbols)


if __name__ == '__main__':
    unittest.main()
