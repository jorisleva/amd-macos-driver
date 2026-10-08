#!/usr/bin/env python3
"""Prepare a local, validated EFI for Joris' observed Ryzen/B550; never install it."""
import argparse
import copy
import json
import os
from pathlib import Path
import plistlib
import re
import secrets
import shutil
import subprocess
import uuid
import zipfile

from pinned_downloads import digest, extract_zip, fetch, local_output

ROOT = Path(__file__).resolve().parents[1]
PROFILE = ROOT / 'boot/ryzen5600x-b550'
NVRAM_GUID = '7C436110-AB2A-4BBB-A880-FE41995C9F82'
BOOT_ARGS = '-v keepsyms=1 debug=0x100 navi48bringup=0 rdna4-off=1'
KEXTS = [('lilu', 'Lilu.kext'), ('smc', 'VirtualSMC.kext'), ('weg', 'WhateverGreen.kext'),
         ('restrict', 'RestrictEvents.kext'), ('mce', 'AppleMCEReporterDisabler.kext'),
         ('rtl', 'RealtekRTL8111.kext'), ('usb-kext', 'USBToolBox.kext'), ('usb-kext', 'UTBDefault.kext')]


def selected_kexts(usb_mode, graphics_mode='firmware'):
    if usb_mode not in ('native', 'toolbox'):
        raise ValueError('Unknown USB mode: ' + usb_mode)
    if graphics_mode not in ('firmware', 'whatevergreen'):
        raise ValueError('Unknown graphics mode: ' + graphics_mode)
    return [entry for entry in KEXTS
            if (usb_mode == 'toolbox' or entry[0] != 'usb-kext')
            and (graphics_mode == 'whatevergreen' or entry[0] != 'weg')]


def selected_acpi(usb_mode):
    selected_kexts(usb_mode)
    return ('SSDT-EC' if usb_mode == 'native' else 'SSDT-EC-USBX', 'SSDT-CPUR')


def clean_sample(value):
    if isinstance(value, dict):
        return {k: clean_sample(v) for k, v in value.items() if not k.startswith('#')}
    if isinstance(value, list):
        return [clean_sample(v) for v in value]
    return value


def config_from_sample(sample, patches, identity, usb_mode='native', graphics_mode='firmware'):
    selected_kexts(usb_mode, graphics_mode)
    config = clean_sample(copy.deepcopy(sample))
    for section, fields in {'ACPI': ['Add', 'Delete', 'Patch'], 'Booter': ['MmioWhitelist', 'Patch'],
                            'Kernel': ['Add', 'Block', 'Force', 'Patch'], 'Misc': ['Entries', 'Tools']}.items():
        for field in fields:
            config[section][field] = []
    config['DeviceProperties'] = {'Add': {}, 'Delete': {}}
    for name in selected_acpi(usb_mode):
        config['ACPI']['Add'].append({'Comment': 'B550M DS3H BIOS FD / observed namespace',
                                     'Enabled': True, 'Path': name + '.aml'})
    config['Booter']['Quirks'].update(AvoidRuntimeDefrag=True, DevirtualiseMmio=False,
        EnableSafeModeSlide=True, EnableWriteUnprotector=False, ProvideCustomSlide=True,
        RebuildAppleMemoryMap=True, SetupVirtualMap=False, SyncRuntimePermissions=True, ResizeAppleGpuBars=0)
    config['Kernel']['Emulate'].update(Cpuid1Data=bytes(16), Cpuid1Mask=bytes(16), DummyPowerManagement=True)
    config['Kernel']['Quirks'].update(ProvideCurrentCpuInfo=True, PanicNoKextDump=True,
        PowerTimeoutKernelPanic=True, DisableLinkeditJettison=True, XhciPortLimit=False,
        CustomSMBIOSGuid=True)
    config['Kernel']['Patch'] = copy.deepcopy(patches['Kernel']['Patch'])
    count = 0
    for patch in config['Kernel']['Patch']:
        if 'Force cpuid_cores_per_package' in patch['Comment']:
            replace = bytearray(patch['Replace'])
            if len(replace) not in (5, 6) or replace[0] not in (0xB8, 0xBA):
                raise ValueError('Unexpected upstream AMD core-count patch')
            replace[1:5] = (6).to_bytes(4, 'little')
            patch['Replace'] = bytes(replace)
            count += 1
    if count != 4:
        raise ValueError('Expected exactly four upstream core-count patches')
    config['Kernel']['Scheme'].update(KernelArch='x86_64', KernelCache='Auto')
    config['Misc']['Boot'].update(ShowPicker=True, HideAuxiliary=False, Timeout=0,
        PickerMode='Builtin', PickerVariant='Auto', PollAppleHotKeys=True, LauncherOption='Disabled')
    config['Misc']['Debug'].update(AppleDebug=True, ApplePanic=True, DisableWatchDog=True,
        DisplayLevel=0x80000042, Target=67, SysReport=False)
    config['Misc']['Security'].update(SecureBootModel='Disabled', Vault='Optional',
        DmgLoading='Signed', ScanPolicy=0, AllowSetDefault=False, ExposeSensitiveData=6)
    config['Misc']['Tools'] = [{'Arguments': '', 'Auxiliary': False, 'Comment': 'UEFI diagnostic shell',
        'Enabled': True, 'Flavour': 'OpenShell:UEFIShell:Shell', 'FullNvramAccess': False,
        'Name': 'OpenShell', 'Path': 'OpenShell.efi', 'RealPath': False, 'TextMode': False}]
    boot_args = BOOT_ARGS
    if graphics_mode == 'whatevergreen':
        boot_args += ' -radvesa agdpmod=pikera'
    config['NVRAM']['Add'] = {NVRAM_GUID: {'boot-args': boot_args, 'csr-active-config': bytes(4),
        'prev-lang:kbd': b'fr-FR:1', 'run-efi-updater': 'No'}}
    config['NVRAM']['Delete'] = {NVRAM_GUID: ['boot-args', 'csr-active-config', 'prev-lang:kbd']}
    config['NVRAM']['WriteFlash'] = False
    config['PlatformInfo'].update(Automatic=True, UpdateDataHub=True, UpdateNVRAM=True,
        UpdateSMBIOS=True, UpdateSMBIOSMode='Custom', CustomMemory=False)
    for key in ('DataHub', 'Memory', 'PlatformNVRAM', 'SMBIOS'):
        config['PlatformInfo'].pop(key, None)
    config['PlatformInfo']['Generic'].update(SystemProductName='MacPro7,1',
        SystemSerialNumber=identity['serial'], MLB=identity['mlb'], SystemUUID=identity['uuid'],
        ROM=bytes.fromhex(identity['rom']), ProcessorType=0, AdviseFeatures=True,
        SpoofVendor=True, SystemMemoryStatus='Auto')
    config['UEFI']['Drivers'] = [{'Arguments': '', 'Comment': '', 'Enabled': True,
        'HideVerbose': False, 'LoadEarly': False, 'Path': name} for name in ('OpenRuntime.efi', 'OpenHfsPlus.efi')]
    config['UEFI']['Output'].update(ProvideConsoleGop=True, Resolution='1920x1080', ForceResolution=False,
        DirectGopRendering=False, GopPassThrough='Disabled', UIScale=1, TextRenderer='BuiltinGraphics')
    config['UEFI']['Quirks'].update(ResizeGpuBars=-1, RequestBootVarRouting=True,
        ReleaseUsbOwnership=False)
    config['UEFI']['ReservedMemory'] = []
    return config


def validate_config(config, efi=None, usb_mode='native', graphics_mode='firmware'):
    def require(test, message):
        if not test:
            raise ValueError(message)
    require(config['Booter']['Quirks']['SetupVirtualMap'] is False, 'B550 SetupVirtualMap must be disabled')
    require(config['Kernel']['Quirks']['ProvideCurrentCpuInfo'] is True, 'AMD CPU info missing')
    require(config['Kernel']['Emulate']['DummyPowerManagement'] is True, 'AMD power-management quirk missing')
    require(config['Kernel']['Quirks']['XhciPortLimit'] is False, 'Tahoe cannot rely on XhciPortLimit')
    args = config['NVRAM']['Add'][NVRAM_GUID]['boot-args'].split()
    require(all(x in args for x in ('navi48bringup=0', 'rdna4-off=1')), 'Experimental GPU driver guards missing')
    if graphics_mode == 'firmware':
        require(not any(x.startswith(('-weg', '-rad', 'agdpmod=')) for x in args),
                'WhateverGreen arguments in firmware graphics profile')
    else:
        require(all(x in args for x in ('-radvesa', 'agdpmod=pikera')), 'WhateverGreen fallback arguments missing')
    require('-wegnoegpu' not in args and '-wegnogpu' not in args, 'The only display GPU must remain enabled')
    require(not config['DeviceProperties']['Add'], 'No GPU spoofing in this baseline')
    require(config['NVRAM']['Add'][NVRAM_GUID]['csr-active-config'] == bytes(4), 'Baseline must keep SIP enabled')
    require(config['Misc']['Boot']['LauncherOption'] == 'Disabled', 'Do not register OpenCore as default firmware boot')
    require(config['Misc']['Boot']['Timeout'] == 0, 'Initial boot must wait for an explicit menu choice')
    require(config['Misc']['Security']['DmgLoading'] == 'Signed', 'Only Apple-signed recovery images')
    core = [p for p in config['Kernel']['Patch'] if 'Force cpuid_cores_per_package' in p['Comment']]
    require(len(core) == 4 and all(int.from_bytes(p['Replace'][1:5], 'little') == 6 for p in core), 'AMD patches must use six physical cores')
    require(any(p['Enabled'] and '13.3+' in p['Comment'] and p['MaxKernel'] == '25.99.99' for p in core), 'Tahoe core-count patch missing')
    def applicable_to_tahoe(patch):
        low = tuple(int(v) for v in (patch['MinKernel'] or '0.0.0').split('.'))
        high = tuple(int(v) for v in (patch['MaxKernel'] or '99.99.99').split('.'))
        return low <= (25, 0, 0) <= high
    pat = [p for p in config['Kernel']['Patch'] if p['Enabled'] and 'mtrr_update_action' in p['Comment'] and applicable_to_tahoe(p)]
    require(len(pat) == 1 and 'algrey' in pat[0]['Comment'].lower(), 'Exactly one upstream PAT method must apply to Tahoe')
    bundles = [p['BundlePath'] for p in config['Kernel']['Add'] if p['Enabled']]
    require(set(bundles) == {name for _, name in selected_kexts(usb_mode, graphics_mode)}, 'Unexpected/missing baseline kext; experimental drivers forbidden')
    require({p['Path'] for p in config['ACPI']['Add'] if p['Enabled']} ==
            {name + '.aml' for name in selected_acpi(usb_mode)}, 'Unexpected/missing baseline SSDT')
    if usb_mode == 'native':
        require(not any(arg.startswith(('-utb', 'utbwait=')) for arg in args), 'USBToolBox arguments in native USB profile')
        require(config['UEFI']['Quirks']['ReleaseUsbOwnership'] is False, 'USB ownership trial in native USB profile')
    identity = config['PlatformInfo']['Generic']
    require(identity['SystemProductName'] == 'MacPro7,1', 'Tahoe baseline SMBIOS mismatch')
    require(bool(re.fullmatch('[A-Z0-9]{12}', identity['SystemSerialNumber'])), 'Missing local serial')
    require(bool(re.fullmatch('[A-Z0-9]{17}', identity['MLB'])), 'Missing local MLB')
    require(uuid.UUID(identity['SystemUUID']).int != 0 and len(identity['ROM']) == 6, 'Missing local UUID/ROM')
    if efi:
        oc = Path(efi) / 'OC'
        require({p.name for p in (oc/'Kexts').glob('*.kext')} == set(bundles), 'Unreferenced kext files remain in EFI')
        if usb_mode == 'native':
            require(not any((oc/'Kexts'/name).exists() for key, name in KEXTS if key == 'usb-kext'),
                    'USBToolBox files remain in native EFI')
            require(not (oc/'ACPI/SSDT-EC-USBX.aml').exists(), 'USBX AML remains in native EFI')
        for entry in config['Kernel']['Add']:
            base = oc / 'Kexts' / entry['BundlePath']
            require((base / entry['PlistPath']).is_file(), 'Missing kext plist: ' + entry['BundlePath'])
            if entry['ExecutablePath']:
                require(has_x64((base / entry['ExecutablePath']).read_bytes()), 'Kext has no x86_64 slice: ' + entry['BundlePath'])
        for entry in config['ACPI']['Add']:
            data = (oc / 'ACPI' / entry['Path']).read_bytes()
            require(data[:4] == b'SSDT' and len(data) == int.from_bytes(data[4:8], 'little') and sum(data) % 256 == 0,
                    'Invalid AML table: ' + entry['Path'])
        for path in [Path(efi) / 'BOOT/BOOTx64.efi', oc / 'OpenCore.efi'] + list((oc / 'Drivers').glob('*.efi')) + list((oc / 'Tools').glob('*.efi')):
            data = path.read_bytes()
            require(data[:2] == b'MZ', 'Invalid UEFI executable')
            offset = int.from_bytes(data[60:64], 'little')
            require(data[offset:offset+6] == b'PE\0\0\x64\x86', 'UEFI executable is not AMD64')
    return {'status': 'passed', 'physical_cores': 6, 'core_patches': len(core), 'kexts': bundles, 'usb_mode': usb_mode,
            'graphics_mode': graphics_mode,
            'injected_graphics_kexts': ['WhateverGreen.kext'] if graphics_mode == 'whatevergreen' else [],
            'gpu_acceleration': 'not-hardware-qualified', 'hardware_boot': 'not-tested'}


def has_x64(data):
    if data[:4] == b'\xcf\xfa\xed\xfe':
        return int.from_bytes(data[4:8], 'little') == 0x01000007
    if data[:4] == b'\xca\xfe\xba\xbe':
        count = int.from_bytes(data[4:8], 'big')
        return any(int.from_bytes(data[8+20*i:12+20*i], 'big') == 0x01000007 for i in range(count))
    return False


def run_logged(command, log):
    result = subprocess.run([str(x) for x in command], capture_output=True, text=True, errors='replace')
    log.write_text(result.stdout + result.stderr, encoding='utf-8')
    if result.returncode:
        raise RuntimeError(f'{Path(command[0]).name} failed; see {log}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT/'out/opencore/ryzen5600x-b550-rx9070xt')
    parser.add_argument('--offline', action='store_true')
    parser.add_argument('--usb-mode', choices=('native', 'toolbox'), default='native',
                        help='Native USB by default; toolbox restores the optional USBToolBox/USBX preparation')
    parser.add_argument('--graphics-mode', choices=('firmware', 'whatevergreen'), default='firmware',
                        help='No injected graphics kext by default; whatevergreen reproduces the previous -radvesa profile')
    args = parser.parse_args()
    if os.name != 'nt':
        raise SystemExit('This kit builder uses the pinned Windows iASL binary; run on the AMD Windows machine.')
    output = local_output(ROOT, args.output)
    if output.exists():
        raise SystemExit('Output already exists; choose a new path under out/ to preserve the previous kit.')
    output.mkdir(parents=True)
    locks = json.loads((PROFILE/'sources.lock.json').read_text())
    cache = ROOT/'out/downloads/opencore'
    sources = {}
    for key, item in locks['inputs'].items():
        archive = fetch(item, cache, args.offline)
        sources[key] = output/'inputs'/key
        if archive.suffix == '.zip':
            extract_zip(archive, sources[key])
        else:
            sources[key].mkdir(parents=True)
            shutil.copyfile(archive, sources[key]/archive.name)
    oc_src = sources['opencore']/'X64/EFI'
    identity_path = ROOT/'out/opencore/private-identity.json'
    if identity_path.exists():
        identity = json.loads(identity_path.read_text())
    else:
        result = subprocess.run([str(sources['opencore']/'Utilities/macserial/macserial.exe'), '-m', 'MacPro7,1', '-n', '1'],
                                capture_output=True, text=True, check=True)
        serial, mlb = [v.strip() for v in result.stdout.strip().split('|')]
        identity = {'serial': serial, 'mlb': mlb, 'uuid': str(uuid.uuid4()).upper(),
                    'rom': (bytes([0x02]) + secrets.token_bytes(5)).hex()}
        identity_path.write_text(json.dumps(identity, indent=2), encoding='utf-8')
    efi = output/'EFI'
    for folder in ('BOOT', 'OC/Drivers', 'OC/Kexts', 'OC/ACPI', 'OC/Tools'):
        (efi/folder).mkdir(parents=True)
    shutil.copyfile(oc_src/'BOOT/BOOTx64.efi', efi/'BOOT/BOOTx64.efi')
    # The first USB trial exposed BOOTx64 as a second, self-launching entry.
    # OpenCore honours this marker when scanning; firmware boot remains available.
    (efi/'BOOT/.contentVisibility').write_bytes(b'Disabled')
    shutil.copyfile(oc_src/'OC/OpenCore.efi', efi/'OC/OpenCore.efi')
    for name in ('OpenRuntime.efi', 'OpenHfsPlus.efi'):
        shutil.copyfile(oc_src/'OC/Drivers'/name, efi/'OC/Drivers'/name)
    shutil.copyfile(oc_src/'OC/Tools/OpenShell.efi', efi/'OC/Tools/OpenShell.efi')
    logs = output/'validation'; logs.mkdir()
    compiler = next(sources['iasl'].rglob('iasl.exe'))
    for name in selected_acpi(args.usb_mode):
        run_logged([compiler, '-p', efi/'OC/ACPI'/name, PROFILE/(name+'.dsl')], logs/(name+'.log'))
    sample = plistlib.loads((sources['opencore']/'Docs/Sample.plist').read_bytes())
    patches = plistlib.loads((sources['amd-patches']/'patches.plist').read_bytes())
    config = config_from_sample(sample, patches, identity, args.usb_mode, args.graphics_mode)
    for key, name in selected_kexts(args.usb_mode, args.graphics_mode):
        candidates = [p for p in sources[key].rglob(name) if '__MACOSX' not in p.parts and p.is_dir()]
        if len(candidates) != 1:
            raise ValueError('Ambiguous/missing upstream kext: '+name)
        shutil.copytree(candidates[0], efi/'OC/Kexts'/name)
        info = plistlib.loads((candidates[0]/'Contents/Info.plist').read_bytes())
        executable = info.get('CFBundleExecutable', '')
        config['Kernel']['Add'].append({'Arch': 'x86_64', 'BundlePath': name,
            'Comment': info.get('CFBundleVersion', ''), 'Enabled': True,
            'ExecutablePath': 'Contents/MacOS/'+executable if executable else '',
            'MaxKernel': '25.99.99', 'MinKernel': '25.0.0', 'PlistPath': 'Contents/Info.plist'})
    config_path = efi/'OC/config.plist'
    config_path.write_bytes(plistlib.dumps(config))
    semantic = validate_config(config, efi, args.usb_mode, args.graphics_mode)
    run_logged([sources['opencore']/'Utilities/ocvalidate/ocvalidate.exe', config_path], logs/'ocvalidate.log')
    recovery = output/'recovery/EFI'; shutil.copytree(efi, recovery)
    safe = copy.deepcopy(config)
    safe['NVRAM']['Add'][NVRAM_GUID]['boot-args'] += ' -x'
    (recovery/'OC/config.plist').write_bytes(plistlib.dumps(safe))
    validate_config(safe, recovery, args.usb_mode, args.graphics_mode)
    run_logged([sources['opencore']/'Utilities/ocvalidate/ocvalidate.exe', recovery/'OC/config.plist'], logs/'ocvalidate-safe-mode.log')
    notices = output/'NOTICES'; notices.mkdir()
    for item in locks['notices'].values():
        path = fetch(item, cache, args.offline)
        shutil.copyfile(path, notices/path.name)
    shutil.copyfile(PROFILE/'sources.lock.json', notices/'sources.lock.json')
    machine_profile = json.loads((PROFILE/'profile.json').read_text())
    machine_profile['usb_mode'] = args.usb_mode
    machine_profile['graphics_mode'] = args.graphics_mode
    if args.usb_mode == 'toolbox':
        machine_profile['usb_mapping'] = 'UTBDefault transitional enumeration; physical socket map not qualified'
    (output/'machine-profile.json').write_text(json.dumps(machine_profile, indent=2), encoding='utf-8')
    shutil.copyfile(ROOT/'docs/OPENCORE-TAHOE.md', output/'LIRE-AVANT-DEMARRAGE.md')
    recovery_boot = output/'com.apple.recovery.boot'
    recovery_boot.mkdir()
    (recovery_boot/'.contentDetails').write_bytes(b'Installer macOS Tahoe (Recovery)')
    readme = 'EFI pour essais Tahoe / Ryzen 5600X / B550M DS3H FD / RX 9070 XT.\n'
    readme += 'Préparation validée par ocvalidate, démarrage matériel NON testé.\n'
    readme += 'Aucun installateur macOS inclus. Voir LIRE-AVANT-DEMARRAGE.md.\n'
    readme += 'EFI/ = référence de diagnostic ; recovery/EFI/ = variante avec -x.\n'
    readme += f'USB : {args.usb_mode} ; démarrage matériel encore à qualifier.\n'
    readme += f'Graphique : {args.graphics_mode} ; reprise du framebuffer firmware non qualifiée.\n'
    readme += 'Identifiants SMBIOS personnels inclus : ne pas publier cette archive.\n'
    (output/'LIRE-MOI.txt').write_text(readme, encoding='utf-8')
    (logs/'checks.json').write_text(json.dumps(semantic, indent=2), encoding='utf-8')
    files = {str(p.relative_to(output)).replace('\\', '/'): digest(p) for p in output.rglob('*')
             if p.is_file() and 'inputs' not in p.relative_to(output).parts}
    (output/'SHA256SUMS.json').write_text(json.dumps(files, indent=2), encoding='utf-8')
    zip_path = output.with_suffix('.zip')
    with zipfile.ZipFile(zip_path, 'x', compression=zipfile.ZIP_DEFLATED) as z:
        for p in output.rglob('*'):
            if p.is_file() and 'inputs' not in p.relative_to(output).parts:
                z.write(p, p.relative_to(output))
    print(json.dumps({'kit': str(output), 'zip': str(zip_path), 'zip_sha256': digest(zip_path),
                      'validation': semantic}, indent=2))


if __name__ == '__main__':
    main()
