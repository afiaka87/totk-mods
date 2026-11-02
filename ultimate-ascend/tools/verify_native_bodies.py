"""Execute the game's body identity/lookup helpers against bounded synthetic worlds.

The methods on 1.0.0 come from the same hknpWorld vtable slots (+F8/+118),
anchored by ten unchanged neighboring methods. Other versions have whole-body
matches with only the body-index call relocated. No game bytes are stored here.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm64_const import UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2, UC_ARM64_REG_X19, UC_ARM64_REG_X29, UC_ARM64_REG_X30, UC_ARM64_REG_PC, UC_ARM64_REG_SP


# (isBodyStaticOrDynamic, getBody, index lookup, validity bytes, lookup bytes).
METHODS = {
    '1.0.0': (0x1E6538, 0x1E67B0, 0x2381264, 0xB0, 0x54),
    '1.1.0': (0x1D1248, 0x1D14D8, None, 0x90, 0x4C),
    '1.1.2': (0x1D1248, 0x1D14D8, None, 0x90, 0x4C),
    '1.2.0': (0x1D1248, 0x1D14D8, 0x8FE984, 0x90, 0x4C),
    '1.2.1': (0x1D1248, 0x1D14D8, 0x2403D94, 0x90, 0x4C),
    '1.4.0': (0x125FD58, 0x125FFE8, 0x1188190, 0x90, 0x4C),
    '1.4.1': (0x1255D88, 0x1256018, None, 0x90, 0x4C),
    '1.4.2': (0x1252608, 0x1252898, None, 0x90, 0x4C),
    '1.4.3': (0x124DF48, 0x124E1D8, None, 0x90, 0x4C),
}


def verify(root: Path) -> dict:
    sys.path.insert(0, str(root / 'scripts'))
    from totk_version_port.arm64 import branch_target, is_bl
    from totk_version_port.images import MainImage
    from totk_version_port.scan import pointers_to
    reference = MainImage('1.2.1')
    result = {}
    for version, (valid, get, expected_index, valid_size, get_size) in METHODS.items():
        image = MainImage(version)
        index = branch_target(image.word(valid + 0x1C), valid + 0x1C)
        assert is_bl(image.word(valid + 0x1C))
        assert is_bl(image.word(get + 0x1C))
        assert branch_target(image.word(get + 0x1C), get + 0x1C) == index
        assert expected_index is None or index == expected_index
        # 1.0.0 optimizes the same lookup differently. Execute it below as well; the later versions additionally retain exact whole-body identity.
        index_size = 0x170 if version == '1.0.0' else 0x230
        if version != '1.0.0':
            assert image.data[index:index + index_size] == reference.data[0x2403D94:0x2403D94 + index_size]
        assert any(image.pointer(slot + 0x20) == get for slot in pointers_to(image, valid))
        uc = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
        pages = {pc & ~0xFFF for pc in (valid, valid + valid_size, get, get + get_size, index, index + index_size)}
        for page in sorted(pages):
            uc.mem_map(page, 0x1000)
            uc.mem_write(page, image.data[page:page + 0x1000])
        world, records, stack, stop = 0x10000000, 0x10002000, 0x10008000, 0x1000F000
        uc.mem_map(world, 0x10000)
        uc.mem_write(world + 0x38, struct.pack('<QI', records, 8))
        bucket = world + 0x4000
        uc.mem_write(world + 0x980, struct.pack('<QQI', bucket, 0, 0))
        count = 0
        # Noncanonical 64-bit IDs require native lookup; bit63 IDs carry a record ID.
        cases = [(True, 0x12000003, True, 3, 0x12000003, True),
                 (True, 0x12000003, True, 0, 0x12000003, False),
                 (True, 0x12000003, True, 3, 0x13000003, False),
                 (True, 8, True, 3, 8, False),
                 (True, 0xFFFFFF, True, 3, 0xFFFFFF, False),
                 (False, 0x12000003, True, 3, 0x12000003, True),
                 (False, 0x12000003, False, 3, 0x12000003, False),
                 (False, 0x12000003, True, 1, 0x12000003, True),
                 (False, 0x12000003, True, 2, 0x12000003, True),
                 (False, 0x12000003, True, 4, 0x12000003, False)]
        for direct, resolved, found, flags, stored, accepted in cases:
            body_id = (1 << 63) | resolved if direct else 0x123456789
            uc.mem_write(records + 3 * 0xC0 + 0x50, struct.pack('<IB', stored, flags))
            tag = ((body_id * 0xFF51AFD7ED558CCD) & ((1 << 64) - 1)) >> 56
            # One live key in slot1, no overflow chain. A wrong key with the same tag exercises the native full-key comparison, not a shortcut miss.
            uc.mem_write(bucket, struct.pack('<QQQQ', tag << 8, 0x100,
                                             body_id if found else body_id + 1, resolved))
            index_calls = []

            def intercept(machine, pc, size, user):
                if pc == index:
                    assert machine.reg_read(UC_ARM64_REG_X0) == world + 0x980
                    assert machine.reg_read(UC_ARM64_REG_X1) == body_id
                    index_calls.append(pc)

            handle = uc.hook_add(UC_HOOK_CODE, intercept, begin=index, end=index)

            def run(entry):
                uc.reg_write(UC_ARM64_REG_X0, world)
                uc.reg_write(UC_ARM64_REG_X1, body_id)
                uc.reg_write(UC_ARM64_REG_X19, 0xCAFE)
                uc.reg_write(UC_ARM64_REG_X29, 0xBEEF)
                uc.reg_write(UC_ARM64_REG_X30, stop)
                uc.reg_write(UC_ARM64_REG_SP, stack)
                uc.emu_start(entry, stop, count=300)
                assert uc.reg_read(UC_ARM64_REG_PC) == stop
                assert uc.reg_read(UC_ARM64_REG_SP) == stack
                assert uc.reg_read(UC_ARM64_REG_X19) == 0xCAFE
                assert uc.reg_read(UC_ARM64_REG_X29) == 0xBEEF
                return uc.reg_read(UC_ARM64_REG_X0)

            assert bool(run(valid)) == accepted, (version, cases[count])
            assert len(index_calls) == (0 if direct else 1)
            if accepted:
                assert run(get) == records + (resolved & 0xFFFFFF) * 0xC0
                assert len(index_calls) == (0 if direct else 2)
            uc.hook_del(handle)
            count += 1
        result[version] = {'valid': hex(valid), 'get': hex(get), 'index': hex(index),
                           'cases': count, 'image_digest': image.data_digest,
                           'index_sha256': hashlib.sha256(image.data[index:index + index_size]).hexdigest(),
                           'valid_sha256': hashlib.sha256(image.data[valid:valid + valid_size]).hexdigest(),
                           'get_sha256': hashlib.sha256(image.data[get:get + get_size]).hexdigest()}
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    report = verify(args.root)
    args.out.write_text(json.dumps(report, indent=2) + '\n')
    print(f'PASS {sum(row["cases"] for row in report.values())} native body identity cases across {len(report)} builds')
