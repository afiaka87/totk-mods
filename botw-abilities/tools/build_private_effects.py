# SPDX-License-Identifier: MIT
# Copyright (c) Clay Mullis
"""Split private effects from the composed ELink; never ship the shared-file override."""
import argparse
import hashlib
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
sys.path[:0] = [str(ROOT / 'scripts'), str(ROOT / 'mods/code/totk-remote-bomb/tools')]
from import_audio import transcode, user_blocks, XLINK_SHA256
from resource_merge import private_elink_text
from totk_mod_tools.zstd_helper import TotkZstd

ELINK = 'ELink2/elink2.Product.110.belnk.zs'
PRIVATE = 'BotwAbilities/Effects/Private.belnk'


def fingerprint(data):
    value = 2166136261
    for byte in data:
        value = ((value ^ byte) * 16777619) & 0xffffffff
    return value


def write_changed(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.is_file() or path.read_bytes() != data:
        path.write_bytes(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--romfs', required=True, type=Path)
    parser.add_argument('--build', required=True, type=Path)
    parser.add_argument('--xlink', required=True, type=Path)
    args = parser.parse_args()
    if hashlib.sha256(args.xlink.read_bytes()).hexdigest() != XLINK_SHA256:
        raise ValueError('unreviewed XLink converter')
    z = TotkZstd(args.romfs)
    work = args.build / 'private-effects'
    work.mkdir(parents=True, exist_ok=True)
    names = work / 'names.txt'
    sources = [args.build / 'champions/effect-users.txt', args.build / 'native-sphere/users.txt']
    names.write_text('\n'.join(p.read_text(encoding='utf-8') for p in sources if p.is_file()), encoding='utf-8')
    shared = args.build / 'romfs' / ELINK
    for name, data in [('clean', z.decompress(args.romfs / ELINK)), ('combined', z.decompress(shared))]:
        binary, text = work / f'{name}.belnk', work / f'{name}.txt'
        binary.write_bytes(data)
        transcode(args.xlink, binary, text, 'EXKing', names)
    clean = (work / 'clean.txt').read_text(encoding='utf-8')
    combined = (work / 'combined.txt').read_text(encoding='utf-8')
    private = private_elink_text(clean, combined, user_blocks)
    source, binary, restored = (work / name for name in ('private.txt', 'private.belnk', 'restored.txt'))
    source.write_text(private, encoding='utf-8')
    transcode(args.xlink, source, binary, 'EXKing', names)
    transcode(args.xlink, binary, restored, 'EXKing', names)
    actual = restored.read_text(encoding='utf-8')
    if user_blocks(actual) != user_blocks(private) or actual.split('Users {', 1)[0] != private.split('Users {', 1)[0]:
        raise ValueError('private ELink round-trip changed effect definitions')
    data = binary.read_bytes()
    write_changed(args.build / 'romfs' / PRIVATE, data)
    info = ('// SPDX-License-Identifier: MIT\n// Copyright (c) Clay Mullis\n#pragma once\n'
            '#include <cstddef>\n#include <cstdint>\nnamespace botw_abilities::effects {\n'
            f'inline constexpr std::size_t kPrivateEffectBytes = {len(data)};\n'
            f'inline constexpr std::uint32_t kPrivateEffectChecksum = 0x{fingerprint(data):08x};\n'
            '}\n')
    write_changed(work / 'generated/PrivateEffectInfo.hpp', info.encode())
    receipt = {'bytes': len(data), 'users': sorted(user_blocks(private)),
               'sha256': hashlib.sha256(data).hexdigest(), 'stock_users_unchanged': True}
    (work / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    shared.unlink()
    print(f'[botw-abilities] private ELink: {len(receipt["users"])} users, {len(data)} bytes; shared override removed')


if __name__ == '__main__':
    main()
