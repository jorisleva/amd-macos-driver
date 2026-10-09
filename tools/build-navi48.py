#!/usr/bin/env python3
"""Build the pinned Navi48 kext in a fresh out/ tree; never install or load it.

Requires a checkout at the locked Navi48 revision and inputs prepared by
prepare-amd-dependencies.py. Re-extracts the SDK from its verified archive and
exports the committed source, so neither previous objects nor edited sources
are silently reused. Python 3.9+; macOS Command Line Tools are sufficient.
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

from pinned_downloads import digest, extract_zip, fetch, local_output
from navi48_binary import verify_firmwares

ROOT = Path(__file__).resolve().parents[1]


def checked_hash(path, expected):
    actual = digest(path)
    if actual != expected:
        raise ValueError('SHA-256 mismatch: ' + str(path))
    return actual


def run(command, log, cwd=None, timeout=900):
    with log.open('w', encoding='utf-8') as stream:
        stream.write('Command: ' + repr([str(a) for a in command]) + '\n')
        stream.flush()
        subprocess.run([str(a) for a in command], cwd=cwd, stdout=stream,
                       stderr=subprocess.STDOUT, check=True, timeout=timeout)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=ROOT / 'out/dependencies/Navi48-MacOS')
    parser.add_argument('--dependencies', type=Path, default=ROOT / 'out/dependencies/amd-pinned')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--jobs', type=int, default=2)
    args = parser.parse_args()
    if sys.platform != 'darwin':
        parser.error('This build requires macOS Command Line Tools')
    if not 1 <= args.jobs <= 16:
        parser.error('--jobs must be between 1 and 16')
    output = local_output(ROOT, args.output)
    if output.exists():
        parser.error('Output already exists; choose a new child of out/')
    if any(c.isspace() for c in str(output)):
        parser.error('The upstream Makefile does not quote paths; use a path without whitespace')
    lock_path = ROOT / 'dependencies/sources.lock.json'
    lock = json.loads(lock_path.read_text())['components']
    revision = lock['navi48']['revision']
    source = args.source.resolve()
    head = subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip()
    if head != revision:
        parser.error('Navi48 checkout is not at the locked revision: ' + head)
    dirty = subprocess.check_output(['git', '-C', str(source), 'status', '--porcelain', '--untracked-files=no'], text=True)
    if dirty:
        parser.error('Navi48 checkout has tracked changes; refusing to ignore them')
    for tool in ('make', 'clang', 'clang++', 'xcrun', 'codesign', 'lipo', 'nm', 'otool', 'dwarfdump', 'c++filt'):
        if not shutil.which(tool):
            parser.error('Missing tool: ' + tool)
    sdk_pin = lock['mac_kernel_sdk']
    sdk_archive = fetch(sdk_pin['archive'], ROOT / 'out/downloads/amd', offline=True)
    firmware = args.dependencies.resolve() / 'linux-firmware'
    for name, expected in lock['linux_firmware']['expected_sha256'].items():
        checked_hash(firmware / 'amdgpu' / name, expected)
    for name, expected in lock['linux_firmware']['notices_sha256'].items():
        checked_hash(firmware / name, expected)
    output.mkdir(parents=True)
    logs = output / 'logs'
    logs.mkdir()
    report = {
        'status': 'building',
        'created_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'navi48_revision': revision,
        'sdk_revision': sdk_pin['revision'],
        'firmware_revision': lock['linux_firmware']['revision'],
        'builder_sha256': digest(Path(__file__)),
        'sources_lock_sha256': digest(lock_path),
        'installed': False, 'loaded': False, 'hardware_qualified': False,
    }
    report_path = output / 'build-report.json'
    try:
        shutil.copyfile(lock_path, output / 'sources.lock.json')
        shutil.copyfile(Path(__file__), output / 'build-navi48.py.snapshot')
        shutil.copyfile(ROOT / 'tools/pinned_downloads.py', output / 'pinned_downloads.py.snapshot')
        shutil.copyfile(ROOT / 'tools/navi48_binary.py', output / 'navi48_binary.py.snapshot')
        for name, command in (
                ('system', ['sw_vers']), ('host', ['uname', '-mrs']),
                ('compiler', ['clang', '--version']),
                ('sdk', ['xcrun', '--sdk', 'macosx', '--show-sdk-version'])):
            run(command, logs / (name + '.txt'))
        # git archive exports the pinned commit, not working-tree or ignored files.
        archive = output / 'navi48-source.zip'
        run(['git', '-C', source, 'archive', '--format=zip', '--output=' + str(archive), revision], logs / 'source-export.txt')
        report['source_archive_sha256'] = digest(archive)
        tree = output / 'source'
        extract_zip(archive, tree)
        extract_zip(sdk_archive, output / 'sdk-source')
        sdk = output / 'sdk-source' / ('MacKernelSDK-' + sdk_pin['revision'])
        checked_hash(sdk / 'LICENSE.txt', sdk_pin['license_sha256'])
        kext_source = tree / 'src/navi48-bringup'
        blobs = kext_source / 'firmware'
        blobs.mkdir(exist_ok=True)
        for name, expected in lock['linux_firmware']['expected_sha256'].items():
            shutil.copyfile(firmware / 'amdgpu' / name, blobs / name)
            checked_hash(blobs / name, expected)
        notices = output / 'firmware-notices'
        notices.mkdir()
        for name in lock['linux_firmware']['notices_sha256']:
            shutil.copyfile(firmware / name, notices / name)
        # GNU make otherwise deletes implicitly generated firmware C files after
        # linking. Retain them for independent byte verification, without changing
        # any upstream source or compiler/linker flags.
        audit_makefile = output / 'retain-intermediates.mk'
        audit_makefile.write_text('.SECONDARY:\n')
        run(['make', '-C', kext_source, '-f', 'Makefile', '-f', audit_makefile,
             'MKSDK=' + str(sdk), '-j' + str(args.jobs), 'all'], logs / 'make.log')
        built = kext_source / 'build/Navi48Bringup.kext'
        artifact = output / 'Navi48Bringup.kext'
        shutil.copytree(built, artifact)
        executable = artifact / 'Contents/MacOS/Navi48Bringup'
        info = plistlib.loads((artifact / 'Contents/Info.plist').read_bytes())
        if info['CFBundleIdentifier'] != 'com.navi48.bringup' or info['CFBundlePackageType'] != 'KEXT':
            raise ValueError('Unexpected bundle identity')
        arch = subprocess.check_output(['lipo', '-archs', str(executable)], text=True).strip()
        if arch != 'x86_64':
            raise ValueError('Unexpected executable architecture: ' + arch)
        for name, command in (
                ('signature-verify', ['codesign', '--verify', '--strict', '--verbose=4', artifact]),
                ('signature-details', ['codesign', '-d', '--verbose=4', artifact]),
                ('uuid', ['dwarfdump', '--uuid', executable]),
                ('load-commands', ['otool', '-l', executable]),
                ('libraries', ['otool', '-L', executable]),
                ('undefined', ['nm', '-u', '-arch', 'x86_64', executable])):
            run(command, logs / (name + '.txt'))
        # Mirror the upstream internal-symbol guard; undefined kernel KPI symbols
        # are expected in a kext. This is NOT kernel-collection link validation.
        undefined = subprocess.check_output(['nm', '-u', '-arch', 'x86_64', str(executable)])
        demangled = subprocess.check_output(['c++filt'], input=undefined).decode()
        (logs / 'undefined-demangled.txt').write_text(demangled)
        if re.search(r'Navi48|AmdTtl|amdgpu::|n48::|n48dcn::|xlat12|_sc_|dcn41_', demangled):
            raise ValueError('An internal Navi48 symbol is undefined')
        for name in lock['linux_firmware']['expected_sha256']:
            run([sys.executable, '-B', kext_source / 'tools/embed-firmware.py', '--verify',
                 blobs / name, kext_source / 'src/fw' / ('fw_' + Path(name).stem + '.c')],
                logs / ('embedded-' + name + '.txt'))
        embedded = verify_firmwares(executable, blobs, lock['linux_firmware']['expected_sha256'])
        (output / 'embedded-firmware.json').write_text(json.dumps(embedded, indent=2) + '\n')
        manifest = {p.relative_to(artifact).as_posix(): digest(p)
                    for p in sorted(artifact.rglob('*')) if p.is_file()}
        (output / 'SHA256SUMS.json').write_text(json.dumps(manifest, indent=2) + '\n')
        report.update(status='built-and-statically-checked-not-load-qualified',
                      artifact=str(artifact.relative_to(ROOT)), architecture=arch,
                      bundle_version=info['CFBundleVersion'],
                      executable_sha256=digest(executable),
                      embedded_firmwares_verified=len(lock['linux_firmware']['expected_sha256']),
                      undefined_symbol_count=len(demangled.splitlines()),
                      kernel_collection_link_validated=False)
    except Exception as error:
        report.update(status='failed', error=str(error))
        raise
    finally:
        report_path.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
