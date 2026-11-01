"""Compose the tested free Ascend/Descend pair and chain the shared approach hook.

Local compatibility build only. Requires the exact public free patch, accepted
Descend patch06, and its retained build layout. Does not modify either input.
"""
import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path


def compose(community, descend, layout, out, root):
    sys.path.insert(0, str(root / 'scripts'))
    from patch_gaps.formats import parse_pchtxt, parse_ips
    from patch_gaps.place import write_ips32, write_pchtxt
    expected = (
        '562abf3b538b1b4d189ad965bb389ab0b5b4845a27100508c0fb81a8f91439d9',
        'ad97e15e1f9332322a726ebb601d40c81dd32e31640b3a92c82c170292e12c75',
    )
    maps = []
    for path, digest in zip((community, descend), expected):
        if hashlib.sha256(path.read_bytes()).hexdigest() != digest:
            raise ValueError(f'Unsupported input: {path.name}')
        _, writes = parse_pchtxt(path.read_text())
        maps.append({w.off+i: v for w in writes if w.enabled for i, v in enumerate(w.data)})
    shared = 0x1c644d0
    if sorted(maps[0].keys() & maps[1].keys()) != list(range(shared, shared+4)):
        raise ValueError('Unexpected overlap')
    report = json.loads((layout / 'report.json').read_text())
    placement = (layout / 'placement.words').read_bytes()
    if hashlib.sha256(placement).hexdigest() != report['placement_sha256']:
        raise ValueError('Layout receipt mismatch')
    # Retained release layout changed only the patch06 label and its data checksum.
    label_changes = {0xbf8: (bytes.fromhex('b5b2906e'), bytes.fromhex('1797e8bf')),
                     0x137158: (b'.14.', b'.15.'), 0x13715c: (b'0 pa', b'0 re'),
                     0x1371a8: (b'tch0', b'leas'), 0x1371ac: (b'6 in', b'e in')}
    for address, word in struct.iter_unpack('<II', placement):
        pair = (bytes(maps[1].get(address+i, 0) for i in range(4)), struct.pack('<I', word))
        if pair[0] != pair[1] and label_changes.get(address) != pair:
            raise ValueError('Layout does not reproduce accepted Descend')
    original = report['adapters']['original_RiseUpdateHook']
    community_word = struct.unpack('<I', bytes(maps[0][shared+i] for i in range(4)))[0]
    if community_word != 0x143ad5f4:
        raise ValueError('Unexpected community entry branch')
    target = shared + (community_word & 0x3ffffff)*4
    delta = target-original
    if delta % 4 or not -(1 << 27) <= delta < (1 << 27):
        raise ValueError('Compatibility branch out of range')
    # Only the native fallback is redirected; owned downward travel returns before Orig.
    memory = maps[0] | maps[1]
    before = bytes(memory[original+i] for i in range(4))
    if struct.unpack('<I', before)[0] != 0xa9be7bfd:
        raise ValueError('Unexpected displaced prologue')
    replacement = struct.pack('<I', 0x14000000 | ((delta//4) & 0x3ffffff))
    for i, byte in enumerate(replacement):
        memory[original+i] = byte
    words = [(a, struct.unpack('<I', bytes(memory[a+i] for i in range(4)))[0])
             for a in sorted(memory) if not a % 4]
    out.mkdir(parents=True, exist_ok=True)
    bid = '9B4E43650501A4D4489B4BBFDB740F26AF3CF850'
    ips, patch = out / (bid+'.ips'), out / 'descend-compat-free.pchtxt'
    write_ips32(ips, words)
    write_pchtxt(patch, bid, words, image_offsets=True)
    for writes in (parse_ips(ips.read_bytes()), parse_pchtxt(patch.read_text())[1]):
        assert {w.off+i: v for w in writes if w.enabled for i,v in enumerate(w.data)} == memory
    result = dict(inputs=list(expected), shared_entry=shared, redirect=original,
                  community_target=target, previous_word=before.hex(), new_word=replacement.hex(),
                  payload_sha256={p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in (ips,patch)})
    (out/'composition.json').write_text(json.dumps(result, indent=2)+'\n')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('community', 'descend', 'layout', 'out', 'root'):
        parser.add_argument('--'+name, type=Path, required=True)
    print(json.dumps(compose(**vars(parser.parse_args())), indent=2))
