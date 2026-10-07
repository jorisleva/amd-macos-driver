#!/usr/bin/env python3
"""Read Windows firmware ACPI tables/VFCT; no driver loading, GPU writes or flashing."""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import re
import struct

ROOT = Path(__file__).resolve().parents[1]


def vbios_info(data):
    if data[:4] != b'VFCT' or len(data) < 76:
        raise ValueError('Invalid VFCT header')
    offset = int.from_bytes(data[52:56], 'little')
    lib1_offset = int.from_bytes(data[56:60], 'little')
    limit = lib1_offset if lib1_offset else len(data)
    if offset < 76 or limit > len(data) or limit < offset:
        raise ValueError('Invalid VFCT section offsets')
    records = []
    while offset + 28 <= limit:
        # This firmware appends an empty, zero-filled image header (28 bytes).
        # Accept padding only when the entire remaining section is zero.
        if not any(data[offset:limit]):
            break
        bus, device, function, vendor, gpu, subvendor, subdevice, revision, length = struct.unpack_from('<IIIHHHHII', data, offset)
        if length <= 0 or offset + 28 + length > limit:
            raise ValueError('Invalid VFCT image length')
        image = data[offset+28:offset+28+length]
        strings = [x.decode('ascii') for x in re.findall(rb'[ -~]{8,}', image)]
        selected = [x.strip() for x in strings if x.startswith(('113-', 'ASRock ', 'ATOMBIOSBK-AMD VER', 'ASROCK_'))]
        records.append({'vendor_id': f'{vendor:04X}', 'device_id': f'{gpu:04X}',
                        'bytes': length, 'sha256': hashlib.sha256(image).hexdigest(), 'identification_strings': selected,
                        'scope': 'firmware-provided ATOMBIOS image; not a complete PCI option ROM or flash backup'})
        offset += 28 + length
    return records


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT/'reports/local/acpi-boot')
    args = parser.parse_args()
    if os.name != 'nt':
        raise SystemExit('Windows firmware API required')
    output = args.output.resolve()
    if not output.is_relative_to((ROOT/'reports/local').resolve()):
        raise SystemExit('Firmware dumps must remain under reports/local/')
    if output.exists():
        raise SystemExit('Output exists; choose a new local path')
    output.mkdir(parents=True)
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    for name in ('EnumSystemFirmwareTables', 'GetSystemFirmwareTable'):
        getattr(kernel, name).restype = ctypes.c_uint32
    kernel.EnumSystemFirmwareTables.argtypes = [ctypes.c_uint32, ctypes.c_void_p, ctypes.c_uint32]
    kernel.GetSystemFirmwareTable.argtypes = [ctypes.c_uint32, ctypes.c_uint32, ctypes.c_void_p, ctypes.c_uint32]
    provider = int.from_bytes(b'ACPI', 'big')
    length = kernel.EnumSystemFirmwareTables(provider, None, 0)
    if not length:
        raise ctypes.WinError(ctypes.get_last_error())
    buffer = ctypes.create_string_buffer(length)
    if kernel.EnumSystemFirmwareTables(provider, buffer, length) != length:
        raise RuntimeError('ACPI enumeration size changed')
    ids = list(dict.fromkeys(int.from_bytes(buffer.raw[i:i+4], 'little') for i in range(0, length, 4)))
    dsdt = int.from_bytes(b'DSDT', 'little')
    if dsdt not in ids:
        ids.append(dsdt)
    records, vbios = [], []
    for table in ids:
        name = table.to_bytes(4, 'little').decode('ascii')
        if not re.fullmatch('[A-Z0-9_]{4}', name):
            raise ValueError('Invalid firmware table signature')
        length = kernel.GetSystemFirmwareTable(provider, table, None, 0)
        if not length:
            records.append({'table': name, 'error': ctypes.get_last_error()})
            continue
        buffer = ctypes.create_string_buffer(length)
        if kernel.GetSystemFirmwareTable(provider, table, buffer, length) != length:
            raise RuntimeError('Firmware table size changed')
        data = buffer.raw
        if data[:4] != table.to_bytes(4, 'little') or int.from_bytes(data[4:8], 'little') != length or sum(data) % 256:
            raise ValueError('ACPI length/signature/checksum rejected: '+name)
        (output/(name+'.aml')).write_bytes(data)
        records.append({'table': name, 'bytes': length, 'sha256': hashlib.sha256(data).hexdigest(), 'checksum_mod_256': 0})
        if name == 'VFCT':
            vbios = vbios_info(data)
    result = {'tables': records, 'vfct_images': vbios,
              'limitation': 'Windows GetSystemFirmwareTable returns only one table per signature; repeated SSDTs are not distinguishable here.'}
    (output/'inventory.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print('Read-only firmware inventory: '+str(output))
    print(json.dumps({'vfct_images': vbios, 'table_signatures': len(records)}, indent=2))


if __name__ == '__main__':
    main()
