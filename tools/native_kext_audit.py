"""Checks for the stage-1 native kext; no installation or runtime GPU authorization."""
import plistlib
import re

PRODUCT = 'Navi48Native'
BUNDLE_ID = 'com.amd-macos-driver.' + PRODUCT
VERSION = '0.2.6'
SOURCE_FILES = ('Navi48Native.cpp', 'Navi48Native.hpp', 'DmaBuffer.cpp', 'DmaBuffer.hpp', 'kmod_info.c', 'Info.plist',
                'ExperimentalCompute.cpp', 'ExperimentalCompute.hpp', 'ComputeAccess.hpp', 'ComputeSysMem.cpp', 'ComputeSysMem.hpp', 'ComputeLog.cpp')
# AcceleratorPreflight.hpp lives in the firmware-core module (single owner)
# and is copied via CORE_HEADERS, not SOURCE_FILES: the kext dependency
# audit expects driver/ headers under kexts/Navi48Native and core/ headers
# under the verified core snapshot.
CORE_HEADERS = ('IOKitController.hpp', 'AcceleratorPreflight.hpp', 'AmdGpuAccess.hpp', 'Preflight.hpp', 'NativeLog.hpp', 'amd/amdgpu_ip.h')
LIBRARIES = {'com.apple.iokit.IOPCIFamily': '1.0', 'com.apple.kpi.iokit': '20.0.0',
             'com.apple.kpi.libkern': '20.0.0', 'com.apple.kpi.mach': '20.0.0',
             'com.apple.kpi.unsupported': '20.0.0'}


def audit_sources(root):
    info = plistlib.loads((root / 'Info.plist').read_bytes())
    for key, expected in {'CFBundleIdentifier': BUNDLE_ID, 'CFBundleExecutable': PRODUCT,
                          'CFBundleName': PRODUCT, 'CFBundlePackageType': 'KEXT',
                          'CFBundleVersion': VERSION, 'CFBundleShortVersionString': VERSION}.items():
        if info.get(key) != expected:
            raise ValueError('Wrong kext identity/version: ' + key)
    personality = {'CFBundleIdentifier': BUNDLE_ID, 'IOClass': PRODUCT, 'IOProviderClass': 'IOPCIDevice',
                   'IOMatchCategory': PRODUCT, 'IOPCIPrimaryMatch': '0x75501002',
                   'IOPCISecondaryMatch': '0x54171849', 'IOPCIClassMatch': '0x03000000&0xffffff00',
                   'IOProbeScore': 0}
    if info.get('IOKitPersonalities') != {PRODUCT: personality} or info.get('OSBundleLibraries') != LIBRARIES:
        raise ValueError('Unreviewed personality or kernel dependency list')
    source = (root / 'Navi48Native.cpp').read_text()
    bodies = re.findall(r'IOReturn Navi48Native::newUserClient\([^)]*\)\s*\{([^}]+)\}', source)
    expected = 'if(handler)*handler=nullptr;returnkIOReturnUnsupported;'
    if len(bodies) != 2 or any(re.sub(r'\s+', '', body) != expected for body in bodies):
        raise ValueError('User clients must remain disabled in stage 1')
    kmod = re.sub(r'\s+', '', (root / 'kmod_info.c').read_text())
    if 'KMOD_EXPLICIT_DECL(' + BUNDLE_ID + ',"' + VERSION + '",_start,_stop)' not in kmod:
        raise ValueError('kmod identity/version does not match plist')
    return {'personality_count': 1, 'class': PRODUCT, 'provider': 'IOPCIDevice',
            'user_clients_disabled': True, 'apple_graphics_personalities': False}


def audit_imports(demangled):
    # IOService's inherited vtable accounts for many imports. No internal AMD
    # function or framework implementation may remain unresolved at this link.
    classes = ('IOService::', 'IORegistryEntry::', 'OSMetaClass::', 'OSMetaClassBase::',
               'OSObject::', 'OSNumber::', 'OSDictionary::', 'IOCommandGate::', 'IOWorkLoop::',
               'IOBufferMemoryDescriptor::', 'IODMACommand::', 'IOMapper::', 'OSIterator::', 'IOPMrootDomain::')
    functions = {'_IOLog', '_IOSleep', '_PE_parse_boot_argn', '_IOLockAlloc', '_IOLockFree',
                 '_IOLockLock', '_IOLockUnlock', '_OSCompareAndSwapPtr', '_bzero', '___bzero',
                 '_current_thread', '_kernel_task', '_memcpy', '_memset', '_vsnprintf',
                 'operator new(unsigned long)', 'operator delete(void*)', 'vtable for IOService',
                 '_mach_absolute_time', '_absolutetime_to_nanoseconds', '_IODelay', '_IOLockTryLock', '_snprintf'}
    pci = {'IOPCIDevice::metaClass', 'IOPCIDevice::extendedConfigRead8(unsigned long long)',
           'IOPCIDevice::extendedConfigRead16(unsigned long long)',
           'IOPCIDevice::extendedConfigRead32(unsigned long long)'}
    imports = [line.strip() for line in demangled.splitlines() if line.strip()]
    unknown = [symbol for symbol in imports if not symbol.startswith(classes) and symbol not in functions | pci]
    if unknown:
        raise ValueError('Unreviewed/unresolved native kext imports: ' + ', '.join(unknown))
    if not pci.issubset(imports) or not {'_IOLog', '_IOSleep', '_PE_parse_boot_argn'}.issubset(imports):
        raise ValueError('Missing real controller/firmware imports')
    return {'undefined_symbols': len(imports), 'direct_pci_imports': sorted(pci),
            'internal_symbols_unresolved': False, 'kernel_collection_link_validated': False}


def audit_defined(symbols):
    required = {'__start', '__stop', '_kmod_info', '__realmain', '__antimain',
                '__ZN12Navi48Native5startEP9IOService',
                '__ZN12Navi48Native20recordBootDiagnosticERKNS_6ActionEi',
                '__ZN9n48native15IOKitController7acquireEP9IOServiceS2_RKNS_15PlatformRequestE',
                '__ZN6amdgpu8psp_initERNS_13DeviceContextERNS_10PSPContextE',
                '__ZN10n48compute8psp_initERNS_13DeviceContextERNS_10PSPContextE'}
    if not any(symbol.startswith('__ZN9n48native9DmaBuffer8allocateE') for symbol in symbols):
        raise ValueError('Missing real DMA allocator implementation')
    if not required.issubset(symbols):
        raise ValueError('Missing native entry/controller/firmware symbols: ' + ', '.join(sorted(required - set(symbols))))
    forbidden = r'Navi48Bringup|NVRM|NVDA|AppleHardware|AmdTtl|n48dcn|Navi48NativeClient'
    if any(re.search(forbidden, symbol) for symbol in symbols):
        raise ValueError('Legacy graphics hook/client included in native kext')
    return {'kmod_entry_points': True, 'native_ioservice': True,
            'platform_controller_linked': True, 'psp_core_linked': True,
            'dma_allocator_linked': True, 'native_compute_engines_linked': True,
            'boot_diagnostic_publisher_linked': True, 'legacy_hooks': False}
