#!/usr/bin/env python3
"""Retrieve the pinned MacKernelSDK and Navi48 firmware inputs; no installation."""
import argparse
import json
from pathlib import Path
import shutil
from pinned_downloads import digest, extract_zip, fetch, local_output

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT/'out/dependencies/amd-pinned')
    parser.add_argument('--offline', action='store_true')
    args = parser.parse_args()
    output = local_output(ROOT, args.output)
    if output.exists():
        raise SystemExit('Output already exists: choose a new directory under out/.')
    lock = json.loads((ROOT/'dependencies/sources.lock.json').read_text())['components']
    sdk, firmware = lock['mac_kernel_sdk'], lock['linux_firmware']
    if not sdk['revision'] or not firmware['revision']:
        raise ValueError('Dependency revisions must be pinned first')
    output.mkdir(parents=True)
    cache = ROOT/'out/downloads/amd'
    archive = fetch(sdk['archive'], cache, args.offline)
    extract_zip(archive, output/'sdk-source')
    sdk_root = output/'sdk-source'/('MacKernelSDK-'+sdk['revision'])
    if digest(sdk_root/'LICENSE.txt') != sdk['license_sha256']:
        raise ValueError('SDK license checksum mismatch')
    for name in ('Headers', 'Library'):
        if not (sdk_root/name).is_dir():
            raise ValueError('SDK source incomplete')
    dest = output/'linux-firmware'; (dest/'amdgpu').mkdir(parents=True)
    result = []
    base = 'https://gitlab.com/kernel-firmware/linux-firmware/-/raw/'+firmware['revision']+'/'
    for name, expected in firmware['expected_sha256'].items():
        blob = fetch({'file': name, 'sha256': expected, 'url': base+'amdgpu/'+name}, cache, args.offline)
        shutil.copyfile(blob, dest/'amdgpu'/name)
        result.append({'name': name, 'sha256': digest(blob), 'bytes': blob.stat().st_size, 'matches_expected': True})
    for name, expected in firmware['notices_sha256'].items():
        notice = fetch({'file': name, 'sha256': expected, 'url': base+name}, cache, args.offline)
        shutil.copyfile(notice, dest/name)
    report = {'status': 'source-inputs-verified-not-build-or-hardware-qualified',
              'sdk_revision': sdk['revision'], 'sdk_path': str(sdk_root),
              'firmware_revision': firmware['revision'], 'firmwares': result,
              'notices_sha256': firmware['notices_sha256'],
              'missing_normative_abi': 'notes/design/NATIVE-S1C-ABI.md absent at pinned Navi48 revision'}
    (output/'verification.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    shutil.copyfile(ROOT/'dependencies/sources.lock.json', output/'sources.lock.json')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
