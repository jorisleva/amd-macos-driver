#!/usr/bin/env python3
"""Set an existing OpenCore installer's language, preserving all other settings."""
import argparse
import datetime
import os
from pathlib import Path
import plistlib
import re
import tempfile

GUID = '7C436110-AB2A-4BBB-A880-FE41995C9F82'
VARIABLE = 'prev-lang:kbd'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', type=Path, required=True, help='Existing EFI/OC/config.plist')
    parser.add_argument('--language', default='fr-FR', help='Locale, default: fr-FR')
    parser.add_argument('--keyboard', type=int, default=1, help='Apple keyboard layout ID; 1 = French')
    args = parser.parse_args()
    if not re.fullmatch(r'[a-z]{2,3}(?:-[A-Za-z]{2,8})?', args.language):
        parser.error('Invalid locale')
    if not -32768 <= args.keyboard <= 32767:
        parser.error('Keyboard ID outside the signed 16-bit range')
    path = args.config.resolve()
    original = path.read_bytes()
    config = plistlib.loads(original)
    nvram = config['NVRAM']
    additions = nvram.setdefault('Add', {}).setdefault(GUID, {})
    deletions = nvram.setdefault('Delete', {}).setdefault(GUID, [])
    desired = f'{args.language}:{args.keyboard}'.encode('ascii')
    if additions.get(VARIABLE) == desired and VARIABLE in deletions:
        print('Installer language already configured: ' + desired.decode('ascii'))
        return
    additions[VARIABLE] = desired
    if VARIABLE not in deletions:
        deletions.append(VARIABLE)
    fmt = plistlib.FMT_BINARY if original.startswith(b'bplist') else plistlib.FMT_XML
    updated = plistlib.dumps(config, fmt=fmt, sort_keys=False)
    timestamp = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%d-%H%M%S-%f')
    backup = path.with_name(path.stem + '.before-language-' + timestamp + path.suffix)
    with backup.open('xb') as stream:
        stream.write(original)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode='wb', dir=path.parent, prefix='language-', suffix='.tmp', delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(updated)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
        temporary = None
        if path.read_bytes() != updated:
            raise OSError('Configuration read-back mismatch; backup retained at ' + str(backup))
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
    print('Installer language configured: ' + desired.decode('ascii'))
    print('Previous configuration saved at: ' + str(backup))


if __name__ == '__main__':
    main()
