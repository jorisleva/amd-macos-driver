"""Read-only checks of firmware bytes in an x86_64 Mach-O kext (Python 3.9+)."""
import hashlib
from pathlib import Path
import struct
import subprocess

OPTIONAL_BLOBS = ('shadercache', 'shader_blit_copy_gfx1201', 'shader_blit_diag_gfx1201',
                  'shader_blit_diagmin_gfx1201', 'shader_blit_copy_offen_gfx1201')


def file_segments(data):
    """Return (vmaddr, fileoff, filesize) for a little-endian x86_64 kext."""
    if len(data) < 32:
        raise ValueError('Truncated Mach-O header')
    magic, cpu, _, filetype, ncmds, sizeofcmds, _, _ = struct.unpack_from('<8I', data)
    if (magic, cpu, filetype) != (0xfeedfacf, 0x01000007, 11):
        raise ValueError('Expected a thin x86_64 MH_KEXT_BUNDLE')
    end = 32 + sizeofcmds
    if end > len(data):
        raise ValueError('Truncated load commands')
    segments = []
    offset = 32
    for _ in range(ncmds):
        if offset + 8 > end:
            raise ValueError('Truncated load command')
        command, size = struct.unpack_from('<II', data, offset)
        if size < 8 or size % 8 or offset + size > end:
            raise ValueError('Invalid load command size')
        if command == 0x19:  # LC_SEGMENT_64
            if size < 72:
                raise ValueError('Truncated segment')
            vmaddr, vmsize, fileoff, filesize = struct.unpack_from('<4Q', data, offset + 24)
            if fileoff + filesize > len(data) or filesize > vmsize:
                raise ValueError('Segment outside file/VM range')
            segments.append((vmaddr, fileoff, filesize))
        offset += size
    if offset != end:
        raise ValueError('Inconsistent load command count')
    return segments


def at_address(data, segments, address, size):
    for base, offset, length in segments:
        if base <= address and address - base <= length and size <= length - (address - base):
            start = offset + address - base
            return data[start:start + size]
    raise ValueError('Symbol range is not backed by file bytes')


def verify_firmwares(executable, firmware_directory, expected):
    data = Path(executable).read_bytes()
    segments = file_segments(data)
    listing = subprocess.check_output(['nm', '-n', str(executable)], text=True)
    symbols = {}
    for line in listing.splitlines():
        fields = line.split()
        if len(fields) == 3 and fields[2].startswith('_fw_'):
            symbols[fields[2]] = int(fields[0], 16)

    def symbol_bytes(name, length):
        if name not in symbols:
            raise ValueError('Missing firmware symbol: ' + name)
        return at_address(data, segments, symbols[name], length)

    result = {}
    for filename, wanted_hash in expected.items():
        source = (Path(firmware_directory) / filename).read_bytes()
        symbol = '_fw_' + Path(filename).stem
        size = struct.unpack('<Q', symbol_bytes(symbol + '_size', 8))[0]
        if size != len(source):
            raise ValueError('Embedded firmware size mismatch: ' + filename)
        blob = symbol_bytes(symbol, size)
        if blob != source or hashlib.sha256(blob).hexdigest() != wanted_hash:
            raise ValueError('Embedded firmware bytes mismatch: ' + filename)
        result[filename] = {'bytes': size, 'sha256': wanted_hash}
    for name in OPTIONAL_BLOBS:
        if struct.unpack('<Q', symbol_bytes('_fw_' + name + '_size', 8))[0] != 0:
            raise ValueError('Unexpected private Apple shader/cache blob: ' + name)
    return {'firmwares': result, 'optional_blob_sizes': {name: 0 for name in OPTIONAL_BLOBS}}
