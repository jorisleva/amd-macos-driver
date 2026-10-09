#!/usr/bin/env python3
"""Prepare Navi48Native boot injection; optionally deploy ONLY to PROBE1401.

Preserve OPENCORE, SIP, CPU patches, SMBIOS and all other OpenCore settings.
Never hot-load, install a system kext, format, modify NVRAM or reboot. Private
EFI copies stay in out/ or on the trial USB. A prepared/deployed EFI is NOT
proof of kernel loading, firmware execution, GPU DMA or acceleration.
"""
import argparse
import copy
import datetime
import importlib.util
import json
import os
from pathlib import Path
import plistlib
import re
import subprocess
import sys

from native_kext_audit import PRODUCT, VERSION, BUNDLE_ID, SOURCE_FILES, audit_sources
from pinned_downloads import digest, extract_zip, fetch, local_output

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('pci_efi_helpers', ROOT / 'tools/prepare-pci-probe-efi.py')
helpers = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(helpers)
require, tree_hashes, copy_verified, command = (helpers.require, helpers.tree_hashes,
                                              helpers.copy_verified, helpers.command)
REFERENCE = Path('/Volumes/OPENCORE/EFI')
TRIAL = Path('/Volumes/PROBE1401')
TRIAL_UUID = '0551A151-04F6-36DF-8852-169828D00C66'
GUID, DARWIN = helpers.GUID, helpers.DARWIN


def injection_entry():
    return {'Arch': 'x86_64', 'BundlePath': PRODUCT + '.kext',
            'Comment': 'Native PCI/DMA preparation ' + VERSION + ' - GPU firmware blocked',
            'Enabled': True, 'ExecutablePath': 'Contents/MacOS/' + PRODUCT,
            'MinKernel': DARWIN, 'MaxKernel': DARWIN, 'PlistPath': 'Contents/Info.plist'}


def boot_delta(offset, size):
    require(type(offset) is int and type(size) is int and offset >= 0 and
            size >= 24 * 1024 * 1024 and not (offset & 0xffff) and not (size & 0xfff) and
            offset + size <= 256 * 1024 * 1024, 'Candidate must fit the reviewed 256 MiB BAR0 profile')
    # These parameters select a READ-ONLY placement candidate, NOT free VRAM.
    return (' navi48-native-platform=1 navi48-native-scratch-offset=' + hex(offset) +
            ' navi48-native-scratch-bytes=' + hex(size))


def validate_baseline(config):
    helpers.validate_baseline(config)
    tokens = config['NVRAM']['Add'][GUID]['boot-args'].split()
    require(not any(t.split('=')[0].startswith('navi48-native-') for t in tokens),
            'Baseline already contains native-driver parameters')


def validate_profile(baseline, profile, offset, size):
    validate_baseline(baseline)
    restored = copy.deepcopy(profile)
    require(len(restored['Kernel']['Add']) == len(baseline['Kernel']['Add']) + 1,
            'Unexpected injection count')
    require(plistlib.dumps(restored['Kernel']['Add'].pop()) == plistlib.dumps(injection_entry()),
            'Unreviewed native injection entry')
    original = baseline['NVRAM']['Add'][GUID]['boot-args']
    require(restored['NVRAM']['Add'][GUID]['boot-args'] == original + boot_delta(offset, size),
            'Unexpected native boot parameters')
    restored['NVRAM']['Add'][GUID]['boot-args'] = original
    require(plistlib.dumps(restored) == plistlib.dumps(baseline),
            'Changes outside Kernel/Add and boot-args are forbidden')


def make_profile(baseline, offset, size):
    validate_baseline(baseline)
    result = copy.deepcopy(baseline)
    result['Kernel']['Add'].append(injection_entry())
    result['NVRAM']['Add'][GUID]['boot-args'] += boot_delta(offset, size)
    validate_profile(baseline, result, offset, size)
    return result


def validate_trial(volume, info):
    require(volume == TRIAL and volume.resolve() == volume,
            'Deployment is restricted to the explicit PROBE1401 mount')
    require(info.get('MountPoint') == str(TRIAL) and info.get('VolumeName') == 'PROBE1401' and
            info.get('VolumeUUID') == TRIAL_UUID and info.get('FilesystemType') == 'msdos' and
            info.get('BusProtocol') == 'USB' and info.get('Internal') is False and
            info.get('WritableVolume') is True, 'Unexpected trial USB identity/filesystem')


def deploy_verified(profile, volume, reference, reference_hashes, current_hashes, tag, validate):
    """Verified whole-tree swap with rollback; never merge two EFI trees."""
    require(bool(re.fullmatch(r'native-[A-Za-z0-9_.-]+', tag)), 'Invalid trial directory tag')
    active = volume / 'EFI'
    staging = volume / ('EFI.NEXT-' + tag)
    backup = volume / ('EFI.BACKUP-' + tag)
    require(volume.resolve() == volume and not reference.resolve().is_relative_to(volume),
            'Reference and trial must be separate real paths')
    for path in (staging, backup):
        require(not path.exists() and not path.is_symlink(), 'Trial backup/staging already exists')
    require(tree_hashes(active) == current_hashes and tree_hashes(reference) == reference_hashes,
            'An EFI changed before deployment')
    expected = copy_verified(profile, staging)
    validate(staging)
    require(tree_hashes(staging) == expected and tree_hashes(active) == current_hashes and
            tree_hashes(reference) == reference_hashes, 'An EFI changed during staging')
    moved_old, activated = False, False
    try:
        active.rename(backup); moved_old = True
        staging.rename(active); activated = True
        validate(active)
        subprocess.run(['/bin/sync'], check=True, timeout=30)
        require(tree_hashes(active) == expected and tree_hashes(backup) == current_hashes and
                tree_hashes(reference) == reference_hashes, 'Post-deployment verification failed')
    except Exception:
        if activated:
            active.rename(staging)
        if moved_old:
            backup.rename(active)
        require(tree_hashes(active) == current_hashes, 'Rollback failed: keep both USB EFI trees for recovery')
        raise
    return {'trial_efi_replaced': True, 'trial_files': len(expected), 'backup': str(backup),
            'backup_verified': True, 'reference_efi_unchanged': True}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--candidate-offset', type=lambda s: int(s, 0), required=True)
    parser.add_argument('--candidate-bytes', type=lambda s: int(s, 0), required=True)
    parser.add_argument('--deploy-probe1401', action='store_true')
    args = parser.parse_args()
    require(sys.platform == 'darwin', 'macOS is required')
    destination, build = local_output(ROOT, args.output), local_output(ROOT, args.build)
    require(not destination.exists(), 'Choose a fresh output directory')
    require(subprocess.check_output(['uname', '-m'], text=True).strip() == 'x86_64' and
            subprocess.check_output(['uname', '-r'], text=True).strip() == DARWIN and
            subprocess.check_output(['sw_vers', '-buildVersion'], text=True).strip() == '25G241',
            'Host must match the reviewed Tahoe x86_64 build')
    current_args = subprocess.check_output(['sysctl', '-n', 'kern.bootargs'], text=True).split()
    require(not any(t.startswith(('navi48-native-', 'navi48-pci-probe=1')) for t in current_args),
            'Return to the reference boot before changing the trial EFI')
    loaded = subprocess.check_output(['kmutil', 'showloaded'], stderr=subprocess.PIPE, text=True)
    require(not any(name in loaded for name in (BUNDLE_ID, helpers.BUNDLE_ID, 'com.navi48.bringup')),
            'An experimental GPU module is already loaded')
    require(os.path.ismount(REFERENCE.parent) and REFERENCE.resolve() == REFERENCE, 'Reference USB must be mounted')
    initial = tree_hashes(REFERENCE)
    baseline = plistlib.loads((REFERENCE / 'OC/config.plist').read_bytes())
    profile = make_profile(baseline, args.candidate_offset, args.candidate_bytes)
    require({p.name for p in (REFERENCE / 'OC/Kexts').iterdir() if p.is_dir()} == helpers.BASE_KEXTS,
            'Unexpected reference kext directory')
    current = None
    if args.deploy_probe1401:
        require(os.path.ismount(TRIAL), 'Trial USB must be mounted')
        validate_trial(TRIAL, plistlib.loads(subprocess.check_output(['diskutil', 'info', '-plist', TRIAL])))
        current = tree_hashes(TRIAL / 'EFI')
        # First native deployment is only from the previously reviewed probe ON
        # profile; any additional USB modifications require a separate review.
        trial_config = plistlib.loads((TRIAL / 'EFI/OC/config.plist').read_bytes())
        helpers.validate_profile(baseline, trial_config, True)
        require({p.name for p in (TRIAL / 'EFI/OC/Kexts').iterdir() if p.is_dir()} ==
                helpers.BASE_KEXTS | {'Navi48PciProbe.kext'}, 'Unexpected existing trial kext directory')
    artifact = build / (PRODUCT + '.kext')
    sums = tree_hashes(artifact)
    report = json.loads((build / 'build-report.json').read_text())
    require(report['status'] == 'kext-built-signed-not-load-qualified' and report['version'] == VERSION and
            report['dma_allocator_invoked_by_service'] is True and report['firmwares_verified_in_kext'] == 10 and
            report['warning_count'] == 0, 'Unreviewed native build')
    require(sums == json.loads((build / 'SHA256SUMS.json').read_text()) and
            digest(artifact / 'Contents/MacOS' / PRODUCT) == report['executable_sha256'], 'Modified kext artifact')
    audit_sources(build / 'source/driver')
    for name in SOURCE_FILES:
        require(digest(ROOT / 'kexts' / PRODUCT / name) == report['source_sha256']['driver/' + name] ==
                digest(build / 'source/driver' / name), 'Driver source differs from build: ' + name)
    for name in ('IOKitController.cpp', 'IOKitController.hpp'):
        require(digest(ROOT / 'native/Navi48FirmwareCore' / name) == report['source_sha256']['core/' + name] ==
                digest(build / 'source/core' / name), 'Controller source differs from build: ' + name)
    destination.mkdir(parents=True)
    logs = destination / 'validation'; logs.mkdir()
    command(['codesign', '--verify', '--strict', '--verbose=4', artifact], logs / 'signature.log')
    lock = json.loads((ROOT / 'boot/ryzen5600x-b550/sources.lock.json').read_text())
    archive = fetch(lock['inputs']['opencore'], ROOT / 'out/downloads/opencore', offline=True)
    extract_zip(archive, destination / 'opencore-tools')
    oc = destination / 'opencore-tools'
    for name in ('BOOT/BOOTx64.efi', 'OC/OpenCore.efi'):
        require(initial[name] == digest(oc / 'X64/EFI' / name), 'Reference is not pinned OpenCore 1.0.8 DEBUG')
    validator = oc / 'Utilities/ocvalidate/ocvalidate'; validator.chmod(0o755)
    imports = command(['xcrun', 'nm', '-u', artifact / 'Contents/MacOS' / PRODUCT], logs / 'imports.txt')
    kc = Path('/System/Library/KernelCollections/BootKernelExtensions.kc')
    exports = command(['xcrun', 'nm', '-arch', 'x86_64', '-gU', kc], logs / 'boot-kc-symbols.txt')
    symbols = helpers.symbol_check(imports, exports)
    require(symbols['imports_found'] == report['import_audit']['undefined_symbols'], 'Import count differs from build')
    copy_verified(REFERENCE, destination / 'reference/EFI')
    efi = destination / 'native/EFI'
    copy_verified(REFERENCE, efi)
    (efi / 'OC/config.plist').write_bytes(plistlib.dumps(profile, sort_keys=False))
    copy_verified(artifact, efi / 'OC/Kexts' / artifact.name)
    hashes = tree_hashes(efi)
    require(sorted(n for n in initial if hashes.get(n) != initial[n]) == ['OC/config.plist'] and
            sorted(hashes.keys() - initial.keys()) == sorted('OC/Kexts/' + artifact.name + '/' + n for n in sums),
            'Unreviewed EFI file changes')

    def validate(path):
        validate_profile(baseline, plistlib.loads((path / 'OC/config.plist').read_bytes()),
                         args.candidate_offset, args.candidate_bytes)
        label = path.parent.name + '-' + path.name
        command([validator, path / 'OC/config.plist'], logs / ('ocvalidate-' + label + '.log'))
        command(['codesign', '--verify', '--strict', path / 'OC/Kexts' / artifact.name],
                logs / ('signature-' + label + '.log'))

    validate(efi)
    require(tree_hashes(REFERENCE) == initial, 'Reference EFI changed during preparation')
    result = {'schema_version': 1, 'status': 'prepared-not-boot-tested',
              'created_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
              'product': PRODUCT, 'version': VERSION, 'build': str(build.relative_to(ROOT)),
              'binary_sha256': report['executable_sha256'], 'boot_kc_sha256': digest(kc),
              'symbol_check': symbols, 'profile_config_sha256': hashes['OC/config.plist'],
              'candidate_bar0_offset': args.candidate_offset, 'candidate_bytes': args.candidate_bytes,
              'candidate_is_vram_reservation': False, 'dma_start_allocation_bytes': 65536,
              'private_smbios_included': True, 'reference_files': len(initial), 'trial_files': len(hashes),
              'reference_efi_unchanged': True, 'trial_efi_replaced': False,
              'system_kext_installed': False, 'kernel_loaded': False, 'boot_test_performed': False,
              'firmware_sent': False, 'gpu_initialized': False, 'gpu_dma_validated': False,
              'security_settings_changed': False, 'reboot_performed': False}
    (destination / 'reference-hashes.json').write_text(json.dumps(initial, indent=2) + '\n')
    (destination / 'native-hashes.json').write_text(json.dumps(hashes, indent=2) + '\n')
    if current is not None:
        copy_verified(TRIAL / 'EFI', destination / 'previous-trial/EFI')
        result.update(deploy_verified(efi, TRIAL, REFERENCE, initial, current, destination.name, validate))
        result['status'] = 'deployed-to-trial-usb-not-boot-tested'
    (destination / 'preparation-report.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    try:
        main()
    except (ValueError, KeyError, OSError, subprocess.SubprocessError) as error:
        sys.exit(str(error))
