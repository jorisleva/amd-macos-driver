"""GPU-free packaging/surface regressions; C++ lifecycle tests run in the builder."""
import copy
import importlib.util
from pathlib import Path
import plistlib
import shutil
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('pci_probe_audit', ROOT / 'tools/pci_probe_audit.py')
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)
SOURCE = ROOT / 'kexts/Navi48PciProbe'


class PciProbeSurface(unittest.TestCase):
    def test_current_source_and_single_personality(self):
        report = audit.audit_sources(SOURCE)
        self.assertEqual(report['personality_count'], 1)
        self.assertEqual(report['provider_operations'], ['copyProperty'])

    def test_foreign_personality_refused(self):
        info = plistlib.loads((SOURCE / 'Info.plist').read_bytes())
        info['IOKitPersonalities']['AppleExperiment'] = {}
        with self.assertRaises(ValueError):
            audit.check_plist(info)

    def test_broader_identity_or_graphics_category_refused(self):
        original = plistlib.loads((SOURCE / 'Info.plist').read_bytes())
        for key, value in (('IOPCIPrimaryMatch', '0x75501002 0x75511002'),
                           ('IOPCISecondaryMatch', '0x54171849&0x0000ffff'),
                           ('IOMatchCategory', 'IOAccelerator'),
                           ('IOProviderClass', 'IOService'),
                           ('IOUserClientClass', 'IOUserClient')):
            with self.subTest(key=key):
                info = copy.deepcopy(original)
                info['IOKitPersonalities'][audit.PRODUCT][key] = value
                with self.assertRaises(ValueError):
                    audit.check_plist(info)

    def test_unexpected_bundle_dependencies_refused(self):
        info = plistlib.loads((SOURCE / 'Info.plist').read_bytes())
        info['OSBundleLibraries']['com.apple.kext.AMDRadeonX6000'] = '1.0'
        with self.assertRaises(ValueError):
            audit.check_plist(info)

    def mutate(self, filename, old, new):
        with tempfile.TemporaryDirectory() as tmp:
            target = Path(tmp) / 'source'
            shutil.copytree(SOURCE, target)
            path = target / filename
            text = path.read_text()
            self.assertIn(old, text)
            path.write_text(text.replace(old, new, 1))
            with self.assertRaises(ValueError):
                audit.audit_sources(target)

    def test_hardware_calls_and_service_publication_refused(self):
        for statement in ('OSDynamicCast(IOPCIDevice, provider)->setMemoryEnable(true);',
                          'OSDynamicCast(IOPCIDevice, provider)->configRead32(0);',
                          'provider->mapDeviceMemoryWithIndex(0);',
                          'registerService();',
                          'provider->setProperty("LoadAccelerator", true);'):
            with self.subTest(statement=statement):
                self.mutate('Navi48PciProbe.cpp', 'n48pci::Snapshot snapshot{};',
                            'n48pci::Snapshot snapshot{}; ' + statement)

    def test_extra_include_refused(self):
        self.mutate('Navi48PciProbe.cpp', '#include "ProbePolicy.hpp"',
                    '#include "ProbePolicy.hpp"\n#include "../Navi48Bringup/psp.h"')

    def test_user_client_success_refused(self):
        self.mutate('Navi48PciProbe.cpp', 'return kIOReturnUnsupported;', 'return 0;')

    def test_kmod_identity_mismatch_refused(self):
        self.mutate('kmod_info.c', '"0.1.0"', '"0.1.1"')

    def test_pci_import_is_type_metadata_only(self):
        baseline = '_PE_parse_boot_argn\nIOPCIDevice::metaClass\nIOService::open(IOService*, unsigned int, void*)\n'
        # Inherited vtable entries are not calls. A PCI member function is different.
        self.assertEqual(audit.audit_imports(baseline)['direct_pci_imports'], ['IOPCIDevice::metaClass'])
        for extra in ('IOPCIDevice::configRead32(unsigned int)', 'IOPCIDevice::setMemoryEnable(bool)',
                      'IOPCIDevice::metaClassBad', '_fw_smu_14_0_3', 'Navi48PciProbe::missing()',
                      '_ml_io_write32', '_IOMalloc'):
            with self.subTest(extra=extra), self.assertRaises(ValueError):
                audit.audit_imports(baseline + extra + '\n')

    def test_missing_boot_or_type_guard_imports_refused(self):
        for imports in ('', 'IOPCIDevice::metaClass\n', '_PE_parse_boot_argn\n'):
            with self.subTest(imports=imports), self.assertRaises(ValueError):
                audit.audit_imports(imports)


if __name__ == '__main__':
    unittest.main()
