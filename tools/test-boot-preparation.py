#!/usr/bin/env python3
"""Reject meaningful boot-profile regressions and unsafe/corrupt local inputs."""
import argparse
import copy
import importlib.util
import json
from pathlib import Path
import plistlib
import tempfile
import zipfile
from pinned_downloads import fetch, extract_zip, local_output

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('boot_builder', ROOT/'tools/build-opencore-kit.py')
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--kit', type=Path, required=True)
    parser.add_argument('--usb-mode', choices=('native', 'toolbox'))
    parser.add_argument('--graphics-mode', choices=('firmware', 'whatevergreen'))
    args = parser.parse_args()
    config = plistlib.loads((args.kit/'EFI/OC/config.plist').read_bytes())
    bundles = {p['BundlePath'] for p in config['Kernel']['Add'] if p['Enabled']}
    usb_mode = args.usb_mode or ('toolbox' if 'USBToolBox.kext' in bundles else 'native')
    graphics_mode = args.graphics_mode or ('whatevergreen' if 'WhateverGreen.kext' in bundles else 'firmware')
    builder.validate_config(config, args.kit/'EFI', usb_mode, graphics_mode)
    mutations = {}
    wrong = copy.deepcopy(config)
    next(p for p in wrong['Kernel']['Patch'] if '13.3+' in p['Comment'] and 'cpuid_cores' in p['Comment'])['Replace'] = b'\xba\x0c\0\0\0'
    mutations['12-threads-used-as-cores'] = wrong
    wrong = copy.deepcopy(config)
    wrong['NVRAM']['Add'][builder.NVRAM_GUID]['boot-args'] += ' -wegnoegpu'
    mutations['sole-display-gpu-disabled'] = wrong
    wrong = copy.deepcopy(config)
    wrong['Kernel']['Quirks']['XhciPortLimit'] = True
    mutations['unsupported-usb-port-limit'] = wrong
    wrong = copy.deepcopy(config)
    wrong['Kernel']['Add'].append({'Enabled': True, 'BundlePath': 'Navi48Bringup.kext'})
    mutations['experimental-kext-in-baseline'] = wrong
    wrong = copy.deepcopy(config)
    next(p for p in wrong['Kernel']['Patch'] if 'Shaneee' in p['Comment'] and p['MinKernel'] == '24.0.0')['Enabled'] = True
    mutations['conflicting-tahoe-PAT'] = wrong
    wrong = copy.deepcopy(config)
    wrong['NVRAM']['Add'][builder.NVRAM_GUID]['csr-active-config'] = b'\x03\x08\0\0'
    mutations['SIP-disabled'] = wrong
    results = []
    for name, altered in mutations.items():
        try:
            builder.validate_config(altered, usb_mode=usb_mode, graphics_mode=graphics_mode)
        except ValueError as e:
            results.append({'test': name, 'status': 'rejected-as-expected', 'reason': str(e)})
        else:
            raise AssertionError('Invalid profile accepted: '+name)
    with tempfile.TemporaryDirectory(prefix='amd-boot-test-') as tmp:
        root = Path(tmp)
        (root/'input.bin').write_bytes(b'corrupt firmware')
        try:
            fetch({'file': 'input.bin', 'sha256': '0'*64, 'url': 'https://example.invalid/'}, root, offline=True)
        except ValueError:
            results.append({'test': 'corrupt-cached-firmware', 'status': 'rejected-as-expected'})
        else:
            raise AssertionError('Corrupt input accepted')
        with zipfile.ZipFile(root/'evil.zip', 'w') as z:
            z.writestr('../escaped.bin', b'bad')
        try:
            extract_zip(root/'evil.zip', root/'extracted')
        except ValueError:
            results.append({'test': 'archive-path-traversal', 'status': 'rejected-as-expected'})
        else:
            raise AssertionError('Archive traversal accepted')
        try:
            local_output(ROOT, ROOT/'EFI')
        except ValueError:
            results.append({'test': 'output-outside-repository-out', 'status': 'rejected-as-expected'})
        else:
            raise AssertionError('External destination accepted')
    print(json.dumps({'valid_kit': 'passed', 'negative_cases': results}, indent=2))


if __name__ == '__main__':
    main()
