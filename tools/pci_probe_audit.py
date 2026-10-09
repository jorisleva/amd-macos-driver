"""Small, fail-closed surface checks for the registry-only PCI observer.

These checks supplement the narrow-I/O host build and manual review. They are
not a formal proof of IOKit behaviour or a kernel-load qualification.
"""
from pathlib import Path
import plistlib
import re

PRODUCT = 'Navi48PciProbe'
BUNDLE_ID = 'com.amd-macos-driver.Navi48PciProbe'
SOURCE_FILES = ('Info.plist', 'Navi48PciProbe.cpp', 'Navi48PciProbe.hpp', 'ProbePolicy.hpp', 'kmod_info.c')
PROPERTY_KEYS = ('vendor-id', 'device-id', 'subsystem-vendor-id', 'subsystem-id',
                 'revision-id', 'class-code', 'assigned-addresses')
EXPECTED_PERSONALITY = {
    'CFBundleIdentifier': BUNDLE_ID, 'IOClass': PRODUCT,
    'IOProviderClass': 'IOPCIDevice', 'IOMatchCategory': PRODUCT,
    'IOPCIPrimaryMatch': '0x75501002', 'IOPCISecondaryMatch': '0x54171849',
    'IOPCIClassMatch': '0x03000000&0xffffff00', 'IOProbeScore': 0,
}
EXPECTED_LIBRARIES = {
    'com.apple.iokit.IOPCIFamily': '1.0', 'com.apple.kpi.iokit': '20.0.0',
    'com.apple.kpi.libkern': '20.0.0', 'com.apple.kpi.unsupported': '20.0.0',
}
ALLOWED_INCLUDES = {
    'Navi48PciProbe.hpp', 'ProbePolicy.hpp', 'stdint.h', 'stddef.h',
    'IOKit/IOService.h', 'IOKit/IOLib.h', 'IOKit/pci/IOPCIDevice.h',
    'libkern/c++/OSData.h', 'libkern/c++/OSDictionary.h', 'libkern/c++/OSNumber.h',
    'pexpert/pexpert.h', 'mach/mach_types.h', 'libkern/OSKextLib.h',
}
ALLOWED_MEMBER_CALLS = {'copyProperty', 'getBytesNoCopy', 'getLength', 'release', 'setObject'}
FORBIDDEN = re.compile(
    r'\b(?:configRead\w*|configWrite\w*|setMemoryEnable|setIOEnable|setBusMasterEnable|'
    r'getDeviceMemory\w*|mapDeviceMemory\w*|map|open|close|registerService|'
    r'callPlatformFunction|setPowerState|PMinit|PMstop|registerPowerDriver|'
    r'(?:enable|disable|register|unregister|cause)Interrupt|ml_io_\w+|'
    r'IOMemoryDescriptor|IOBufferMemoryDescriptor|IOMemoryMap|IOInterrupt\w*|IOWorkLoop|'
    r'IOAccel\w*|IOSurface\w*|AMDRadeon\w*|Navi48Bringup|LoadAccelerator|'
    r'volatile|asm|__asm__|reinterpret_cast)\b')


def check_plist(info):
    expected = {
        'CFBundleIdentifier': BUNDLE_ID, 'CFBundleName': PRODUCT,
        'CFBundleExecutable': PRODUCT, 'CFBundlePackageType': 'KEXT',
        'CFBundleInfoDictionaryVersion': '6.0', 'CFBundleVersion': '0.1.0',
        'CFBundleShortVersionString': '0.1.0',
        'IOKitPersonalities': {PRODUCT: EXPECTED_PERSONALITY},
        'OSBundleLibraries': EXPECTED_LIBRARIES,
    }
    if info != expected:
        raise ValueError('PCI observer plist changed: exact identity/category/dependencies required')


def audit_sources(directory):
    directory = Path(directory)
    check_plist(plistlib.loads((directory / 'Info.plist').read_bytes()))
    calls = set()
    for name in SOURCE_FILES[1:]:
        text = (directory / name).read_text()
        code = re.sub(r'/\*.*?\*/|//[^\n]*', '', text, flags=re.S)
        includes = set(re.findall(r'^\s*#include\s*[<"]([^>"]+)[>"]', code, re.M))
        if not includes <= ALLOWED_INCLUDES:
            raise ValueError('Unexpected include in ' + name)
        # Strings (including report field names) cannot authorize an operation.
        operations = re.sub(r'"(?:\\.|[^"\\])*"', '""', code)
        if FORBIDDEN.search(operations):
            raise ValueError('Hardware/service API or unsafe construct in ' + name)
        members = set(re.findall(r'->\s*(\w+)\s*\(', operations))
        if not members <= ALLOWED_MEMBER_CALLS:
            raise ValueError('Unexpected member call in ' + name)
        if any(m != 'copyProperty' for m in re.findall(r'provider\s*->\s*(\w+)\s*\(', operations)):
            raise ValueError('Provider operation is not a property copy')
        calls.update(members)
        if name == 'kmod_info.c':
            declaration = 'KMOD_EXPLICIT_DECL(' + BUNDLE_ID + ',"0.1.0",_start,_stop)'
            if declaration not in re.sub(r'\s+', '', code):
                raise ValueError('kmod identity/version must match the plist')
        if name == 'Navi48PciProbe.cpp':
            bodies = re.findall(r'IOReturn Navi48PciProbe::newUserClient\([^)]*\)\s*\{([^}]+)\}', code)
            expected = 'if(handler)*handler=nullptr;returnkIOReturnUnsupported;'
            if len(bodies) != 2 or any(re.sub(r'\s+', '', body) != expected for body in bodies):
                raise ValueError('Both user-client overloads must refuse unconditionally')
    return {'personality_count': 1, 'provider_operations': ['copyProperty'],
            'member_call_names': sorted(calls), 'kernel_sources': list(SOURCE_FILES[1:]),
            'hardware_api_scan': 'passed-not-a-formal-proof'}


def audit_imports(demangled):
    pci = []
    allowed_classes = ('IOPCIDevice::', 'IOService::', 'IORegistryEntry::', 'OSMetaClass::',
                       'OSMetaClassBase::', 'OSObject::', 'OSData::', 'OSNumber::', 'OSDictionary::')
    allowed_functions = {'_IOLog', '_PE_parse_boot_argn', '___bzero', '_memcpy', 'vtable for IOService'}
    for line in demangled.splitlines():
        if not line.startswith(allowed_classes) and line not in allowed_functions:
            raise ValueError('Unexpected kernel import: ' + line)
        if 'IOPCIDevice::' in line:
            if line.strip() != 'IOPCIDevice::metaClass':
                raise ValueError('Unexpected direct IOPCIDevice import: ' + line)
            pci.append(line.strip())
        if re.search(r'amdgpu|Navi48Bringup|Navi48Native|AMDRadeon|IOSurface|_fw_|IOInterruptEventSource', line):
            raise ValueError('Unexpected GPU/interrupt import: ' + line)
    if 'Navi48PciProbe::' in demangled:
        raise ValueError('Unresolved internal observer symbol')
    if pci != ['IOPCIDevice::metaClass'] or '_PE_parse_boot_argn' not in demangled.splitlines():
        raise ValueError('Missing provider type/boot-argument imports')
    return {'undefined_symbols': len(demangled.splitlines()),
            'direct_pci_imports': pci,
            'kernel_collection_link_validated': False,
            'note': 'Inherited IOService vtable symbols are not evidence of direct hardware calls.'}
