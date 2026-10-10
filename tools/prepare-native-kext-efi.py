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


def injection_entry(compute=False, version=VERSION):
    comment = ('Experimental GPU init + two shaders ' + version + ' - reboot-only teardown' if compute else
               'Native PCI/DMA preparation ' + version + ' - GPU firmware blocked')
    return {'Arch': 'x86_64', 'BundlePath': PRODUCT + '.kext',
            'Comment': comment,
            'Enabled': True, 'ExecutablePath': 'Contents/MacOS/' + PRODUCT,
            'MinKernel': DARWIN, 'MaxKernel': DARWIN, 'PlistPath': 'Contents/Info.plist'}


def boot_delta(offset, size, compute=False, shell=False):
    require(type(offset) is int and type(size) is int and offset >= 0 and
            size >= 24 * 1024 * 1024 and not (offset & 0xffff) and not (size & 0xfff) and
            offset + size <= 256 * 1024 * 1024, 'Candidate must fit the reviewed 256 MiB BAR0 profile')
    if compute:
        require(not (offset & 0xfffff) and size >= 32 * 1024 * 1024,
                'Compute scratch requires MiB alignment and at least 32 MiB')
    require(not shell or compute, 'Read-only shell requires the compute trial')
    # Still NOT a proof/reservation of free VRAM. Compute explicitly authorizes
    # an experimental takeover/writes rather than fabricating old RO Claims.
    # The shell is read-only (no writes/submission/power) and needs compute+risk.
    return (' navi48-native-platform=1 navi48-native-scratch-offset=' + hex(offset) +
            ' navi48-native-scratch-bytes=' + hex(size) +
            (' navi48-native-compute=1 navi48-native-risk=1' if compute else '') +
            (' navi48-native-shell=1' if shell else ''))


def validate_baseline(config):
    helpers.validate_baseline(config)
    tokens = config['NVRAM']['Add'][GUID]['boot-args'].split()
    require(not any(t.split('=')[0].startswith('navi48-native-') for t in tokens),
            'Baseline already contains native-driver parameters')


def validate_profile(baseline, profile, offset, size, compute=False, version=VERSION, shell=False):
    validate_baseline(baseline)
    restored = copy.deepcopy(profile)
    require(len(restored['Kernel']['Add']) == len(baseline['Kernel']['Add']) + 1,
            'Unexpected injection count')
    require(plistlib.dumps(restored['Kernel']['Add'].pop()) == plistlib.dumps(injection_entry(compute, version)),
            'Unreviewed native injection entry')
    original = baseline['NVRAM']['Add'][GUID]['boot-args']
    require(restored['NVRAM']['Add'][GUID]['boot-args'] == original + boot_delta(offset, size, compute, shell),
            'Unexpected native boot parameters')
    restored['NVRAM']['Add'][GUID]['boot-args'] = original
    require(plistlib.dumps(restored) == plistlib.dumps(baseline),
            'Changes outside Kernel/Add and boot-args are forbidden')


def make_profile(baseline, offset, size, compute=False, shell=False):
    validate_baseline(baseline)
    result = copy.deepcopy(baseline)
    result['Kernel']['Add'].append(injection_entry(compute))
    result['NVRAM']['Add'][GUID]['boot-args'] += boot_delta(offset, size, compute, shell)
    validate_profile(baseline, result, offset, size, compute, VERSION, shell)
    return result


PREVIOUS_COMPUTE_TRIALS = {
    # Each retired trial may only be replaced by its reviewed successor.
    # The pinned executable hash prevents silent binary substitution.
    '0.2.0': ('0.2.1', '34ce08473ac2acedd0a9094420cbd4ebf159bf48b21a0134095715cfd95809f9'),
    '0.2.1': ('0.2.2', '441140b8f86095f0bd06e7c77d61f90ae20518e0fa40171e35dd270bd49e01da'),
    '0.2.2': ('0.2.3', '46fe9e475150d275dde9986a98591dc5acd92b20ff506b6853e35cfe0a5de62a'),
    '0.2.3': ('0.2.4', '61fc8ffc684fce66d8898db4f2b8dd7143b6dea288ecb25d7f1de613c1a4eeec'),
    '0.2.4': ('0.2.5', '9fe464d7cffac6c0cdd2b7dcf77244cad96d05d1c726b2059f8fc0fead2bef9e'),
    '0.2.5': ('0.2.6', 'c1e6c4bb565380bb4a7585a212de6b4e90dca4320c532bebb467716ec403ddd0'),
    '0.2.6': ('0.2.7', 'efcf2e8f823c3be7485a536f6819bfe10cf686d243b207bcacb8c7a361da67c0'),
    '0.2.7': ('0.2.8', '2409a00710dd276ff8c19848e568fca04f0793326294ef9da70af8a6f116ec37'),
    '0.2.8': ('0.2.9', '8871d7f167de34ce98d14780c62d3608d9d7530f3a7bc7eaa59bc468e4f8c0bd'),
    '0.2.9': ('0.2.10', '0bada18712b5bd2e31424ac40ea3996a4d70e277020d4e906a9bad1b4be09d07'),
}
def validate_previous_compute_trial(baseline, profile, current, reviewed, expected_version):
    """Only an already deployed, hashed 64+64 MiB compute trial may be replaced."""
    require(expected_version in PREVIOUS_COMPUTE_TRIALS, 'Unreviewed replacement source version')
    successor, binary = PREVIOUS_COMPUTE_TRIALS[expected_version]
    require(VERSION == successor, 'Replacement build must be the reviewed successor ' + successor)
    validate_profile(baseline, profile, 0x4000000, 0x4000000, compute=True, version=expected_version)
    require(current == reviewed and
            current.get('OC/Kexts/Navi48Native.kext/Contents/MacOS/Navi48Native') == binary,
            'Native ' + expected_version + ' EFI differs from the previously reviewed deployment')


def validate_previous_compute_0_2_0(baseline, profile, current, reviewed):
    validate_previous_compute_trial(baseline, profile, current, reviewed, '0.2.0')


RETIRED_BOOT_MODULES = {
    '0.2.0': ('0.2.0', 'AB1F0B3A-865C-33FC-BC6B-5CA0056BBF45'),
    '0.2.1': ('0.2.1', '62893962-8984-3EE0-B636-40E33C9F073D'),
    '0.2.2': ('0.2.2', 'E88B08E7-29B2-3BF1-A836-D7270C52B6AC'),
    '0.2.3': ('0.2.3', '1E9DDB5A-0841-3DE1-9999-0D854E226E14'),
    '0.2.4': ('0.2.4', '4F894D5F-FDC9-384C-8688-C6B4BAA68674'),
    '0.2.5': ('0.2.5', '160033C1-04F7-3DE5-A795-A511A749F8F5'),
    '0.2.6': ('0.2.6', 'BAD9777A-7C72-338A-A6B8-01FA4A3D28C3'),
    '0.2.7': ('0.2.7', '84064BBD-DD39-3B69-9F7E-22863BCFFEAE'),
    '0.2.8': ('0.2.8', 'DBD6962B-7594-3B64-9B1A-BAAEAFC07D03'),
    '0.2.9': ('0.2.9', '809701A2-4A08-39F7-AA30-9CE65ECE4C7B'),
}
def validate_boot_session(baseline, boot_args, loaded, allow_retired=False,
                          native_service='', native_instances=None, retired_source=None,
                          allow_retained=False):
    """EFI files ONLY: no unload/retry/MMIO, even in the reviewed retired boot.

    This exception is not a claim that hardware was untouched. It authorizes
    an offline USB update from the exact loaded trial, with zero native
    instances, rather than asking for an extra reference reboot just to copy.
    With allow_retained (explicit user-forced copy from a retained boot), a
    live native service holding GPU resources is TOLERATED: the loaded kext
    is already in memory and never re-reads USB files, so the copy cannot
    perturb it. Recorded transparently; hardware-touched stays unknown.
    """
    experimental = (BUNDLE_ID, helpers.BUNDLE_ID, 'com.navi48.bringup')
    native_args = any(t.startswith(('navi48-native-', 'navi48-pci-probe=1')) for t in boot_args)
    if not native_args and not any(name in loaded for name in experimental):
        return False
    require(allow_retired, 'Return to the reference boot before changing the trial EFI')
    require(retired_source in RETIRED_BOOT_MODULES, 'Unreviewed retired boot source')
    expected_args = (baseline['NVRAM']['Add'][GUID]['boot-args'] +
                     boot_delta(0x4000000, 0x4000000, compute=True)).split()
    modules = re.findall(re.escape(BUNDLE_ID) + r'\s+\(([^)]+)\)\s+([0-9A-Fa-f-]+)', loaded)
    require(boot_args == expected_args and modules == [RETIRED_BOOT_MODULES[retired_source]] and
            not any(name in loaded for name in experimental[1:]),
            'Only the reviewed ' + retired_source + ' boot permits this offline USB update')
    if allow_retained:
        # Forced copy from the exact retained boot: the live service is
        # expected (it holds GPU resources until reboot). Still require an
        # integer instance count so the record states what was live.
        require(type(native_instances) is int,
                'Unreviewed IORegistry instance count for forced retained copy')
        return 'retained-forced'
    require(not native_service.strip() and type(native_instances) is int and native_instances == 0,
            'Only the reviewed retired ' + retired_source + ' boot permits this offline USB update')
    return True


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
    parser.add_argument('--experimental-compute', action='store_true', help='Explicit risky GPU initialization + two one-shot shaders')
    parser.add_argument('--read-only-shell', dest='read_only_shell', action='store_true', help='Explicit read-only interactive shell (no writes/submission/power); requires compute trial and adds navi48-native-shell=1')
    replacements = parser.add_mutually_exclusive_group()
    replacements.add_argument('--replace-native-0.1.2', dest='replace_native_0_1_2', action='store_true', help='Replace ONLY the previously hashed native 0.1.2 trial')
    replacements.add_argument('--replace-native-0.2.0', dest='replace_native_0_2_0', action='store_true', help='Replace ONLY the reviewed compute 0.2.0 EFI with diagnostic 0.2.1; permits offline copy from its exact retired boot')
    replacements.add_argument('--replace-native-0.2.1', dest='replace_native_0_2_1', action='store_true', help='Replace ONLY the reviewed diagnostic 0.2.1 EFI with field-level 0.2.2; permits offline copy from its exact retired boot')
    replacements.add_argument('--replace-native-0.2.2', dest='replace_native_0_2_2', action='store_true', help='Replace ONLY the reviewed field-level 0.2.2 EFI with descriptor-identity 0.2.3; permits offline copy from its exact retired boot')
    replacements.add_argument('--replace-native-0.2.3', dest='replace_native_0_2_3', action='store_true', help='Replace ONLY the reviewed descriptor-identity 0.2.3 EFI with declared-adoption 0.2.4; permits offline copy from its exact retired boot')
    replacements.add_argument('--replace-native-0.2.4', dest='replace_native_0_2_4', action='store_true', help='Replace ONLY the reviewed declared-adoption 0.2.4 EFI with accelerator-preflight 0.2.5; permits offline copy from its exact retired boot')
    replacements.add_argument('--replace-native-0.2.5', dest='replace_native_0_2_5', action='store_true', help='Replace ONLY the reviewed accelerator-preflight 0.2.5 EFI with RW-diagnostics 0.2.6; permits offline copy from its exact retired boot')
    replacements.add_argument('--replace-native-0.2.6', dest='replace_native_0_2_6', action='store_true', help='Replace ONLY the reviewed RW-diagnostics 0.2.6 EFI with RW-adoption 0.2.7; permits offline copy from its exact retired boot')
    replacements.add_argument('--replace-native-0.2.7', dest='replace_native_0_2_7', action='store_true', help='Replace ONLY the reviewed RW-adoption 0.2.7 EFI with stage-16-detail 0.2.8; permits offline copy from its exact retired boot')
    replacements.add_argument('--replace-native-0.2.8', dest='replace_native_0_2_8', action='store_true', help='Replace ONLY the reviewed stage-16-detail 0.2.8 EFI with GC-survey 0.2.9; permits offline copy OR explicit forced copy from its exact retained boot')
    replacements.add_argument('--replace-native-0.2.9', dest='replace_native_0_2_9', action='store_true', help='Replace ONLY the reviewed GC-survey 0.2.9 EFI with shell+phases 0.2.10; permits offline copy OR explicit forced copy from its exact retained boot')
    args = parser.parse_args()
    require(sys.platform == 'darwin', 'macOS is required')
    require(not (args.replace_native_0_1_2 or args.replace_native_0_2_0 or args.replace_native_0_2_1 or
                 args.replace_native_0_2_2 or args.replace_native_0_2_3 or args.replace_native_0_2_4 or
                 args.replace_native_0_2_5 or args.replace_native_0_2_6 or args.replace_native_0_2_7 or
                 args.replace_native_0_2_8 or args.replace_native_0_2_9) or
            (args.deploy_probe1401 and args.experimental_compute),
            'Replacing a native trial requires explicit compute deployment')
    if args.replace_native_0_2_0:
        require(VERSION == '0.2.1' and args.candidate_offset == 0x4000000 and args.candidate_bytes == 0x4000000,
                'The reviewed 0.2.0 replacement is restricted to diagnostic 0.2.1 with unchanged 64+64 MiB scratch')
    if args.replace_native_0_2_1:
        require(VERSION == '0.2.2' and args.candidate_offset == 0x4000000 and args.candidate_bytes == 0x4000000,
                'The reviewed 0.2.1 replacement is restricted to field-level 0.2.2 with unchanged 64+64 MiB scratch')
    if args.replace_native_0_2_2:
        require(VERSION == '0.2.3' and args.candidate_offset == 0x4000000 and args.candidate_bytes == 0x4000000,
                'The reviewed 0.2.2 replacement is restricted to descriptor-identity 0.2.3 with unchanged 64+64 MiB scratch')
    if args.replace_native_0_2_3:
        require(VERSION == '0.2.4' and args.candidate_offset == 0x4000000 and args.candidate_bytes == 0x4000000,
                'The reviewed 0.2.3 replacement is restricted to declared-adoption 0.2.4 with unchanged 64+64 MiB scratch')
    if args.replace_native_0_2_4:
        require(VERSION == '0.2.5' and args.candidate_offset == 0x4000000 and args.candidate_bytes == 0x4000000,
                'The reviewed 0.2.4 replacement is restricted to accelerator-preflight 0.2.5 with unchanged 64+64 MiB scratch')
    if args.replace_native_0_2_5:
        require(VERSION == '0.2.6' and args.candidate_offset == 0x4000000 and args.candidate_bytes == 0x4000000,
                'The reviewed 0.2.5 replacement is restricted to RW-diagnostics 0.2.6 with unchanged 64+64 MiB scratch')
    if args.replace_native_0_2_6:
        require(VERSION == '0.2.7' and args.candidate_offset == 0x4000000 and args.candidate_bytes == 0x4000000,
                'The reviewed 0.2.6 replacement is restricted to RW-adoption 0.2.7 with unchanged 64+64 MiB scratch')
    if args.replace_native_0_2_7:
        require(VERSION == '0.2.8' and args.candidate_offset == 0x4000000 and args.candidate_bytes == 0x4000000,
                'The reviewed 0.2.7 replacement is restricted to stage-16-detail 0.2.8 with unchanged 64+64 MiB scratch')
    if args.replace_native_0_2_8:
        require(VERSION == '0.2.9' and args.candidate_offset == 0x4000000 and args.candidate_bytes == 0x4000000,
                'The reviewed 0.2.8 replacement is restricted to GC-survey 0.2.9 with unchanged 64+64 MiB scratch')
    if args.replace_native_0_2_9:
        require(VERSION == '0.2.10' and args.candidate_offset == 0x4000000 and args.candidate_bytes == 0x4000000,
                'The reviewed 0.2.9 replacement is restricted to shell+phases 0.2.10 with unchanged 64+64 MiB scratch')
    destination, build = local_output(ROOT, args.output), local_output(ROOT, args.build)
    require(not destination.exists(), 'Choose a fresh output directory')
    require(subprocess.check_output(['uname', '-m'], text=True).strip() == 'x86_64' and
            subprocess.check_output(['uname', '-r'], text=True).strip() == DARWIN and
            subprocess.check_output(['sw_vers', '-buildVersion'], text=True).strip() == '25G241',
            'Host must match the reviewed Tahoe x86_64 build')
    current_args = subprocess.check_output(['sysctl', '-n', 'kern.bootargs'], text=True).split()
    loaded = subprocess.check_output(['kmutil', 'showloaded'], stderr=subprocess.PIPE, text=True)
    require(os.path.ismount(REFERENCE.parent) and REFERENCE.resolve() == REFERENCE, 'Reference USB must be mounted')
    initial = tree_hashes(REFERENCE)
    baseline = plistlib.loads((REFERENCE / 'OC/config.plist').read_bytes())
    native_service, native_instances = '', None
    allow_retained_copy = bool(args.replace_native_0_2_8 or args.replace_native_0_2_9)
    retired_source = ('0.2.0' if args.replace_native_0_2_0 else
                      '0.2.1' if args.replace_native_0_2_1 else
                      '0.2.2' if args.replace_native_0_2_2 else
                      '0.2.3' if args.replace_native_0_2_3 else
                      '0.2.4' if args.replace_native_0_2_4 else
                      '0.2.5' if args.replace_native_0_2_5 else
                      '0.2.6' if args.replace_native_0_2_6 else
                      '0.2.7' if args.replace_native_0_2_7 else
                      '0.2.8' if args.replace_native_0_2_8 else ('0.2.9' if args.replace_native_0_2_9 else None))
    if retired_source is not None:
        native_service = subprocess.check_output(['ioreg', '-r', '-c', PRODUCT, '-l', '-w', '0'], text=True)
        diagnostics = plistlib.loads(subprocess.check_output(['ioreg', '-l', '-d', '1', '-a']))
        roots = [diagnostics] if isinstance(diagnostics, dict) else diagnostics
        require(isinstance(roots, list) and len(roots) == 1 and isinstance(roots[0], dict),
                'Unreviewed IORegistry diagnostics root')
        native_instances = roots[0].get('IOKitDiagnostics', {}).get('Classes', {}).get(PRODUCT)
    retired_boot = validate_boot_session(baseline, current_args, loaded, retired_source is not None,
                                         native_service, native_instances, retired_source,
                                         allow_retained=allow_retained_copy)
    profile = make_profile(baseline, args.candidate_offset, args.candidate_bytes, args.experimental_compute)
    require({p.name for p in (REFERENCE / 'OC/Kexts').iterdir() if p.is_dir()} == helpers.BASE_KEXTS,
            'Unexpected reference kext directory')
    current = None
    if args.deploy_probe1401:
        require(os.path.ismount(TRIAL), 'Trial USB must be mounted')
        validate_trial(TRIAL, plistlib.loads(subprocess.check_output(['diskutil', 'info', '-plist', TRIAL])))
        current = tree_hashes(TRIAL / 'EFI')
        # Each allowed source profile is exact, independently reviewed/hashed.
        # The replacement flags never authorize an arbitrary native EFI update.
        trial_config = plistlib.loads((TRIAL / 'EFI/OC/config.plist').read_bytes())
        if args.replace_native_0_1_2:
            validate_profile(baseline, trial_config, 0x4000000, 0x1800000, version='0.1.2')
            previous = ROOT / 'out/efi-native/native-0.1.2-trial/native-hashes.json'
            require(current == json.loads(previous.read_text()) and
                    current['OC/Kexts/Navi48Native.kext/Contents/MacOS/Navi48Native'] ==
                    'ce1f4e39a2795bdabd3789fb8d632fdac3ed3209deab851f30429ca418696e23',
                    'Native 0.1.2 EFI differs from the previously reviewed deployment')
            expected_kexts = helpers.BASE_KEXTS | {'Navi48Native.kext'}
        elif args.replace_native_0_2_0 or args.replace_native_0_2_1 or args.replace_native_0_2_2 or args.replace_native_0_2_3 or args.replace_native_0_2_4 or args.replace_native_0_2_5 or args.replace_native_0_2_6 or args.replace_native_0_2_7 or args.replace_native_0_2_8 or args.replace_native_0_2_9:
            retired = ('0.2.0' if args.replace_native_0_2_0 else
                       '0.2.1' if args.replace_native_0_2_1 else
                       '0.2.2' if args.replace_native_0_2_2 else
                       '0.2.3' if args.replace_native_0_2_3 else
                       '0.2.4' if args.replace_native_0_2_4 else
                       '0.2.5' if args.replace_native_0_2_5 else
                       '0.2.6' if args.replace_native_0_2_6 else
                       '0.2.7' if args.replace_native_0_2_7 else
                       '0.2.8' if args.replace_native_0_2_8 else '0.2.9')
            previous = ROOT / {'0.2.0': 'out/efi-native/native-0.2.0-compute-trial',
                               '0.2.1': 'out/efi-native/native-0.2.1-diagnostics-trial',
                               '0.2.2': 'out/efi-native/native-0.2.2-mapping-trial',
                               '0.2.3': 'out/efi-native/native-0.2.3-descriptor-trial',
                               '0.2.4': 'out/efi-native/native-0.2.4-adoption-trial',
                               '0.2.5': 'out/efi-native/native-0.2.5-preflight-trial',
                               '0.2.6': 'out/efi-native/native-0.2.6-rw-trial',
                               '0.2.7': 'out/efi-native/native-0.2.7-rw-adoption-trial',
                               '0.2.8': 'out/efi-native/native-0.2.8-stage16-trial',
                               '0.2.9': 'out/efi-native/native-0.2.9-gc-trial'}[retired]
            require(initial == json.loads((previous / 'reference-hashes.json').read_text()),
                    'Reference differs from the reviewed ' + retired + ' deployment')
            validate_previous_compute_trial(baseline, trial_config, current,
                                            json.loads((previous / 'native-hashes.json').read_text()), retired)
            expected_kexts = helpers.BASE_KEXTS | {'Navi48Native.kext'}
        else:
            helpers.validate_profile(baseline, trial_config, True)
            expected_kexts = helpers.BASE_KEXTS | {'Navi48PciProbe.kext'}
        require({p.name for p in (TRIAL / 'EFI/OC/Kexts').iterdir() if p.is_dir()} == expected_kexts,
                'Unexpected existing trial kext directory')
    artifact = build / (PRODUCT + '.kext')
    sums = tree_hashes(artifact)
    report = json.loads((build / 'build-report.json').read_text())
    require(report['status'] == 'kext-built-signed-not-load-qualified' and report['version'] == VERSION and
            report['dma_allocator_invoked_by_service'] is True and report['firmwares_verified_in_kext'] == 10 and
            report['warning_count'] == 0, 'Unreviewed native build')
    if args.experimental_compute:
        require(report['experimental_compute']['called_by_service'] is True and
                report['experimental_compute']['io_vm_per_page'] is True and
                report['compute_shaders_verified_in_kext'] == 2, 'Missing native compute path/shaders')
    if args.replace_native_0_2_0 or args.replace_native_0_2_1 or args.replace_native_0_2_2 or args.replace_native_0_2_3 or args.replace_native_0_2_4 or args.replace_native_0_2_5 or args.replace_native_0_2_6 or args.replace_native_0_2_7 or args.replace_native_0_2_8 or args.replace_native_0_2_9:
        require(report.get('boot_diagnostics', {}).get('resource') == 'Navi48Native,BootDiagnostics' and
                report.get('symbol_audit', {}).get('boot_diagnostic_publisher_linked') is True,
                'Missing reviewed persistent diagnostic publisher')
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
                         args.candidate_offset, args.candidate_bytes, args.experimental_compute)
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
              'experimental_compute_enabled': args.experimental_compute,
              'explicit_hardware_risk_acknowledged': args.experimental_compute,
              'two_one_shot_shaders_scheduled': args.experimental_compute,
              'native_0_1_2_replacement_acknowledged': args.replace_native_0_1_2,
              'native_0_2_0_replacement_acknowledged': args.replace_native_0_2_0,
              'native_0_2_1_replacement_acknowledged': args.replace_native_0_2_1,
              'native_0_2_2_replacement_acknowledged': args.replace_native_0_2_2,
              'native_0_2_3_replacement_acknowledged': args.replace_native_0_2_3,
              'native_0_2_4_replacement_acknowledged': args.replace_native_0_2_4,
              'native_0_2_5_replacement_acknowledged': args.replace_native_0_2_5,
              'native_0_2_6_replacement_acknowledged': args.replace_native_0_2_6,
              'native_0_2_7_replacement_acknowledged': args.replace_native_0_2_7,
              'native_0_2_8_replacement_acknowledged': args.replace_native_0_2_8,
              'native_0_2_9_replacement_acknowledged': args.replace_native_0_2_9,
              'prepared_from_reviewed_retired_boot': retired_boot,
              'retained_boot_forced_copy': retired_boot == 'retained-forced',
              'retained_native_instances_during_copy': native_instances if retired_boot == 'retained-forced' else 0,
              'retired_source_version': retired_source if retired_boot else None,
              'running_native_version_during_update': retired_source if retired_boot else None,
              'running_native_hardware_touched_reported': None, 'hot_load_performed': False,
              'gpu_command_executed': False,
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
