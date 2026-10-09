#!/usr/bin/env python3
"""Build native firmware and a separate IOKit mapping-controller archive, NOT kexts.

Uses only pinned, already-cached inputs; exports a fresh source/SDK each time.
Host tests use RAM for PSP and IOKit doubles for the actual resource controller.
No live PCI mapping, hardware authorization, firmware execution or GPU access.
"""
import argparse
import datetime
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys

from native_core_audit import (LOCAL_FILES, PLATFORM_FILES, PLATFORM_DEPENDENCIES,
                               audit_dependencies, audit_symbols, audit_platform_symbols,
                               isolate_logging, harden_access_header, validate_psp_patch,
                               object_sections, validate_manifest, verify_firmware_bytes)
from pinned_downloads import digest, extract_zip, fetch, local_output

ROOT = Path(__file__).resolve().parents[1]
MODULE = ROOT / 'native/Navi48FirmwareCore'


def run(command, log, timeout=180, cwd=None):
    with log.open('w') as stream:
        stream.write('Command: ' + repr([str(x) for x in command]) + '\n')
        stream.flush()
        subprocess.run([str(x) for x in command], stdout=stream, stderr=subprocess.STDOUT,
                       check=True, timeout=timeout, cwd=cwd)


def output(command):
    return subprocess.check_output([str(x) for x in command], text=True, timeout=60)


def hashes(root):
    return {p.relative_to(root).as_posix(): digest(p) for p in sorted(root.rglob('*')) if p.is_file()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--source', type=Path, default=ROOT / 'out/dependencies/Navi48-MacOS')
    parser.add_argument('--dependencies', type=Path, default=ROOT / 'out/dependencies/amd-pinned')
    args = parser.parse_args()
    if sys.platform != 'darwin':
        parser.error('macOS Command Line Tools required')
    destination = local_output(ROOT, args.output)
    if destination.exists() or any(c.isspace() for c in str(destination)):
        parser.error('Use a fresh child of out/, without whitespace')
    lock_path = ROOT / 'dependencies/sources.lock.json'
    lock = json.loads(lock_path.read_text())['components']
    manifest = json.loads((MODULE / 'manifest.json').read_text())
    validate_manifest(manifest)
    revision = lock['navi48']['revision']
    checkout = args.source.resolve()
    if (manifest['navi48_revision'] != revision or
            output(['git', '-C', checkout, 'rev-parse', 'HEAD']).strip() != revision):
        parser.error('Native manifest/checkout must match pinned Navi48 revision')
    if output(['git', '-C', checkout, 'status', '--porcelain', '--untracked-files=no']).strip():
        parser.error('Refusing modified upstream checkout')
    for directory in (MODULE, ROOT / 'tests/native-core', ROOT / 'tests/native-platform'):
        if directory.is_symlink() or any(p.is_symlink() for p in directory.rglob('*')):
            parser.error('Source/test symlinks are not accepted')
    sdk_pin = lock['mac_kernel_sdk']
    sdk_archive = fetch(sdk_pin['archive'], ROOT / 'out/downloads/amd', offline=True)
    fw_pin = lock['linux_firmware']
    firmware = args.dependencies.resolve() / 'linux-firmware'
    for name, expected in dict(fw_pin['expected_sha256'], **fw_pin['notices_sha256']).items():
        path = firmware / ('amdgpu/' + name if name in fw_pin['expected_sha256'] else name)
        if digest(path) != expected:
            raise ValueError('Firmware/notice hash mismatch: ' + name)
    destination.mkdir(parents=True)
    logs = destination / 'logs'
    logs.mkdir()
    report = {
        'status': 'building', 'created_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'product': manifest['product'], 'navi48_revision': revision,
        'sdk_revision': sdk_pin['revision'], 'firmware_revision': fw_pin['revision'],
        'kind': 'non-loadable-static-library', 'installed': False, 'loaded': False,
        'gpu_code_executed': False, 'hardware_qualified': False,
        'runtime_preflight_adapter_implemented': False, 'native_transport_included': False,
        'declared_mapping_binder_implemented': True, 'bounded_access_and_psp_staging_integrated': True,
        'iokit_mapping_controller_implemented': True, 'hardware_authorization_implemented': False,
    }
    try:
        inputs = destination / 'inputs'
        inputs.mkdir()
        shutil.copytree(MODULE, inputs / 'module')
        shutil.copytree(ROOT / 'tests/native-core', inputs / 'tests')
        shutil.copytree(ROOT / 'tests/native-platform', inputs / 'platform-tests')
        shutil.copyfile(lock_path, inputs / 'sources.lock.json')
        for name in ('build-native-firmware-core.py', 'native_core_audit.py', 'pinned_downloads.py'):
            shutil.copyfile(ROOT / 'tools' / name, inputs / name)
        report['local_input_sha256'] = hashes(inputs)
        run(['sw_vers'], logs / 'system.log')
        run(['xcrun', 'clang++', '--version'], logs / 'compiler.log')
        sysroot = output(['xcrun', '--sdk', 'macosx', '--show-sdk-path']).strip()
        report['macos_sdk'] = output(['xcrun', '--sdk', 'macosx', '--show-sdk-version']).strip()
        archive = destination / 'upstream.zip'
        run(['git', '-C', checkout, 'archive', '--format=zip', '--output=' + str(archive), revision],
            logs / 'export.log')
        report['upstream_archive_sha256'] = digest(archive)
        extract_zip(archive, destination / 'upstream')
        extract_zip(sdk_archive, destination / 'sdk-source')
        sdk = destination / 'sdk-source' / ('MacKernelSDK-' + sdk_pin['revision'])
        if digest(sdk / 'LICENSE.txt') != sdk_pin['license_sha256']:
            raise ValueError('SDK notice hash mismatch')
        upstream = destination / 'upstream/src/navi48-bringup'
        source = destination / 'source'
        source.mkdir()
        for name, expected in manifest['upstream_sha256'].items():
            original = upstream / 'src' / name
            if digest(original) != expected:
                raise ValueError('Upstream subset hash mismatch: ' + name)
            target = source / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(original, target)
        # All upstream edits are local to the fresh export, recorded and hash-guarded.
        log_header = source / 'amd/amdgpu_log.h'
        changed, patch = isolate_logging(log_header.read_text())
        log_header.write_text(changed)
        (destination / '0001-isolate-logging.patch').write_text(patch)
        for name in LOCAL_FILES + PLATFORM_FILES:
            shutil.copyfile(inputs / 'module' / name, source / name)
        regs = source / 'amd/amdgpu_regs.h'
        changed, patch = harden_access_header(regs.read_text())
        regs.write_text(changed)
        (destination / '0002-bounded-access.patch').write_text(patch)
        psp_patch = inputs / 'module/patches/0003-psp-access-errors.patch'
        validate_psp_patch(psp_patch.read_text())
        run(['patch', '-t', '-F', '0', '-p1', '-i', psp_patch], logs / 'psp-patch.log', cwd=source)
        if digest(source / 'amd/psp_v14_0.cpp') != manifest['patched_psp_sha256']:
            raise ValueError('Patched PSP source hash mismatch')
        notices = destination / 'notices'
        notices.mkdir()
        for original, target in (
            (destination / 'upstream/LICENSE', 'Navi48-root-LICENSE'),
            (upstream / 'LICENSE', 'Navi48Bringup-LICENSE'),
            (upstream / 'NOTICE', 'Navi48Bringup-NOTICE'),
            (destination / 'upstream/third-party/THIRD-PARTY-LICENSES.txt', 'THIRD-PARTY-LICENSES.txt'),
            (sdk / 'LICENSE.txt', 'MacKernelSDK-LICENSE.txt'), (ROOT / 'LICENSE', 'local-LICENSE')
        ):
            shutil.copyfile(original, notices / target)
        blobs = destination / 'firmware'
        blobs.mkdir()
        for name in fw_pin['notices_sha256']:
            shutil.copyfile(firmware / name, notices / name)
        generated = []
        for name in sorted(fw_pin['expected_sha256']):
            shutil.copyfile(firmware / 'amdgpu' / name, blobs / name)
            generated_name = 'fw/fw_' + Path(name).stem + '.c'
            generated.append(generated_name)
            run([sys.executable, '-B', upstream / 'tools/embed-firmware.py',
                 blobs / name, source / generated_name, 'fw_' + Path(name).stem],
                logs / ('embed-' + name + '.log'))
        # Policy/logger only: no upstream hardware routine is run on the host.
        host = destination / 'native_core_test'
        tests = inputs / 'tests'
        run(['xcrun', 'clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wshadow',
             '-O1', '-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
             '-I', tests / 'stubs', '-I', source, source / 'Preflight.cpp',
             source / 'NativeLog.cpp', tests / 'preflight_test.cpp', '-o', host], logs / 'host-build.log')
        run([host], logs / 'host-test.log')
        summary = re.search(r'native_core_test: (\d+) checks, (\d+) failed', (logs / 'host-test.log').read_text())
        if not summary or int(summary[2]):
            raise ValueError('Missing/failing host test summary')
        report['host_tests'] = {'checks': int(summary[1]), 'failed': 0,
                                'sanitizers': ['address', 'undefined'],
                                'scope': 'local preflight/ownership model and logger, not GPU routines'}
        access_host = destination / 'native_access_test'
        run(['xcrun', 'clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wshadow',
             '-O1', '-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
             '-I', tests / 'stubs', '-I', source, '-I', source / 'amd',
             source / 'Preflight.cpp', source / 'MappedAccess.cpp', source / 'NativeLog.cpp',
             source / 'amd/psp_v14_0.cpp', tests / 'access_test.cpp', '-o', access_host], logs / 'access-build.log')
        run([access_host], logs / 'access-test.log')
        summary = re.search(r'native_access_test: (\d+) checks, (\d+) failed', (logs / 'access-test.log').read_text())
        if not summary or int(summary[2]):
            raise ValueError('Missing/failing actual access/PSP test summary')
        report['access_tests'] = {'checks': int(summary[1]), 'failed': 0,
                                  'sanitizers': ['address', 'undefined'],
                                  'scope': 'same bounded access, mapping binder and PSP code on ordinary RAM',
                                  'gpu_executed': False}
        platform_host = destination / 'native_platform_test'
        platform_tests = inputs / 'platform-tests'
        run(['xcrun', 'clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wshadow',
             '-O1', '-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
             '-I', platform_tests / 'stubs', '-I', source, source / 'IOKitController.cpp',
             platform_tests / 'controller_test.cpp', '-o', platform_host], logs / 'platform-host-build.log')
        run([platform_host], logs / 'platform-host-test.log')
        summary = re.search(r'native_platform_test: (\d+) checks, (\d+) failed',
                            (logs / 'platform-host-test.log').read_text())
        if not summary or int(summary[2]):
            raise ValueError('Missing/failing IOKit resource-controller test summary')
        report['platform_tests'] = {'checks': int(summary[1]), 'failed': 0,
                                    'sanitizers': ['address', 'undefined'],
                                    'scope': 'same IOKit controller with simulated provider/descriptors/maps',
                                    'live_iokit_calls': False, 'gpu_executed': False}
        objects = destination / 'objects'
        objects.mkdir()
        units = manifest['sources'] + ['NativeLog.cpp', 'Preflight.cpp', 'MappedAccess.cpp'] + generated
        expected_files = set(manifest['upstream_sha256']) | set(LOCAL_FILES) | set(generated)
        common = ['-arch', 'x86_64', '-target', 'x86_64-apple-macos11.0', '-isysroot', sysroot,
                  '-isystem', sdk / 'Headers', '-I', source, '-I', source / 'amd', '-nostdinc',
                  '-DKERNEL', '-DKERNEL_PRIVATE', '-DDRIVER_PRIVATE', '-DAPPLE', '-DNeXT',
                  '-D_FORTIFY_SOURCE=0', '-fno-builtin', '-fno-common', '-fno-stack-protector',
                  '-mkernel', '-Wall', '-Wextra', '-Os', '-MD']
        compiled = []
        deps = set()
        for name in units:
            obj = objects / (Path(name).stem + '.o')
            dep = obj.with_suffix('.d')
            cxx = name.endswith('.cpp')
            flags = common + (['-std=c++17', '-fapple-kext', '-fno-exceptions', '-fno-rtti',
                               '-fcheck-new', '-fno-c++-static-destructors'] if cxx else [])
            if name in ('NativeLog.cpp', 'Preflight.cpp', 'MappedAccess.cpp'):
                flags += ['-Werror', '-Wshadow']
            run(['xcrun', 'clang++' if cxx else 'clang'] + flags +
                ['-MF', dep, '-c', source / name, '-o', obj], logs / (obj.stem + '-compile.log'))
            deps.update(dep.read_text().replace('\\\n', '').split(':', 1)[1].split())
            compiled.append(obj)
        report['compiled_source_dependencies'] = audit_dependencies(deps, source, sdk, expected_files)
        report['compilation_units'] = units
        merged = destination / 'Navi48FirmwareCore.o'
        run(['xcrun', 'ld', '-r', '-arch', 'x86_64', '-o', merged] + compiled, logs / 'partial-link.log')
        data = merged.read_bytes()
        sections = object_sections(data)
        undefined = output(['nm', '-u', '-j', merged]).splitlines()
        defined = output(['nm', '-g', '-U', '-j', merged]).splitlines()
        report['symbol_audit'] = audit_symbols(undefined, defined, manifest['allowed_kernel_imports'])
        (logs / 'undefined.txt').write_text('\n'.join(undefined) + '\n')
        (logs / 'defined.txt').write_text('\n'.join(defined) + '\n')
        linked_fw = verify_firmware_bytes(data, sections, output(['nm', '-n', merged]),
                                         blobs, fw_pin['expected_sha256'])
        (destination / 'embedded-firmware.json').write_text(json.dumps(linked_fw, indent=2) + '\n')
        library = destination / 'libNavi48FirmwareCore.a'
        # -D makes archive headers deterministic; there is no kext link/sign/package step.
        run(['xcrun', 'libtool', '-static', '-D', '-o', library, merged], logs / 'archive.log')
        # The platform implementation is deliberately NOT linked into the
        # firmware core: its reviewed PCI/IOKit imports have a separate boundary.
        platform_object = destination / 'Navi48PlatformController.o'
        platform_dep = platform_object.with_suffix('.d')
        run(['xcrun', 'clang++'] + common +
            ['-std=c++17', '-fapple-kext', '-fno-exceptions', '-fno-rtti', '-fcheck-new',
             '-fno-c++-static-destructors', '-Werror', '-Wshadow', '-MF', platform_dep,
             '-c', source / 'IOKitController.cpp', '-o', platform_object], logs / 'IOKitController-compile.log')
        platform_deps = platform_dep.read_text().replace('\\\n', '').split(':', 1)[1].split()
        platform_inputs = audit_dependencies(platform_deps, source, sdk, PLATFORM_DEPENDENCIES)
        object_sections(platform_object.read_bytes())
        platform_symbols = audit_platform_symbols(output(['nm', '-u', '-j', platform_object]).splitlines(),
                                                  output(['nm', '-g', '-U', '-j', platform_object]).splitlines(),
                                                  manifest['platform_controller']['allowed_kernel_imports'])
        platform_library = destination / 'libNavi48PlatformController.a'
        run(['xcrun', 'libtool', '-static', '-D', '-o', platform_library, platform_object], logs / 'platform-archive.log')
        run(['otool', '-l', platform_object], logs / 'platform-load-commands.log')
        report['platform_controller'] = {'product': 'Navi48PlatformController',
                                         'kind': 'non-loadable-static-library',
                                         'object_sha256': digest(platform_object),
                                         'library_sha256': digest(platform_library),
                                         'compiled_source_dependencies': platform_inputs,
                                         'symbol_audit': platform_symbols,
                                         'live_mappings_acquired': False, 'hardware_authorized': False}
        run(['otool', '-l', merged], logs / 'load-commands.log')
        run(['file', merged, library], logs / 'file.log')
        report.update(status='isolated-subset-built-not-hardware-qualified', architecture='x86_64',
                      library_sha256=digest(library), object_sha256=digest(merged),
                      embedded_firmwares_verified=len(linked_fw), source_sha256=hashes(source),
                      warning_count=sum(len(re.findall(r'\bwarning:', p.read_text())) for p in logs.glob('*-compile.log')))
        (destination / 'SHA256SUMS.json').write_text(json.dumps({
            merged.name: digest(merged), library.name: digest(library),
            platform_object.name: digest(platform_object), platform_library.name: digest(platform_library)}, indent=2) + '\n')
    except Exception as error:
        report.update(status='failed', error=str(error))
        raise
    finally:
        (destination / 'build-report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({k: v for k, v in report.items() if k not in (
        'source_sha256', 'local_input_sha256', 'compiled_source_dependencies', 'compilation_units')}, indent=2))


if __name__ == '__main__':
    main()
