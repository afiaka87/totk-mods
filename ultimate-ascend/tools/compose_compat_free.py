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


def compose_port(community, descend, layout, out, report, research, candidate):
    """Activate placed community code after the exact full-build receipt passes."""
    from patch_gaps.formats import parse_pchtxt, parse_ips
    from patch_gaps.place import write_ips32, write_pchtxt
    from patch_gaps.image import Image, FULL_BUILD_ID
    from totk_version_port.arm64 import branch_target
    from community_port import SOURCE_SHA256

    port = report['community_port']
    if report['research_only'] and not research:
        raise ValueError('Research ports require explicit --research; they cannot be packaged')
    if not research:
        if not candidate:
            raise ValueError('A port requires --candidate and full validation')
        validated = json.loads((layout/'validated.json').read_text())
        required = {'compile','inventory','storage','layout','arm64','effects','visuals','routes','rejections'}
        if (not required <= set(validated['steps']) or validated['research_only']
                or validated['payload_sha256']['ultimate-ascend.pchtxt'] != report['pchtxt_sha256']):
            raise ValueError('Port validation does not match the complete candidate')
    if hashlib.sha256(community.read_bytes()).hexdigest() != port['source_sha256'] or port['source_sha256'] != SOURCE_SHA256:
        raise ValueError('Different compatibility source')
    if hashlib.sha256(descend.read_bytes()).hexdigest() != report['pchtxt_sha256']:
        raise ValueError('Different placed patch')
    placement = (layout / 'placement.words').read_bytes()
    if hashlib.sha256(placement).hexdigest() != report['placement_sha256']:
        raise ValueError('Different placement receipt')
    if hashlib.sha256((layout / 'native/descend-patch.o').read_bytes()).hexdigest() != report['input_sha256']:
        raise ValueError('Different compiled object')
    _, writes = parse_pchtxt(descend.read_text())
    memory = {w.off+i: v for w in writes if w.enabled for i,v in enumerate(w.data)}
    placed = {a+i: b for a,w in struct.iter_unpack('<II',placement)
              for i,b in enumerate(struct.pack('<I',w))}
    if placed != memory:
        raise ValueError('Patch and placement disagree')
    clean = Image.load(report['build_id'])
    shared = report['native_profile']['bindings']['kPreUpdate']
    original = report['adapters']['original_RiseUpdateHook']
    hooks = port['hooks']
    if len(hooks) != 20 or len({h['address'] for h in hooks}) != 20:
        raise ValueError('Incomplete community hook set')
    gates = {g['address']:g for g in report.get('shrine_gates',[])}
    if gates and set(gates) != {h['address'] for h in hooks}:
        raise ValueError('Incomplete shrine gates')
    targets = {int(k):v for k,v in port['relocation_targets'].items()}
    installed = []
    def branch(at, target, link=False):
        delta = target - at
        if delta & 3 or not -(1 << 27) <= delta < (1 << 27):
            raise ValueError('Community branch out of range')
        return (0x94000000 if link else 0x14000000) | ((delta // 4) & 0x3ffffff)
    for hook in hooks:
        at, word = hook['address'], hook['patched']
        if clean.word(at) != hook['original']:
            raise ValueError('Community native instruction differs')
        if at != shared and any(at+i in memory for i in range(4)):
            raise ValueError('Unexpected community overlap')
        if at in gates:
            gate = gates[at]
            if (gate['patched'],gate['original']) != (word,hook['original']):
                raise ValueError('Shrine gate differs from community hook')
            word = branch(at, gate['gate'])
        elif (target := branch_target(word,at)) is not None:
            word = branch(at, targets[target], bool(word & 0x80000000))
        if at == shared:
            target = branch_target(word,at)
            if target is None or struct.unpack('<I',bytes(memory[original+i] for i in range(4)))[0] != hook['original']:
                raise ValueError('Shared approach replay differs')
            word, at = branch(original,target), original
        for i,b in enumerate(struct.pack('<I',word)):
            memory[at+i] = b
        installed.append({'native':hook['address'],'write':at,'word':word})
    out.mkdir(parents=True,exist_ok=True)
    bid = FULL_BUILD_ID[report['build_id']]
    ips,patch = out/(bid+'.ips'),out/'ultimate-ascend.pchtxt'
    words = [(a,struct.unpack('<I',bytes(memory[a+i] for i in range(4)))[0]) for a in sorted(memory) if a % 4 == 0]
    write_ips32(ips,words)
    write_pchtxt(patch,bid,words,image_offsets=True)
    for decoded in (parse_ips(ips.read_bytes()),parse_pchtxt(patch.read_text())[1]):
        if {w.off+i:b for w in decoded if w.enabled for i,b in enumerate(w.data)} != memory:
            raise ValueError('Composed format roundtrip differs')
    result = {'research_only':bool(research or report['research_only']),'game_version':port['version'],'source_sha256':SOURCE_SHA256,
              'shared_entry':shared,'redirect':original,'hooks':installed,'shrine_gates':len(gates),
              'payload_sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in (ips,patch)}}
    (out/'composition.json').write_text(json.dumps(result,indent=2)+'\n')
    return result


def compose(community, descend, layout, out, root, candidate=False, research=False):
    sys.path.insert(0, str(root / 'scripts'))
    from patch_gaps.formats import parse_pchtxt, parse_ips
    from patch_gaps.place import write_ips32, write_pchtxt
    report = json.loads((layout / 'report.json').read_text())
    if report.get('community_port'):
        return compose_port(community,descend,layout,out,report,research,candidate)
    if report.get('research_only'):
        raise ValueError('Incomplete research profiles cannot enter compatibility packaging')
    descend_hash = 'ad97e15e1f9332322a726ebb601d40c81dd32e31640b3a92c82c170292e12c75'
    if candidate:
        validated = json.loads((layout / 'validated.json').read_text())
        required = {'compile', 'inventory', 'storage', 'layout', 'arm64', 'effects', 'visuals', 'routes', 'rejections'}
        if not required <= set(validated['steps']):
            raise ValueError('Candidate is missing required offline validation')
        descend_hash = validated['payload_sha256']['ultimate-ascend.pchtxt']
        if descend_hash != report['pchtxt_sha256'] or hashlib.sha256(
                (layout / 'native/descend-patch.o').read_bytes()).hexdigest() != report['input_sha256']:
            raise ValueError('Candidate validation and layout disagree')
    expected = (
        '562abf3b538b1b4d189ad965bb389ab0b5b4845a27100508c0fb81a8f91439d9',
        descend_hash,
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
    placement = (layout / 'placement.words').read_bytes()
    if hashlib.sha256(placement).hexdigest() != report['placement_sha256']:
        raise ValueError('Layout receipt mismatch')
    # Retained release layout changed only the patch06 label and its data checksum.
    label_changes = {0xbf8: (bytes.fromhex('b5b2906e'), bytes.fromhex('1797e8bf')),
                     0x137158: (b'.14.', b'.15.'), 0x13715c: (b'0 pa', b'0 re'),
                     0x1371a8: (b'tch0', b'leas'), 0x1371ac: (b'6 in', b'e in')}
    if candidate:
        label_changes = {}
        placed_bytes = {address+i: byte for address, word in struct.iter_unpack('<II',placement)
                        for i,byte in enumerate(struct.pack('<I',word))}
        if placed_bytes != maps[1]:
            raise ValueError('Candidate patch differs from verified placement')
    for address, word in struct.iter_unpack('<II', placement):
        pair = (bytes(maps[1].get(address+i, 0) for i in range(4)), struct.pack('<I', word))
        if pair[0] != pair[1] and label_changes.get(address) != pair:
            raise ValueError('Layout does not reproduce accepted Descend')
    original = report['adapters']['original_RiseUpdateHook']
    community_word = struct.unpack('<I', bytes(maps[0][shared+i] for i in range(4)))[0]
    if community_word != 0x143ad5f4:
        raise ValueError('Unexpected community entry branch')
    target = shared + (community_word & 0x3ffffff)*4
    gates = report.get('shrine_gates', [])
    if gates and report.get('community_sha256') != expected[0]:
        raise ValueError('Shrine gates were built for a different compatibility input')
    shared_gate = next((g['gate'] for g in gates if g['address'] == shared), target)
    delta = shared_gate-original
    if delta % 4 or not -(1 << 27) <= delta < (1 << 27):
        raise ValueError('Compatibility branch out of range')
    # Only the native fallback is redirected; owned downward travel returns before Orig.
    memory = maps[0] | maps[1]
    for gate in gates:
        site, destination = gate['address'], gate['gate']
        if site == shared:
            continue
        if struct.unpack('<I', bytes(maps[0][site+i] for i in range(4)))[0] != gate['patched']:
            raise ValueError('Shrine gate does not match the community instruction')
        displacement = destination - site
        if displacement % 4 or not -(1 << 27) <= displacement < (1 << 27):
            raise ValueError('Shrine gate branch out of range')
        branch = struct.pack('<I', 0x14000000 | ((displacement // 4) & 0x3ffffff))
        for i, byte in enumerate(branch):
            memory[site+i] = byte
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
    ips, patch = out / (bid+'.ips'), out / 'ultimate-ascend.pchtxt'
    write_ips32(ips, words)
    write_pchtxt(patch, bid, words, image_offsets=True)
    for writes in (parse_ips(ips.read_bytes()), parse_pchtxt(patch.read_text())[1]):
        assert {w.off+i: v for w in writes if w.enabled for i,v in enumerate(w.data)} == memory
    result = dict(inputs=list(expected), candidate=candidate, shared_entry=shared, redirect=original,
                  community_target=target, shrine_gates=len(gates), previous_word=before.hex(), new_word=replacement.hex(),
                  payload_sha256={p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in (ips,patch)})
    (out/'composition.json').write_text(json.dumps(result, indent=2)+'\n')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('community', 'descend', 'layout', 'out', 'root'):
        parser.add_argument('--'+name, type=Path, required=True)
    parser.add_argument('--candidate', action='store_true', help='Require exact fresh build/validation receipts')
    parser.add_argument('--research', action='store_true', help='Offline port composition only; cannot be packaged')
    print(json.dumps(compose(**vars(parser.parse_args())), indent=2))
