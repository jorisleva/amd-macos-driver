"""Compare an ioreg -a snapshot with same-boot IOPCIDevice metadata.

No I/O, kext loading or hardware access. OSNumber values transported through
CFNumber/XML may be signed, even when the source value is an unsigned bitmask.
Normalize only within the field's declared width, never truncate addresses.
"""
import struct

IDENTITY = {'vendor-id': 0x1002, 'device-id': 0x7550,
            'subsystem-vendor-id': 0x1849, 'subsystem-id': 0x5417,
            'revision-id': 0xc0, 'class-code': 0x030000}
REGISTERS = {0x10 + i * 4: 'BAR' + str(i) for i in range(6)}
REGISTERS[0x30] = 'ROM'


def unsigned(value, bits):
    """Accept unsigned or signed XML integer of exactly the declared width."""
    if bits not in (32, 64) or type(value) is not int:
        raise ValueError('Expected a 32/64-bit integer, not a boolean/string/float')
    if not -(1 << (bits - 1)) <= value < (1 << bits):
        raise ValueError('Integer outside declared width')
    return value & ((1 << bits) - 1)


def expected_snapshot(properties_hex):
    if (type(properties_hex) is not dict or
            set(properties_hex) != set(IDENTITY) | {'assigned-addresses'} or
            any(type(v) is not str for v in properties_hex.values())):
        raise ValueError('Unexpected provider property set/types')
    expected = {'SchemaVersion': 1}
    for key, value in IDENTITY.items():
        raw = bytes.fromhex(properties_hex[key])
        if len(raw) != 4 or int.from_bytes(raw, 'little') != value:
            raise ValueError('Wrong provider identity: ' + key)
        expected[key] = value
    raw = bytes.fromhex(properties_hex['assigned-addresses'])
    if not raw or len(raw) % 20 or len(raw) > 140:
        raise ValueError('Invalid provider address-record length')
    bdf = None
    ranges = []
    for flags, hi, lo, size_hi, size_lo in struct.iter_unpack('<IIIII', raw):
        register = flags & 0xff
        space = (flags >> 24) & 3
        name = REGISTERS.get(register)
        bus = flags & 0x00ffff00
        base = (hi << 32) | lo
        length = (size_hi << 32) | size_lo
        if not name or name in expected or not space or flags & 0x1c000000:
            raise ValueError('Invalid/duplicate provider register or flags')
        if bdf is not None and bdf != bus:
            raise ValueError('Mixed PCI BDF')
        bdf = bus
        if not base or not length or base + length > (1 << 64) - 1:
            raise ValueError('Invalid provider range')
        memory = space >= 2
        if register in (0x10, 0x18, 0x24, 0x30) and not memory:
            raise ValueError('Required memory BAR/ROM is not memory')
        if any(m == memory and base < end and start < base + length for m, start, end in ranges):
            raise ValueError('Overlapping provider ranges')
        ranges.append((memory, base, base + length))
        expected[name] = {'ConfigRegister': register, 'RegistryFlags': flags,
                          'Base': base, 'Length': length}
    if not {'BAR0', 'BAR2', 'BAR5'} <= expected.keys():
        raise ValueError('Missing required memory BAR')
    expected.update(ResourceCount=len(ranges), PCIBDF=bdf)
    return expected


def compare_snapshot(snapshot, properties_hex):
    expected = expected_snapshot(properties_hex)
    if type(snapshot) is not dict or snapshot.keys() != expected.keys():
        raise ValueError('Snapshot schema/keys differ from provider-derived report')
    normalized = {}
    signed_fields = []
    for key, wanted in expected.items():
        actual = snapshot[key]
        if type(wanted) is dict:
            if type(actual) is not dict or actual.keys() != wanted.keys():
                raise ValueError('Invalid resource fields: ' + key)
            normalized[key] = {}
            for field, value in wanted.items():
                bits = 64 if field in ('Base', 'Length') else 32
                n = unsigned(actual[field], bits)
                if n != value:
                    raise ValueError('Snapshot differs from provider: ' + key + '.' + field)
                normalized[key][field] = n
                if actual[field] < 0:
                    signed_fields.append(key + '.' + field)
        else:
            normalized[key] = unsigned(actual, 32)
            if normalized[key] != wanted:
                raise ValueError('Snapshot differs from provider: ' + key)
    return {'matched': True, 'normalized_snapshot': normalized,
            'signed_xml_fields_normalized': sorted(signed_fields),
            'scope': 'IORegistry metadata equality only; no PCI/MMIO/VRAM/DMA access validation'}
