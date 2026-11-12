# SPDX-License-Identifier: MIT
# Copyright (c) HanSoloBlaster, afiaka87
"""Append the wolf's two effect users to the accepted private bank as a reviewed draft."""
import argparse
import hashlib
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
sys.path[:0] = [str(ROOT / 'scripts'), str(ROOT / 'mods/code/totk-remote-bomb/tools'),
                str(ROOT / 'mods/code/totk-cryonis/tools')]
from import_audio import XLINK_SHA256, parser_budget, transcode, user_blocks
from build_pillar import cloned_resource_floor
from totk_mod_tools.zstd_helper import TotkZstd
sys.path.insert(0, str(Path(__file__).resolve().parent))
from build_champion_effects import DONOR, TRIGGER_DONOR, clone_carrier, effect_specs, effect_user, tint_for
from build_private_effects import fingerprint

PRIVATE = 'romfs/BotwAbilities/Effects/Private.belnk'
ELINK = 'ELink2/elink2.Product.110.belnk.zs'
WOLF_KEYS = ('WolfArrive', 'WolfLeave')


def digest(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--romfs', required=True, type=Path, help='clean TotK 1.2.1 RomFS')
    parser.add_argument('--baseline-repo', required=True, type=Path)
    parser.add_argument('--baseline-receipt', required=True, type=Path)
    parser.add_argument('--payload', required=True, type=Path, help='verified v0.2.11 payload directory')
    parser.add_argument('--xlink', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    if digest(args.xlink.read_bytes()) != XLINK_SHA256:
        raise ValueError('unreviewed XLink converter')
    receipt_rows = {row['path']: row['sha256'] for row in
                    json.loads(args.baseline_receipt.read_text(encoding='utf-8'))['artifact']['files']}
    baseline_private = (args.payload / PRIVATE).read_bytes()
    if digest(baseline_private) != receipt_rows[PRIVATE]:
        raise ValueError('baseline private bank no longer matches the accepted receipt')

    specs = effect_specs()
    wolf = {key: f'EftLoc_Botw{key}' for key in WOLF_KEYS}
    if [key for key in specs if key.startswith('Wolf')] != list(WOLF_KEYS):
        raise ValueError('effect catalogue and this draft disagree about the wolf entries')
    static_bank = TotkZstd(args.romfs).decompress(args.romfs / 'Effect/static.Nin_NX_NVN.esetb.byml.zs')
    for key in WOLF_KEYS:
        if specs[key][0].encode() + b'\0' not in static_bank:
            raise ValueError(f'{specs[key][0]} absent from the resident effect bank')

    work = args.output / 'work'
    work.mkdir(parents=True, exist_ok=True)
    names = work / 'names.txt'
    listed = []
    for source in ('build/champions/effect-users.txt', 'build/native-sphere/users.txt'):
        listed += (args.baseline_repo / 'mods/code/botw-abilities' / source).read_text(encoding='utf-8').split()
    names.write_text('\n'.join(dict.fromkeys(listed + list(wolf.values()))) + '\n', encoding='utf-8')

    z = TotkZstd(args.romfs)
    stock_binary, stock_text = work / 'stock.belnk', work / 'stock.txt'
    stock_binary.write_bytes(z.decompress(args.romfs / ELINK))
    transcode(args.xlink, stock_binary, stock_text, 'EXKing', names)
    template = user_blocks(stock_text.read_text(encoding='utf-8'))[TRIGGER_DONOR]

    private_binary, private_text = work / 'baseline.belnk', work / 'baseline.txt'
    private_binary.write_bytes(baseline_private)
    transcode(args.xlink, private_binary, private_text, 'EXKing', names)
    text = private_text.read_text(encoding='utf-8')
    users = user_blocks(text)
    if users.keys() & set(wolf.values()):
        raise ValueError('wolf effect user already exists in the accepted bank')
    added = {wolf[key]: effect_user(template, wolf[key], specs[key][0], tint_for(key, 'Wolf'))
             for key in WOLF_KEYS}
    candidate = work / 'candidate.txt'
    candidate.write_text(text.rstrip()[:-1] + '\n'.join(added.values()) + '\n}\n', encoding='utf-8')
    merged_binary, restored = work / 'candidate.belnk', work / 'restored.txt'
    transcode(args.xlink, candidate, merged_binary, 'EXKing', names)
    transcode(args.xlink, merged_binary, restored, 'EXKing', names)
    after = restored.read_text(encoding='utf-8')
    if (user_blocks(after) != users | added
            or after.split('Users {', 1)[0] != text.split('Users {', 1)[0]):
        raise ValueError('bank conversion changed the accepted users or the wolf calls')
    merged = merged_binary.read_bytes()
    if len(merged) >= 64 * 1024:
        raise ValueError('private bank outgrew its reserved 64 KiB pool')

    import oead
    resource_table = z.decompress(args.romfs / 'System/Resource/ResourceSizeTable.Product.121.rsizetable.zs')
    donor = {f.name: bytes(f.data) for f in oead.Sarc(z.decompress(
        args.romfs / f'Pack/Actor/{DONOR}.pack.zs')).get_files()}
    floors, files = {'BotwAbilities/Effects/Private.belnk': len(merged) + 4096}, {}

    def emit(path, data):
        target = args.output / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        files[path] = digest(data)

    emit(PRIVATE, merged)
    for key, actor in wolf.items():
        raw = clone_carrier(donor, actor)
        pack_path = f'Pack/Actor/{actor}.pack.zs'
        emit('romfs/' + pack_path, z.compress(raw, pack_path))
        if z.decompress(args.output / 'romfs' / pack_path) != raw:
            raise ValueError(f'{pack_path}: compression round-trip failed')
        floors[f'Pack/Actor/{actor}.pack'] = (max(parser_budget(resource_table, f'Pack/Actor/{DONOR}.pack'),
                                                  len(raw)) + max(65536, len(raw) * 10))
        for member in oead.Sarc(raw).get_files():
            source = member.name.replace(actor, DONOR)
            floors[member.name] = cloned_resource_floor(parser_budget(resource_table, source),
                                                        len(donor[source]), len(bytes(member.data)))

    generated = args.output / 'generated'
    generated.mkdir(exist_ok=True)
    actors = {key: f'EftLoc_Botw{key}' for key in specs}
    header = ('// SPDX-License-Identifier: MIT\n// Copyright (c) HanSoloBlaster, afiaka87\n#pragma once\n'
              '#include <array>\n#include <cstdint>\nnamespace botw_abilities::champions {\n'
              'enum class Effect : std::uint8_t {\n    ' + ', '.join(specs) + ', Count\n};\n'
              f'inline constexpr std::array<const char*, {len(specs)}> kEffectActors{{{{\n'
              + ''.join(f'    "{actors[key]}",\n' for key in specs) + '}};\n}\n')
    baseline_header = (args.baseline_repo /
                       'mods/code/botw-abilities/build/champions/generated/ChampionEffects.hpp')
    old_names = baseline_header.read_text(encoding='utf-8').split('Effect : std::uint8_t {\n    ')[1].split(', Count')[0]
    if ', '.join(specs) != old_names + ', ' + ', '.join(WOLF_KEYS):
        raise ValueError('the wolf entries must extend the accepted effect order, never reorder it')
    (generated / 'ChampionEffects.hpp').write_text(header, encoding='utf-8')
    info = ('// SPDX-License-Identifier: MIT\n// Copyright (c) HanSoloBlaster, afiaka87\n#pragma once\n'
            '#include <cstddef>\n#include <cstdint>\nnamespace botw_abilities::effects {\n'
            f'inline constexpr std::size_t kPrivateEffectBytes = {len(merged)};\n'
            f'inline constexpr std::uint32_t kPrivateEffectChecksum = 0x{fingerprint(merged):08x};\n'
            '}\n')
    (generated / 'PrivateEffectInfo.hpp').write_text(info, encoding='utf-8')
    draft_receipt = {
        'xlink_sha256': XLINK_SHA256,
        'baseline_private_sha256': digest(baseline_private),
        'private_bytes': len(merged), 'private_checksum': f'0x{fingerprint(merged):08x}',
        'users_preserved': len(users),
        'effects': {wolf[key]: {'effect': specs[key][0], 'tint': tint_for(key, 'Wolf')} for key in WOLF_KEYS},
        'files': files, 'floors': floors,
        'generated': {name: digest((generated / name).read_bytes())
                      for name in ('ChampionEffects.hpp', 'PrivateEffectInfo.hpp')},
    }
    (args.output / 'effects-receipt.json').write_text(json.dumps(draft_receipt, indent=2) + '\n', encoding='utf-8')
    print(f'Wolf effects: {len(users)} accepted users preserved, 2 added, bank {len(merged)} bytes')


if __name__ == '__main__':
    main()
