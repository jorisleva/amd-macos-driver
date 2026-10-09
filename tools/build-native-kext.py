#!/usr/bin/env python3
"""Build Navi48Native.kext from the verified native subset. NEVER install/load it.

One targeted service/controller smoke test, then compile/link/sign the actual
kext and inspect its firmware bytes. Reuses verified core objects: no repeated
observer/mutation campaign or wholesale upstream build. Offline, output under out/.
"""
import argparse
import datetime
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import zipfile

from native_core_audit import (LOCAL_FILES, PLATFORM_FILES, audit_symbols, audit_platform_symbols,
                               object_sections, validate_manifest, verify_firmware_bytes)
from native_kext_audit import (PRODUCT, VERSION, SOURCE_FILES, CORE_HEADERS,
                               audit_sources, audit_imports, audit_defined)
from navi48_binary import file_segments
from pinned_downloads import digest, extract_zip, fetch, local_output

ROOT = Path(__file__).resolve().parents[1]
MODULE = ROOT / 'native/Navi48FirmwareCore'


def run(command, log, timeout=180):
    with log.open('w') as stream:
        stream.write('Command: ' + repr([str(x) for x in command]) + '\n')
        stream.flush()
        subprocess.run([str(x) for x in command], stdout=stream, stderr=subprocess.STDOUT,
                       check=True, timeout=timeout)


def output(command):
    return subprocess.check_output([str(x) for x in command], text=True, timeout=60)


def verify_core(core, lock, manifest):
    report = json.loads((core / 'build-report.json').read_text())
    if (report['status'] != 'isolated-subset-built-not-hardware-qualified' or
            report['product'] != 'Navi48FirmwareCore' or report['navi48_revision'] != manifest['navi48_revision'] or
            report['sdk_revision'] != lock['mac_kernel_sdk']['revision'] or
            report['firmware_revision'] != lock['linux_firmware']['revision']):
        raise ValueError('Core build does not match pinned native subset')
    for name in LOCAL_FILES + PLATFORM_FILES:
        if digest(MODULE / name) != digest(core / 'source' / name):
            raise ValueError('Native code changed since core build: rebuild core first (' + name + ')')
    for name, expected in report['source_sha256'].items():
        if digest(core / 'source' / name) != expected:
            raise ValueError('Modified core source snapshot: ' + name)
    for name, expected in lock['linux_firmware']['expected_sha256'].items():
        if digest(core / 'firmware' / name) != expected:
            raise ValueError('Modified firmware input: ' + name)
    units = {}
    for name, expected in (("Navi48FirmwareCore.o", report['object_sha256']),
                           ("Navi48PlatformController.o", report['platform_controller']['object_sha256'])):
        path = core / name
        if digest(path) != expected:
            raise ValueError('Modified core object: ' + name)
        sections = object_sections(path.read_bytes())
        undefined = output(['nm', '-u', '-j', path]).splitlines()
        defined = output(['nm', '-g', '-U', '-j', path]).splitlines()
        if name == 'Navi48FirmwareCore.o':
            audit_symbols(undefined, defined, manifest['allowed_kernel_imports'])
            verify_firmware_bytes(path.read_bytes(), sections, output(['nm', '-n', path]),
                                  core / 'firmware', lock['linux_firmware']['expected_sha256'])
        else:
            audit_platform_symbols(undefined, defined, manifest['platform_controller']['allowed_kernel_imports'])
        units[name] = expected
    return units


def check_dependencies(dep_files, source, sdk):
    allowed = {'driver/' + name for name in SOURCE_FILES if name.endswith(('.cpp', '.hpp', '.c'))} | {
        'core/' + name for name in CORE_HEADERS}
    seen = set()
    for dep in dep_files:
        for name in dep.read_text().replace('\\\n', '').split(':', 1)[1].split():
            path = Path(name).resolve()
            if path.is_relative_to(source):
                relative = path.relative_to(source).as_posix()
                if relative not in allowed:
                    raise ValueError('Unreviewed kext compilation dependency: ' + relative)
                seen.add(relative)
            elif not path.is_relative_to(sdk):
                raise ValueError('Kext dependency outside isolated source/SDK: ' + str(path))
    if seen != allowed:
        raise ValueError('Missing selected kernel compilation dependency')
    return sorted(seen)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--core-build', type=Path, default=ROOT / 'out/native-platform/final-a')
    args = parser.parse_args()
    if sys.platform != 'darwin':
        parser.error('macOS Command Line Tools required')
    destination = local_output(ROOT, args.output)
    core = local_output(ROOT, args.core_build)
    if destination.exists() or any(c.isspace() for c in str(destination) + str(core)):
        parser.error('Choose a fresh output under out/, without whitespace')
    source_root = ROOT / 'kexts' / PRODUCT
    test_root = ROOT / 'tests/native-kext'
    dma_test_root = ROOT / 'tests/native-dma'
    for root in (source_root, test_root, dma_test_root, core):
        if root.is_symlink() or any(p.is_symlink() for p in root.rglob('*')):
            parser.error('Source/test/core symlinks are not accepted')
    lock_path = ROOT / 'dependencies/sources.lock.json'
    lock = json.loads(lock_path.read_text())['components']
    manifest = json.loads((MODULE / 'manifest.json').read_text())
    validate_manifest(manifest)
    if manifest['navi48_revision'] != lock['navi48']['revision']:
        parser.error('Manifest revision does not match pinned Navi48')
    audit_sources(source_root)
    inputs = verify_core(core, lock, manifest)
    sdk_pin = lock['mac_kernel_sdk']
    archive = fetch(sdk_pin['archive'], ROOT / 'out/downloads/amd', offline=True)
    destination.mkdir(parents=True)
    logs = destination / 'logs'; logs.mkdir()
    report = {'status': 'building', 'created_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
              'product': PRODUCT, 'version': VERSION, 'kind': 'kext-bundle',
              'installed': False, 'loaded': False, 'hardware_qualified': False,
              'gpu_initialized': False, 'firmware_sent': False, 'default_enabled': False,
              'activation_boot_arg': 'navi48-native-platform=1', 'explicit_scratch_required': True,
              'dma_allocator_linked': True, 'dma_allocator_invoked_by_service': False,
              'gpu_dma_validated': False, 'step1_complete': False, 'step2_complete': False,
              'core_build': str(core.relative_to(ROOT)), 'core_object_sha256': inputs,
              'sdk_revision': sdk_pin['revision'], 'navi48_revision': manifest['navi48_revision']}
    try:
        source = destination / 'source'; source.mkdir()
        driver = source / 'driver'; driver.mkdir()
        subset = source / 'core'; subset.mkdir()
        for name in SOURCE_FILES:
            shutil.copyfile(source_root / name, driver / name)
        for name in CORE_HEADERS + ('IOKitController.cpp',):
            target = subset / name; target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(core / 'source' / name, target)
        tests = destination / 'tests'; shutil.copytree(test_root, tests)
        dma_tests = destination / 'dma-tests'; shutil.copytree(dma_test_root, dma_tests)
        snapshots = destination / 'inputs'; snapshots.mkdir()
        shutil.copyfile(lock_path, snapshots / 'sources.lock.json')
        shutil.copyfile(MODULE / 'manifest.json', snapshots / 'native-manifest.json')
        shutil.copyfile(core / 'build-report.json', snapshots / 'core-build-report.json')
        for name in ('build-native-kext.py', 'native_kext_audit.py', 'native_core_audit.py',
                     'navi48_binary.py', 'pinned_downloads.py'):
            shutil.copyfile(ROOT / 'tools' / name, snapshots / name)
        report['source_sha256'] = {p.relative_to(source).as_posix(): digest(p) for p in sorted(source.rglob('*')) if p.is_file()}
        report['test_sha256'] = {p.relative_to(tests).as_posix(): digest(p) for p in sorted(tests.rglob('*')) if p.is_file()}
        report['dma_test_sha256'] = {p.relative_to(dma_tests).as_posix(): digest(p) for p in sorted(dma_tests.rglob('*')) if p.is_file()}
        report['input_sha256'] = {p.name: digest(p) for p in sorted(snapshots.iterdir())}
        report['personality_audit'] = audit_sources(driver)
        extract_zip(archive, destination / 'sdk-source')
        sdk = destination / 'sdk-source' / ('MacKernelSDK-' + sdk_pin['revision'])
        if digest(sdk / 'LICENSE.txt') != sdk_pin['license_sha256']:
            raise ValueError('SDK license hash mismatch')
        run(['sw_vers'], logs / 'system.log')
        run(['xcrun', 'clang++', '--version'], logs / 'compiler.log')
        sysroot = output(['xcrun', '--sdk', 'macosx', '--show-sdk-path']).strip()
        report['macos_sdk'] = output(['xcrun', '--sdk', 'macosx', '--show-sdk-version']).strip()
        smoke = destination / 'native_kext_smoke'
        run(['xcrun', 'clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wshadow',
             '-O1', '-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
             '-I', tests / 'stubs', '-I', driver, '-I', subset, driver / 'Navi48Native.cpp',
             subset / 'IOKitController.cpp', tests / 'service_smoke.cpp', '-o', smoke], logs / 'smoke-build.log')
        run([smoke], logs / 'smoke.log')
        match = re.search(r'native_kext_smoke: (\d+) checks, (\d+) failed', (logs / 'smoke.log').read_text())
        if not match or int(match[2]):
            raise ValueError('Native service/controller smoke test failed')
        report['targeted_smoke'] = {'checks': int(match[1]), 'failed': 0, 'sanitizers': ['address', 'undefined'],
                                    'scope': 'same service/controller with IOKit doubles, not a loaded kext'}
        dma_smoke = destination / 'native_dma_test'
        run(['xcrun', 'clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wshadow',
             '-O1', '-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
             '-I', dma_tests / 'stubs', '-I', driver, '-I', subset, driver / 'DmaBuffer.cpp',
             dma_tests / 'buffer_test.cpp', '-o', dma_smoke], logs / 'dma-smoke-build.log')
        run([dma_smoke], logs / 'dma-smoke.log')
        match = re.search(r'native_dma_test: (\d+) checks, (\d+) failed', (logs / 'dma-smoke.log').read_text())
        if not match or int(match[2]):
            raise ValueError('Native DMA lifecycle/IOVM test failed')
        report['dma_smoke'] = {'checks': int(match[1]), 'failed': 0, 'sanitizers': ['address', 'undefined'],
                               'scope': 'actual DMA adapter with IOKit doubles; no actual DMA/GPU'}
        objects = destination / 'objects'; objects.mkdir()
        for name in inputs:
            shutil.copyfile(core / name, objects / name)
        common = ['-arch', 'x86_64', '-target', 'x86_64-apple-macos11.0', '-isysroot', sysroot,
                  '-isystem', sdk / 'Headers', '-I', driver, '-I', subset, '-nostdinc',
                  '-DKERNEL', '-DKERNEL_PRIVATE', '-DDRIVER_PRIVATE', '-DAPPLE', '-DNeXT',
                  '-D_FORTIFY_SOURCE=0', '-fno-builtin', '-fno-common', '-fno-stack-protector',
                  '-mkernel', '-Wall', '-Wextra', '-Werror', '-Wshadow', '-Os', '-MD']
        deps = []
        for name in ('Navi48Native.cpp', 'DmaBuffer.cpp', 'kmod_info.c'):
            obj = objects / (Path(name).stem + '.o'); dep = obj.with_suffix('.d'); deps.append(dep)
            cxx = name.endswith('.cpp')
            flags = ['-std=c++17', '-fapple-kext', '-fno-exceptions', '-fno-rtti', '-fcheck-new',
                     '-fno-c++-static-destructors'] if cxx else []
            run(['xcrun', 'clang++' if cxx else 'clang'] + common + flags +
                ['-MF', dep, '-c', driver / name, '-o', obj], logs / (obj.stem + '-compile.log'))
        report['compiled_source_dependencies'] = check_dependencies(deps, source, sdk)
        artifact = destination / (PRODUCT + '.kext')
        executable = artifact / 'Contents/MacOS' / PRODUCT; executable.parent.mkdir(parents=True)
        run(['xcrun', 'clang++', '-arch', 'x86_64', '-target', 'x86_64-apple-macos11.0',
             '-isysroot', sysroot, '-nostdlib', '-Xlinker', '-kext', objects / 'Navi48Native.o',
             objects / 'kmod_info.o', objects / 'DmaBuffer.o',
             objects / 'Navi48PlatformController.o', objects / 'Navi48FirmwareCore.o',
             '-L' + str(sdk / 'Library/x86_64'), '-lkmodc++', '-lkmod', '-Wl,-no_deduplicate',
             '-o', executable], logs / 'link.log')
        shutil.copyfile(driver / 'Info.plist', artifact / 'Contents/Info.plist')
        # Keep upstream/firmware notices with the actual linked product.
        notices = artifact / 'Contents/Resources/Notices'
        shutil.copytree(core / 'notices', notices)
        run(['xattr', '-cr', artifact], logs / 'xattr.log')
        run(['codesign', '--force', '--sign', '-', '--timestamp=none', artifact], logs / 'sign.log')
        run(['codesign', '--verify', '--strict', '--verbose=4', artifact], logs / 'signature.log')
        run(['file', executable], logs / 'file.log')
        run(['otool', '-l', executable], logs / 'load-commands.log')
        if re.search(r'cmd LC_\w*DYLIB', (logs / 'load-commands.log').read_text()):
            raise ValueError('Unexpected dylib dependency in kernel image')
        undefined = subprocess.check_output(['nm', '-u', executable], timeout=60)
        demangled = subprocess.check_output(['c++filt'], input=undefined, timeout=60).decode()
        (logs / 'imports.txt').write_text(demangled)
        report['import_audit'] = audit_imports(demangled)
        # SDK kmod trampolines and _realmain/_antimain are private/local symbols.
        # They must exist, but need not be globally exported.
        defined = output(['nm', '-U', '-j', executable]).splitlines()
        report['symbol_audit'] = audit_defined(defined)
        data = executable.read_bytes()
        embedded = verify_firmware_bytes(data, file_segments(data), output(['nm', '-n', executable]),
                                         core / 'firmware', lock['linux_firmware']['expected_sha256'])
        (destination / 'embedded-firmware.json').write_text(json.dumps(embedded, indent=2) + '\n')
        sums = {p.relative_to(artifact).as_posix(): digest(p) for p in sorted(artifact.rglob('*')) if p.is_file()}
        (destination / 'SHA256SUMS.json').write_text(json.dumps(sums, indent=2) + '\n')
        package = destination / (PRODUCT + '.zip')
        with zipfile.ZipFile(package, 'w', compression=zipfile.ZIP_DEFLATED) as bundle:
            for path in sorted(artifact.rglob('*')):
                if path.is_file(): bundle.write(path, path.relative_to(destination).as_posix())
        report.update(status='kext-built-signed-not-load-qualified', architecture='x86_64',
                      artifact=str(artifact.relative_to(ROOT)), executable_sha256=digest(executable),
                      package_sha256=digest(package), firmwares_verified_in_kext=len(embedded),
                      warning_count=sum(len(re.findall(r'\bwarning:', p.read_text())) for p in logs.glob('*-compile.log')))
    except Exception as error:
        report.update(status='failed', error=str(error))
        raise
    finally:
        (destination / 'build-report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({k: v for k, v in report.items() if k not in ('source_sha256', 'test_sha256', 'input_sha256')}, indent=2))


if __name__ == '__main__':
    main()
