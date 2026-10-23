# SPDX-License-Identifier: MIT
# Copyright (c) Clay Mullis
"""Add persistent Champion state and Key Item discovery without editing native flags."""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
import sys
from pathlib import Path

import byml

ROOT = Path(__file__).resolve().parents[4]
sys.path[:0] = [str(ROOT / 'scripts'), str(ROOT / 'mods/code/totk-remote-bomb/tools')]
from totk_mod_tools.zstd_helper import TotkZstd
from build_inventory import hash_name
from import_audio import parser_budget
from resource_merge import encode_byml, saved_metadata

RESOURCE = 'GameData/GameDataList.Product.110.byml.zs'
BLESSINGS = (('Mipha', 'Zora', 1), ('Revali', 'Rito', 3), ('Daruk', 'Goron', 6), ('Urbosa', 'Gerudo', 3))


def extend(clean):
    result = copy.deepcopy(clean)
    data = result['Data']
    occupied = {int(row['Hash']) for rows in data.values() for row in rows}
    templates = {kind: next(row for row in clean['Data'][kind]
                           if row.get('SaveFileIndex') == 0 and row.get('ResetTypeValue') == 16
                           and set(row) == {'Hash', 'DefaultValue', 'SaveFileIndex', 'ResetTypeValue'})
                 for kind in ('Bool', 'Int', 'Float')}
    records = {}

    def add(kind, name, default):
        hashed = hash_name(name)
        if hashed in occupied:
            raise ValueError(f'Champion GameData collision: {name}')
        occupied.add(hashed)
        row = copy.deepcopy(templates[kind])
        row.update(Hash=byml.UInt(hashed), DefaultValue=default)
        data[kind].append(row)
        return hashed

    for hero, region, charges in BLESSINGS:
        actor = f'Obj_BotwHeroSoul_{region}'
        record = {'actor': actor}
        for key, kind, default in (('Enabled', 'Bool', True), ('Charges', 'Int', byml.Int(charges)),
                                   ('Recharge', 'Float', byml.Float(0.0))):
            record[key] = add(kind, f'BotwChampions.{hero}.{key}', default)
        for registry_name in ('IsGet', 'IsGetAnyway'):
            registry = next(row for row in data['Struct'] if row['Hash'] == hash_name(registry_name))
            actor_hash = hash_name(actor)
            if any(row['Hash'] == actor_hash for row in registry['DefaultValue']):
                raise ValueError(f'Champion discovery registry collision: {actor}')
            flag = add('Bool', f'{registry_name}.{actor}', False)
            registry['DefaultValue'].append({'Hash': byml.UInt(actor_hash), 'Value': byml.UInt(flag)})
            record[registry_name] = flag
        records[hero] = record
    result['MetaData'] = saved_metadata(clean, data)
    return result, records


def build(args):
    z = TotkZstd(args.romfs)
    original = z.decompress(args.romfs / RESOURCE)
    clean = byml.Byml(original).parse()
    result, records = extend(clean)
    raw = encode_byml(result, original)
    output = args.output / 'romfs' / RESOURCE
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(z.compress(raw, output.name))
    if z.decompress(output) != raw:
        raise ValueError('Champion GameData compression changed bytes')
    table = z.decompress(args.romfs / 'System/Resource/ResourceSizeTable.Product.121.rsizetable.zs')
    floor = max(parser_budget(table, RESOURCE[:-3]), len(raw)) + max(65536, (len(raw) - len(original)) * 10)
    (args.output / 'state-restbl-forces.tsv').write_text(f'{RESOURCE[:-3]}\t{floor}\n', encoding='utf-8')
    generated = args.output / 'generated'
    generated.mkdir(exist_ok=True)
    header = ('// SPDX-License-Identifier: MIT\n// Copyright (c) Clay Mullis\n#pragma once\n'
              '#include <array>\n#include <cstdint>\nnamespace botw_abilities::champions {\n'
              'struct SaveKeys { const char* actor; std::uint32_t enabled, charges, recharge; };\n'
              'inline constexpr std::array<SaveKeys, 4> kSaveKeys{{\n')
    for hero, _, _ in BLESSINGS:
        row = records[hero]
        header += f'    {{"{row["actor"]}", 0x{row["Enabled"]:08x}, 0x{row["Charges"]:08x}, 0x{row["Recharge"]:08x}}},\n'
    (generated / 'ChampionSaveSchema.hpp').write_text(header + '}};\n}\n', encoding='utf-8')
    receipt = {'source_sha256': hashlib.sha256(original).hexdigest(),
               'output_sha256': hashlib.sha256(output.read_bytes()).hexdigest(), 'blessings': records,
               'metadata_before': clean['MetaData'], 'metadata_after': result['MetaData'],
               'new_flags': 20, 'parser_floor': floor}
    (args.output / 'state-receipt.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    print('Champion state: 12 persistent state flags, 8 discovery flags, 160 bytes per save')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--romfs', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    build(parser.parse_args())
