#!/usr/bin/env python3
"""Prepare reference/OFF/ON EFI copies. Never replace a boot EFI or load a kext.

Private outputs (SMBIOS included) stay under out/. Optional staging writes only
an unused child of /Volumes/OPENCORE/PROFILS-TAHOE, NOT /Volumes/OPENCORE/EFI.
Python 3.9+, macOS. This tool preserves the current EFI, not a generated model.
"""
import argparse
import copy
import datetime
import json
import os
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import sys

from pinned_downloads import digest, extract_zip, fetch, local_output

ROOT = Path(__file__).resolve().parents[1]
GUID = '7C436110-AB2A-4BBB-A880-FE41995C9F82'
PRODUCT = 'Navi48PciProbe'
BUNDLE_ID = 'com.amd-macos-driver.Navi48PciProbe'
APPROVED_BINARY = '8146414908a073e9fe504ca89b285d3ee076f2e327eb606cecaf023d0cd1ca94'
BASE_KEXTS = {'Lilu.kext', 'VirtualSMC.kext', 'AppleMCEReporterDisabler.kext',
              'RestrictEvents.kext', 'RealtekRTL8111.kext'}
DARWIN = '25.6.0'


def require(condition, message):
    if not condition:
        raise ValueError(message)


def tree_hashes(root):
    root = Path(root)
    require(root.is_dir() and not root.is_symlink(), 'Expected a real input directory')
    result = {}
    for path in sorted(root.rglob('*')):
        require(not path.is_symlink(), 'Symlinks are not accepted')
        if path.is_dir():
            continue
        require(path.is_file(), 'Special files are not accepted')
        result[path.relative_to(root).as_posix()] = digest(path)
    require(bool(result), 'Empty input tree')
    return result


def copy_verified(source, target):
    """No copystat/copytree: preserve explicit AppleDouble files on FAT32."""
    before = tree_hashes(source)
    target = Path(target)
    target.mkdir(parents=True, exist_ok=False)
    # FAT32 may discard an orphan ._ sidecar when its main file/directory is
    # subsequently created. Create directories first and sidecars LAST.
    for directory in sorted(Path(source).rglob('*')):
        if directory.is_dir():
            (target / directory.relative_to(source)).mkdir(parents=True, exist_ok=True)
    for name in sorted(before, key=lambda n: (Path(n).name.startswith('._'), n)):
        destination = target / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        with (Path(source) / name).open('rb') as src, destination.open('xb') as dst:
            shutil.copyfileobj(src, dst)
    require(tree_hashes(source) == before, 'Source changed during copy')
    require(tree_hashes(target) == before, 'Copy checksum mismatch: ' + str(target))
    return before


def validate_baseline(config):
    entries = config['Kernel']['Add']
    require(len(entries) == len(BASE_KEXTS) and
            {p['BundlePath'] for p in entries} == BASE_KEXTS and
            all(p['Enabled'] is True for p in entries), 'Unexpected baseline kext list')
    require(not config['Kernel'].get('Force'), 'Kernel/Force requires a separate review')
    nvram = config['NVRAM']
    additions = nvram['Add'][GUID]
    tokens = additions['boot-args'].split()
    for key, wanted in [('navi48bringup', '0'), ('rdna4-off', '1')]:
        require([t for t in tokens if t.split('=')[0] == key] == [key + '=' + wanted],
                'Missing/contradictory GPU kill switch: ' + key)
    require(not any(t.split('=')[0] == 'navi48-pci-probe' for t in tokens),
            'Baseline already includes probe activation settings')
    require('boot-args' in nvram['Delete'].get(GUID, []), 'NVRAM/Delete must already include boot-args')
    require(additions.get('csr-active-config') == b'\0\0\0\0', 'SIP configuration must remain enabled')
    require(nvram['WriteFlash'] is False, 'Persistent NVRAM writes are not part of this trial')
    require(config['Misc']['Boot']['LauncherOption'] == 'Disabled', 'Firmware launcher registration is not part of this trial')


def entry():
    return {'Arch': 'x86_64', 'BundlePath': PRODUCT + '.kext',
            'Comment': 'PCI registry observer 0.1.0 - no GPU initialization',
            'Enabled': True, 'ExecutablePath': 'Contents/MacOS/' + PRODUCT,
            'MinKernel': DARWIN, 'MaxKernel': DARWIN, 'PlistPath': 'Contents/Info.plist'}


def make_profile(baseline, enabled):
    require(type(enabled) is bool, 'Activation must be a boolean')
    validate_baseline(baseline)
    result = copy.deepcopy(baseline)
    result['Kernel']['Add'].append(entry())
    result['NVRAM']['Add'][GUID]['boot-args'] += ' navi48-pci-probe=' + str(int(enabled))
    validate_profile(baseline, result, enabled)
    return result


def validate_profile(baseline, profile, enabled):
    """Permit exactly one Kernel/Add entry and one boot argument; nothing else."""
    validate_baseline(baseline)
    restored = copy.deepcopy(profile)
    require(len(restored['Kernel']['Add']) == len(baseline['Kernel']['Add']) + 1, 'Unexpected injection count')
    require(plistlib.dumps(restored['Kernel']['Add'].pop()) == plistlib.dumps(entry()),
            'Unexpected probe injection entry')
    original_args = baseline['NVRAM']['Add'][GUID]['boot-args']
    require(restored['NVRAM']['Add'][GUID]['boot-args'] ==
            original_args + ' navi48-pci-probe=' + str(int(enabled)), 'Unexpected boot argument delta')
    restored['NVRAM']['Add'][GUID]['boot-args'] = original_args
    require(plistlib.dumps(restored) == plistlib.dumps(baseline),
            'Profile modifies settings outside the two allowed changes')


def symbol_check(import_text, export_text):
    # Apple nm -u emits bare names; LLVM/GNU variants may prefix them with U.
    imports = set()
    for line in import_text.splitlines():
        words = line.split()
        if len(words) == 1 and re.fullmatch(r'_[A-Za-z0-9_.$]+', words[0]):
            imports.add(words[0])
        elif len(words) == 2 and words[0] == 'U':
            imports.add(words[1])
    providers = {}
    provider = None
    for line in export_text.splitlines():
        if line.startswith('Symbols for ') and line.rstrip().endswith(':'):
            provider = line.strip()[len('Symbols for '):-1]
        else:
            parts = line.split()
            if provider and len(parts) == 3 and parts[1] != 'U' and re.fullmatch('[0-9a-fA-F]+', parts[0]):
                providers.setdefault(parts[2], set()).add(provider)
    require(bool(imports), 'No undefined symbols parsed')
    missing = sorted(imports - providers.keys())
    require(not missing, 'Imports not found in BootKC: ' + ', '.join(missing))
    owners = sorted({p for symbol in imports for p in providers[symbol]})
    return {'imports_found': len(imports), 'missing': missing, 'symbol_owners': owners,
            'kernel_link_validated': False, 'abi_compatibility_validated': False,
            'scope': 'Defined symbol names only, not relocation/vtable/load qualification'}


def stage_destination(volume, name, info):
    volume = Path(volume)
    require(volume == Path('/Volumes/OPENCORE') and volume.resolve() == volume,
            'Staging is restricted to the explicit OPENCORE mount')
    require(info.get('MountPoint') == str(volume) and info.get('FilesystemType') == 'msdos'
            and info.get('BusProtocol') == 'USB' and info.get('Internal') is False,
            'Expected the external USB FAT32 OPENCORE volume')
    require(bool(re.fullmatch(r'pci-probe-[A-Za-z0-9_-]+', name)), 'Unexpected package directory name')
    parent = volume / 'PROFILS-TAHOE'
    require(parent.resolve() == parent, 'Profile directory must not be a symlink')
    target = parent / name
    require(not target.exists() and not target.is_symlink(), 'Stage destination already exists')
    return target


def command(args, log):
    result = subprocess.run([str(a) for a in args], stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, timeout=90)
    log.write_bytes(result.stdout)
    require(result.returncode == 0, 'Command failed; see ' + str(log))
    return result.stdout.decode(errors='replace')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--efi', type=Path, default=Path('/Volumes/OPENCORE/EFI'))
    parser.add_argument('--build', type=Path, default=ROOT / 'out/pci-probe/release-a')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--offline', action='store_true')
    parser.add_argument('--stage-volume', type=Path)
    args = parser.parse_args()
    require(sys.platform == 'darwin', 'macOS is required')
    output = local_output(ROOT, args.output)
    require(not output.exists(), 'Output already exists')
    source = args.efi.resolve()
    require(not args.efi.is_symlink() and not output.is_relative_to(source), 'Unsafe EFI source/output relationship')
    initial = tree_hashes(source)
    baseline = plistlib.loads((source / 'OC/config.plist').read_bytes())
    validate_baseline(baseline)
    require({p.name for p in (source / 'OC/Kexts').iterdir() if p.is_dir()} == BASE_KEXTS,
            'Unexpected physical kext directory')
    require(subprocess.check_output(['uname', '-m'], text=True).strip() == 'x86_64', 'Expected x86_64 host')
    require(subprocess.check_output(['uname', '-r'], text=True).strip() == DARWIN, 'Different Darwin release; review required')
    require(subprocess.check_output(['sw_vers', '-buildVersion'], text=True).strip() == '25G241', 'Different macOS build; review required')
    loaded = subprocess.check_output(['kmutil', 'showloaded'], stderr=subprocess.PIPE, text=True)
    require(BUNDLE_ID not in loaded and 'com.navi48.bringup' not in loaded, 'An experimental module is already loaded')
    stage = None
    if args.stage_volume:
        require(os.path.ismount(args.stage_volume), 'Stage volume is not mounted')
        info = plistlib.loads(subprocess.check_output(['diskutil', 'info', '-plist', str(args.stage_volume)]))
        stage = stage_destination(args.stage_volume, output.name, info)
    build = args.build.resolve()
    artifact = build / (PRODUCT + '.kext')
    artifact_hashes = tree_hashes(artifact)
    require(artifact_hashes == json.loads((build / 'SHA256SUMS.json').read_text()), 'Build manifest mismatch')
    require(digest(artifact / 'Contents/MacOS' / PRODUCT) == APPROVED_BINARY, 'Binary is not the reviewed 0.1.0 artifact')
    build_report = json.loads((build / 'build-report.json').read_text())
    require(build_report['status'] == 'built-and-host-tested-not-load-qualified', 'Build is not accepted for preparation')
    output.mkdir(parents=True)
    logs = output / 'validation'
    logs.mkdir()
    command(['codesign', '--verify', '--strict', '--verbose=4', artifact], logs / 'signature.log')
    lock = json.loads((ROOT / 'boot/ryzen5600x-b550/sources.lock.json').read_text())
    archive = fetch(lock['inputs']['opencore'], ROOT / 'out/downloads/opencore', offline=args.offline)
    extract_zip(archive, output / 'opencore-tools')
    oc = output / 'opencore-tools'
    for name in ['BOOT/BOOTx64.efi', 'OC/OpenCore.efi']:
        require(initial[name] == digest(oc / 'X64/EFI' / name), 'Baseline is not pinned OpenCore 1.0.8 DEBUG')
    validator = oc / 'Utilities/ocvalidate/ocvalidate'
    validator.chmod(0o755)
    imports = command(['xcrun', 'nm', '-u', artifact / 'Contents/MacOS' / PRODUCT], logs / 'imports.txt')
    kc = Path('/System/Library/KernelCollections/BootKernelExtensions.kc')
    exports = command(['xcrun', 'nm', '-arch', 'x86_64', '-gU', kc], logs / 'boot-kc-symbols.txt')
    symbols = symbol_check(imports, exports)
    require(symbols['imports_found'] == build_report['import_audit']['undefined_symbols'],
            'Parsed symbol count differs from the reviewed build')
    package = output / 'package'
    package.mkdir()
    profiles = {}
    for name, mode in [('00-reference', None), ('01-disabled', False), ('02-enabled', True)]:
        efi = package / name / 'EFI'
        copy_verified(source, efi)
        if mode is not None:
            profile = make_profile(baseline, mode)
            config = efi / 'OC/config.plist'
            config.write_bytes(plistlib.dumps(profile, sort_keys=False))
            validate_profile(baseline, plistlib.loads(config.read_bytes()), mode)
            require(copy_verified(artifact, efi / 'OC/Kexts' / artifact.name) == artifact_hashes,
                    'Probe artifact changed after preflight')
            command(['codesign', '--verify', '--strict', efi / 'OC/Kexts' / artifact.name], logs / (name + '-signature.log'))
        command([validator, efi / 'OC/config.plist'], logs / (name + '-ocvalidate.log'))
        hashes = tree_hashes(efi)
        changed = sorted(n for n in initial if hashes.get(n) != initial[n])
        added = sorted(hashes.keys() - initial.keys())
        expected_added = sorted('OC/Kexts/' + artifact.name + '/' + n for n in tree_hashes(artifact))
        require(changed == ([] if mode is None else ['OC/config.plist']), 'Unexpected baseline file change')
        require(added == ([] if mode is None else expected_added), 'Unexpected additional EFI file')
        profiles[name] = {'files': len(hashes), 'changed': changed, 'added': added,
                          'config_sha256': digest(efi / 'OC/config.plist'), 'ocvalidate_passed': True}
    require(tree_hashes(source) == initial, 'Active EFI changed during preparation')
    report = {'schema_version': 1, 'status': 'prepared-not-activated-not-boot-tested',
              'created_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
              'source_config_sha256': initial['OC/config.plist'], 'source_files': len(initial),
              'source_efi_unchanged': True, 'source_efi': str(source),
              'opencore': '1.0.8 DEBUG', 'opencore_archive_sha256': digest(archive),
              'binary_sha256': APPROVED_BINARY, 'darwin': DARWIN, 'macos_build': '25G241',
              'probe_build_report_sha256': digest(build / 'build-report.json'),
              'builder_sha256': digest(Path(__file__)), 'boot_kc_sha256': digest(kc),
              'symbol_check': symbols, 'profiles': profiles,
              'boot_efi_replaced': False, 'system_kext_installed': False,
              'boot_test_performed': False, 'nvram_or_security_changed_by_tool': False,
              'private_smbios_included': True}
    (package / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    shutil.copyfile(ROOT / 'docs/PCI-PROBE-EFI-ESSAI.md', package / 'LIRE-AVANT-ESSAI.md')
    for log in logs.glob('*-ocvalidate.log'):
        shutil.copyfile(log, package / log.name)
    payload = tree_hashes(package)
    (package / 'SHA256SUMS.json').write_text(json.dumps(payload, indent=2) + '\n')
    if stage:
        copy_verified(package, stage)
        for name in ('01-disabled', '02-enabled'):
            command(['codesign', '--verify', '--strict', stage / name / 'EFI/OC/Kexts' / artifact.name],
                    logs / (name + '-usb-signature.log'))
            command([validator, stage / name / 'EFI/OC/config.plist'], logs / (name + '-usb-ocvalidate.log'))
        require(tree_hashes(source) == initial, 'Active EFI changed during staging')
    summary = {'local_package': str(package.relative_to(ROOT)), 'staged_package': str(stage) if stage else None,
               'status': report['status'], 'active_efi_unchanged': True,
               'package_sha256': tree_hashes(package)}
    (output / 'preparation-report.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps({k: v for k, v in summary.items() if k != 'package_sha256'}, indent=2))


if __name__ == '__main__':
    try:
        main()
    except (ValueError, KeyError, OSError, subprocess.SubprocessError) as error:
        sys.exit(str(error))
