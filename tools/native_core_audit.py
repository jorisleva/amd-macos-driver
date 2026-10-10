"""Static checks for the first, non-loadable native firmware subset (Python 3.9+)."""
import difflib
import hashlib
from pathlib import Path, PurePosixPath
import re
import struct

SOURCES = (
    'ipdiscovery.cpp', 'psp.cpp', 'amd/amdgpu_discovery.cpp',
    'amd/amdgpu_ucode_extract.cpp', 'amd/fw_loader.cpp', 'amd/fw_table.cpp',
    'amd/psp_v14_0.cpp', 'amd/smu_v14_0.cpp', 'amd/imu_v12_0.cpp',
)
LOCAL_FILES = ('NativeLog.hpp', 'NativeLog.cpp', 'Preflight.hpp', 'Preflight.cpp',
               'AmdGpuAccess.hpp', 'MappedAccess.hpp', 'MappedAccess.cpp')
PLATFORM_FILES = ('IOKitController.hpp', 'IOKitController.cpp', 'AcceleratorPreflight.hpp')
# AcceleratorPreflight.hpp is copied (PLATFORM_FILES) and host-tested via the
# controller suite, but IOKitController.o must not depend on it: the audit
# requires every PLATFORM_DEPENDENCIES entry in the object .d file.
PLATFORM_DEPENDENCIES = (set(PLATFORM_FILES) - {'AcceleratorPreflight.hpp'}) | {'AmdGpuAccess.hpp', 'Preflight.hpp',
                                             'NativeLog.hpp', 'amd/amdgpu_ip.h'}
PLATFORM_IMPORTS = (
    '_IOLockAlloc', '_IOLockFree', '_IOLockLock', '_IOLockUnlock', '_PE_parse_boot_argn',
    '__ZN11IOPCIDevice19extendedConfigRead8Ey', '__ZN11IOPCIDevice20extendedConfigRead16Ey',
    '__ZN11IOPCIDevice20extendedConfigRead32Ey', '__ZN11IOPCIDevice9metaClassE',
    '__ZN15OSMetaClassBase12safeMetaCastEPKS_PK11OSMetaClass', '__ZN9IOService11getPlatformEv',
    '__ZNK9IOService10isInactiveEv', '___bzero', '_kernel_task', '_memcpy',
)
PLATFORM_SYMBOL_PREFIX = '__ZN9n48native15IOKitController'
LOG_BEFORE = '''#include "n48log.h"

// Every module's log macro funnels through the driver's own ring buffer, which
// also calls IOLog. See n48log.h for why we no longer trust macOS logging to
// still hold these lines when we go looking for them.
#define AMDGPU_LOG(tag, fmt, ...) \\
    ::amdgpu::n48_logf("Navi48Bringup: " tag ": " fmt "\\n", ##__VA_ARGS__)'''
LOG_AFTER = '''#include "NativeLog.hpp"

// Local isolation: bounded IOLog only; no legacy capture-buffer/user-client ABI
// or Navi48Bringup/Apple performance callbacks. See NativeLog.cpp.
#define AMDGPU_LOG(tag, fmt, ...) \\
    ::amdgpu::n48_logf("Navi48FirmwareCore: " tag ": " fmt "\\n", ##__VA_ARGS__)'''


def validate_manifest(manifest):
    if (manifest['schema'] != 1 or manifest['product'] != 'Navi48FirmwareCore' or
            manifest['kind'] != 'non-loadable-static-library' or
            tuple(manifest['sources']) != SOURCES):
        raise ValueError('Unreviewed compilation-unit list/product')
    if not re.fullmatch('[0-9a-f]{64}', manifest.get('patched_psp_sha256', '')):
        raise ValueError('Missing/invalid patched PSP hash')
    names = manifest['sources'] + manifest['headers']
    if len(names) != len(set(names)) or set(names) != set(manifest['upstream_sha256']):
        raise ValueError('Missing/duplicate input hash')
    for name in names:
        path = PurePosixPath(name)
        if (path.is_absolute() or '..' in path.parts or '\\' in name or
                path.as_posix() != name or re.search(r'apple|dcn|Client|Navi48Bringup|n48log', name)):
            raise ValueError('Forbidden input path: ' + name)
        if name in manifest['headers'] and path.suffix not in ('.h', '.hpp'):
            raise ValueError('Non-header in header list')
        if not re.fullmatch('[0-9a-f]{64}', manifest['upstream_sha256'][name]):
            raise ValueError('Invalid input hash')
    imports = manifest['allowed_kernel_imports']
    if len(imports) != len(set(imports)) or not imports:
        raise ValueError('Empty/duplicate import allowlist')
    platform = manifest.get('platform_controller', {})
    if (platform.get('product') != 'Navi48PlatformController' or
            platform.get('kind') != 'non-loadable-static-library' or
            platform.get('sources') != ['IOKitController.cpp'] or
            tuple(platform.get('allowed_kernel_imports', [])) != PLATFORM_IMPORTS):
        raise ValueError('Unreviewed platform-controller product/source/import list')


def isolate_logging(original):
    if original.count(LOG_BEFORE) != 1:
        raise ValueError('Upstream logging patch context changed')
    changed = original.replace(LOG_BEFORE, LOG_AFTER)
    patch = ''.join(difflib.unified_diff(original.splitlines(True), changed.splitlines(True),
                                       fromfile='a/amd/amdgpu_log.h', tofile='b/amd/amdgpu_log.h'))
    return changed, patch


def harden_access_header(original):
    if original.count('struct DeviceContext {') != 1 or original.count('namespace amdgpu {') != 1:
        raise ValueError('Upstream register interface changed')
    changed = ('// Local hardened replacement; original MIT source/NOTICE retained in upstream export.\n'
               '#pragma once\n#include "AmdGpuAccess.hpp"\n')
    patch = ''.join(difflib.unified_diff(original.splitlines(True), changed.splitlines(True),
                                       fromfile='a/amd/amdgpu_regs.h', tofile='b/amd/amdgpu_regs.h'))
    return changed, patch


def validate_psp_patch(text):
    if ([line for line in text.splitlines() if line.startswith('--- ')] != ['--- a/amd/psp_v14_0.cpp'] or
            [line for line in text.splitlines() if line.startswith('+++ ')] != ['+++ b/amd/psp_v14_0.cpp'] or
            not text.startswith('--- a/amd/psp_v14_0.cpp\n+++ b/amd/psp_v14_0.cpp\n@@ ')):
        raise ValueError('Patch must target only the selected PSP translation unit')
    for line in text.splitlines()[2:]:
        if line.startswith('@@ '):
            if not re.fullmatch(r'@@ -\d+(?:,\d+)? \+\d+(?:,\d+)? @@.*', line):
                raise ValueError('Invalid unified hunk header')
        elif not line or line[0] not in ' +-':
            raise ValueError('Only unified hunks are accepted; no additional patch metadata')


def audit_dependencies(paths, source, sdk, expected_source_files):
    """Compiler -MD dependencies: selected sources or the verified kernel SDK only."""
    seen_source = set()
    for path in paths:
        p = Path(path).resolve()
        if p.is_relative_to(source.resolve()):
            name = p.relative_to(source.resolve()).as_posix()
            if name not in expected_source_files:
                raise ValueError('Unexpected source dependency: ' + name)
            seen_source.add(name)
        elif not p.is_relative_to(sdk.resolve()):
            raise ValueError('Dependency outside isolated source/SDK: ' + str(p))
    if seen_source != set(expected_source_files):
        raise ValueError('Missing selected input in compiler dependencies')
    return sorted(seen_source)


def object_sections(data):
    """Require thin x86_64 MH_OBJECT, no dylibs or automatic init/term sections."""
    if len(data) < 32:
        raise ValueError('Truncated object')
    magic, cpu, _, kind, count, command_bytes, _, _ = struct.unpack_from('<8I', data)
    if (magic, cpu, kind) != (0xfeedfacf, 0x01000007, 1):
        raise ValueError('Expected non-loadable thin x86_64 MH_OBJECT')
    end = 32 + command_bytes
    if end > len(data):
        raise ValueError('Truncated load commands')
    offset = 32
    sections = []
    for _ in range(count):
        if offset + 8 > end:
            raise ValueError('Truncated command')
        command, size = struct.unpack_from('<II', data, offset)
        if size < 8 or size % 8 or offset + size > end:
            raise ValueError('Invalid load command')
        if command in (4, 5, 0xc, 0xd, 0xe, 0xf, 0x11, 0x1a, 0x80000018,
                       0x8000001f, 0x20, 0x80000023, 0x80000028, 0x80000035):
            raise ValueError('Dylib/dynamic loader/entry/initializer command in object')
        if command == 0x19:
            if size < 72:
                raise ValueError('Truncated segment')
            nsects = struct.unpack_from('<I', data, offset + 64)[0]
            if size != 72 + 80 * nsects:
                raise ValueError('Invalid section count')
            for i in range(nsects):
                start = offset + 72 + 80 * i
                name = data[start:start + 16].split(b'\0')[0].decode('ascii')
                address, length, fileoff = struct.unpack_from('<QQI', data, start + 32)
                flags = struct.unpack_from('<I', data, start + 64)[0]
                if name in ('__mod_init_func', '__mod_term_func', '__init_offsets') or flags & 0xff in (9, 10, 0x16):
                    raise ValueError('Automatic constructor/destructor in object')
                if flags & 0xff not in (1, 0xc, 0x12) and length:
                    if fileoff < end or fileoff + length > len(data):
                        raise ValueError('Section outside file bytes')
                    sections.append((address, fileoff, length))
        offset += size
    if offset != end or not sections:
        raise ValueError('Inconsistent/empty object load commands')
    return sections


def audit_symbols(undefined, defined, allowed):
    unknown = set(undefined) - set(allowed)
    if unknown:
        raise ValueError('Unreviewed/unresolved import: ' + ', '.join(sorted(unknown)))
    if not {'_IOLog', '_vsnprintf', '_IOSleep'}.issubset(undefined):
        raise ValueError('Missing expected firmware/logging imports')
    if any(s in {'_start', '_stop', '__realmain', '_kmod_info'} or
           re.search(r'Navi48Bringup|AppleHardware|AmdTtl|Navi48.*Client|n48dcn', s) for s in defined):
        raise ValueError('Driver entry/Apple or user-client symbol in core')
    if not any(s.startswith('_fw_') for s in defined):
        raise ValueError('Missing embedded firmware symbols')
    return {'kernel_import_count': len(set(undefined)), 'imports': sorted(set(undefined)),
            'internal_symbols_unresolved': False, 'kernel_link_validated': False}


def audit_platform_symbols(undefined, defined, allowed):
    """The IOKit controller is separate: never broaden the firmware import boundary."""
    if tuple(allowed) != PLATFORM_IMPORTS:
        raise ValueError('Unreviewed platform import allowlist')
    unknown = set(undefined) - set(allowed)
    if unknown:
        raise ValueError('Unreviewed/unresolved platform import: ' + ', '.join(sorted(unknown)))
    required = set(PLATFORM_IMPORTS) - {'___bzero', '_memcpy'}
    if not required.issubset(undefined):
        raise ValueError('Missing actual IOKit config/mapping-controller imports')
    methods = ('7acquireEP9IOServiceS2_RKNS_15PlatformRequestE', '7releaseEv', '8snapshotEv', '10revalidateEv')
    if (not all(PLATFORM_SYMBOL_PREFIX + method in defined for method in methods) or
            any(not symbol.startswith(PLATFORM_SYMBOL_PREFIX) for symbol in defined)):
        raise ValueError('Missing controller method or unexpected platform entry/firmware/client symbol')
    return {'kernel_import_count': len(set(undefined)), 'imports': sorted(set(undefined)),
            'internal_symbols_unresolved': False, 'kernel_link_validated': False,
            'hardware_authorized': False}


def verify_firmware_bytes(data, sections, listing, firmware_dir, expected, shader_dir=None):
    symbols = {}
    for line in listing.splitlines():
        fields = line.split()
        if len(fields) == 3 and fields[2].startswith('_fw_'):
            if fields[2] in symbols:
                raise ValueError('Duplicate firmware symbol')
            symbols[fields[2]] = int(fields[0], 16)
    wanted = {'_fw_' + Path(name).stem + suffix for name in expected for suffix in ('', '_size')}
    shader_names = ('store_magic', 'store_magic_hsa') if shader_dir is not None else ()
    wanted |= {'_fw_shader_' + name + suffix for name in shader_names for suffix in ('', '_size')}
    if set(symbols) != wanted:
        raise ValueError('Missing/extra firmware; no optional private Apple blobs accepted')

    def read(name, length):
        address = symbols[name]
        for base, offset, size in sections:
            if base <= address and address - base <= size and length <= size - (address - base):
                start = offset + address - base
                return data[start:start + length]
        raise ValueError('Firmware symbol is not backed by file bytes')

    result = {}
    for filename, expected_hash in expected.items():
        name = '_fw_' + Path(filename).stem
        source = (Path(firmware_dir) / filename).read_bytes()
        size = struct.unpack('<Q', read(name + '_size', 8))[0]
        if size != len(source):
            raise ValueError('Embedded firmware size mismatch')
        actual = read(name, size)
        if actual != source or hashlib.sha256(actual).hexdigest() != expected_hash:
            raise ValueError('Embedded firmware hash/bytes mismatch')
        result[filename] = {'bytes': size, 'sha256': expected_hash}
    for stem in shader_names:
        source = (Path(shader_dir) / (stem + '.bin')).read_bytes()
        name = '_fw_shader_' + stem
        size = struct.unpack('<Q', read(name + '_size', 8))[0]
        if size != len(source) or read(name, size) != source:
            raise ValueError('Embedded compute shader mismatch: ' + stem)
        result['shaders/' + stem + '.bin'] = {'bytes': size, 'sha256': hashlib.sha256(source).hexdigest()}
    return result
