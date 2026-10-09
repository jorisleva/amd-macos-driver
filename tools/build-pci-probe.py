#!/usr/bin/env python3
"""Build/test the independent registry-only PCI observer; never install or load.

Python 3.9+, macOS Command Line Tools and the cached pinned MacKernelSDK archive.
--live-registry replays only sanitized IORegistry properties through the host
build; it does NOT load the kext, open a GPU client or touch PCI registers.
"""
import argparse
import datetime
import json
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import sys

from navi48_binary import file_segments
from pci_probe_audit import PRODUCT, PROPERTY_KEYS, SOURCE_FILES, audit_imports, audit_sources
from pinned_downloads import digest, extract_zip, fetch, local_output

ROOT = Path(__file__).resolve().parents[1]


def run(command, log, cwd=None, timeout=180):
    with log.open('w') as stream:
        stream.write('Command: ' + repr([str(v) for v in command]) + '\n')
        stream.flush()
        subprocess.run([str(v) for v in command], cwd=cwd, stdout=stream,
                       stderr=subprocess.STDOUT, timeout=timeout, check=True)


def live_properties():
    raw = subprocess.check_output(['ioreg', '-a', '-r', '-c', 'IOPCIDevice'], timeout=30)
    found = {}

    def walk(nodes):
        for node in nodes:
            yield node
            yield from walk(node.get('IORegistryEntryChildren', []))

    for node in walk(plistlib.loads(raw)):
        if node.get('vendor-id') == bytes.fromhex('02100000') and node.get('device-id') == bytes.fromhex('50750000'):
            found[node['IORegistryEntryID']] = node
    if len(found) != 1:
        raise ValueError('Expected exactly one 1002:7550 registry entry')
    node = next(iter(found.values()))
    return {key: node[key].hex() for key in PROPERTY_KEYS}  # no serials/SMBIOS/other nodes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--live-registry', action='store_true')
    args = parser.parse_args()
    if sys.platform != 'darwin':
        parser.error('macOS Command Line Tools are required')
    try:
        output = local_output(ROOT, args.output)
    except ValueError as error:
        parser.error(str(error))
    if output.exists():
        parser.error('Output exists; choose a fresh directory under out/')
    lock_path = ROOT / 'dependencies/sources.lock.json'
    sdk_pin = json.loads(lock_path.read_text())['components']['mac_kernel_sdk']
    archive = fetch(sdk_pin['archive'], ROOT / 'out/downloads/amd', offline=True)
    source_root = ROOT / 'kexts' / PRODUCT
    audit_sources(source_root)
    for source in (source_root, ROOT / 'tests/pci-probe'):
        if source.is_symlink() or any(p.is_symlink() for p in source.rglob('*')):
            parser.error('Source/test symlinks are not accepted')
    output.mkdir(parents=True)
    logs = output / 'logs'
    logs.mkdir()
    report = {
        'status': 'building', 'created_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'product': PRODUCT, 'version': '0.1.0', 'sdk_revision': sdk_pin['revision'],
        'sdk_archive_sha256': sdk_pin['archive']['sha256'],
        'installed': False, 'loaded': False, 'hardware_qualified': False,
        'default_enabled': False, 'activation_boot_arg': 'navi48-pci-probe=1',
        'input_model': 'IORegistry properties only; no direct PCI/MMIO reads or writes',
    }
    try:
        source = output / 'source'
        source.mkdir()
        for name in SOURCE_FILES:
            shutil.copyfile(source_root / name, source / name)
        tests = output / 'tests'
        shutil.copytree(ROOT / 'tests/pci-probe', tests)
        shutil.copyfile(lock_path, output / 'sources.lock.json')
        shutil.copyfile(ROOT / 'LICENSE', output / 'LICENSE')
        for name in ('build-pci-probe.py', 'pci_probe_audit.py', 'pinned_downloads.py', 'navi48_binary.py'):
            shutil.copyfile(ROOT / 'tools' / name, output / (name + '.snapshot'))
        report['source_sha256'] = {p.name: digest(p) for p in sorted(source.iterdir())}
        report['test_sha256'] = {p.relative_to(tests).as_posix(): digest(p) for p in sorted(tests.rglob('*')) if p.is_file()}
        report['tool_sha256'] = {p.name: digest(p) for p in sorted(output.glob('*.snapshot'))}
        report['surface_audit'] = audit_sources(source)
        extract_zip(archive, output / 'sdk-source')
        sdk = output / 'sdk-source' / ('MacKernelSDK-' + sdk_pin['revision'])
        if digest(sdk / 'LICENSE.txt') != sdk_pin['license_sha256']:
            raise ValueError('SDK license hash mismatch')
        run(['sw_vers'], logs / 'system.txt')
        run(['xcrun', 'clang++', '--version'], logs / 'compiler.txt')
        sysroot = subprocess.check_output(['xcrun', '--sdk', 'macosx', '--show-sdk-path'], text=True).strip()
        report['macos_sdk'] = subprocess.check_output(['xcrun', '--sdk', 'macosx', '--show-sdk-version'], text=True).strip()
        host = output / 'pci_probe_test'
        run(['xcrun', 'clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wshadow',
             '-O1', '-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
             '-I', tests / 'stubs', '-I', source, source / 'Navi48PciProbe.cpp',
             tests / 'probe_test.cpp', '-o', host], logs / 'host-build.log')
        run([host], logs / 'host-test.log')
        match = re.search(r'pci_probe_test: (\d+) checks, (\d+) failed', (logs / 'host-test.log').read_text())
        if not match or int(match[2]) != 0:
            raise ValueError('Missing/failed host test summary')
        report['host_tests'] = {'checks': int(match[1]), 'failed': int(match[2]),
                                'sanitizers': ['address', 'undefined'], 'kernel_executed': False}
        fixture = json.loads((tests / 'target-registry.json').read_text())['properties_hex']
        run([host, '--registry'] + [fixture[k] for k in PROPERTY_KEYS], logs / 'fixture-replay.log')
        if args.live_registry:
            properties = live_properties()
            (output / 'registry-properties.json').write_text(json.dumps(properties, indent=2) + '\n')
            run([host, '--registry'] + [properties[k] for k in PROPERTY_KEYS], logs / 'registry-replay.log')
            report['live_registry_replay'] = {'passed': True, 'kernel_executed': False,
                                               'properties_sha256': digest(output / 'registry-properties.json')}
        # Compile only these two translation units. No upstream Navi48, firmware,
        # wildcard sources, user-client implementation, or Apple graphics hooks.
        common = ['-arch', 'x86_64', '-target', 'x86_64-apple-macos11.0', '-isysroot', sysroot,
                  '-isystem', sdk / 'Headers', '-I', source, '-nostdinc',
                  '-DKERNEL', '-DKERNEL_PRIVATE', '-DDRIVER_PRIVATE', '-DAPPLE', '-DNeXT',
                  '-D_FORTIFY_SOURCE=0', '-fno-builtin', '-fno-common', '-fno-stack-protector',
                  '-mkernel', '-Wall', '-Wextra', '-Werror', '-Wshadow', '-Os']
        objects = output / 'objects'
        objects.mkdir()
        run(['xcrun', 'clang++'] + common + ['-std=c++17', '-fapple-kext', '-fno-exceptions',
            '-fno-rtti', '-fcheck-new', '-fno-c++-static-destructors', '-c', source / 'Navi48PciProbe.cpp',
            '-o', objects / 'probe.o'], logs / 'kernel-cxx.log')
        run(['xcrun', 'clang'] + common + ['-c', source / 'kmod_info.c', '-o', objects / 'kmod_info.o'], logs / 'kernel-c.log')
        artifact = output / (PRODUCT + '.kext')
        executable = artifact / 'Contents/MacOS' / PRODUCT
        executable.parent.mkdir(parents=True)
        run(['xcrun', 'clang++', '-arch', 'x86_64', '-target', 'x86_64-apple-macos11.0',
             '-isysroot', sysroot, '-nostdlib', '-Xlinker', '-kext', objects / 'probe.o',
             objects / 'kmod_info.o', '-L' + str(sdk / 'Library/x86_64'), '-lkmodc++', '-lkmod',
             '-Wl,-no_deduplicate', '-o', executable], logs / 'link.log')
        shutil.copyfile(source / 'Info.plist', artifact / 'Contents/Info.plist')
        run(['xattr', '-cr', artifact], logs / 'xattr.log')
        run(['codesign', '--force', '--sign', '-', '--timestamp=none', artifact], logs / 'sign.log')
        run(['codesign', '--verify', '--strict', '--verbose=4', artifact], logs / 'signature-verify.log')
        run(['codesign', '-d', '--verbose=4', artifact], logs / 'signature-details.log')
        file_segments(executable.read_bytes())  # reject non-thin/non-x86_64/non-kext images
        run(['otool', '-l', executable], logs / 'load-commands.log')
        load_commands = (logs / 'load-commands.log').read_text()
        if re.search(r'cmd LC_\w*DYLIB', load_commands):
            raise ValueError('Unexpected dylib dependency')
        undefined = subprocess.check_output(['nm', '-u', str(executable)])
        demangled = subprocess.check_output(['c++filt'], input=undefined).decode()
        (logs / 'undefined-demangled.txt').write_text(demangled)
        report['import_audit'] = audit_imports(demangled)
        report.update(status='built-and-host-tested-not-load-qualified', architecture='x86_64',
                      artifact=str(artifact.relative_to(ROOT)), executable_sha256=digest(executable))
        hashes = {p.relative_to(artifact).as_posix(): digest(p) for p in sorted(artifact.rglob('*')) if p.is_file()}
        (output / 'SHA256SUMS.json').write_text(json.dumps(hashes, indent=2) + '\n')
    except Exception as error:
        report.update(status='failed', error=str(error))
        raise
    finally:
        (output / 'build-report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
