# SPDX-License-Identifier: MIT
# Copyright (c) Clay Mullis
"""Build private native Champion gameplay actors from the owned clean dump."""
import argparse
import copy
import hashlib
import json
import math
import sys
from pathlib import Path

import byml

ROOT = Path(__file__).resolve().parents[4]
sys.path[:0] = [str(ROOT / 'scripts'), str(ROOT / 'mods/code/totk-remote-bomb/tools'),
               str(ROOT / 'mods/code/totk-cryonis/tools')]
from totk_mod_tools.zstd_helper import TotkZstd
from resource_merge import encode_byml, sarc_files, write_sarc
from import_audio import parser_budget
from build_pillar import cloned_resource_floor


def tuning():
    value = json.loads(Path(__file__).with_name('champion_gameplay.json').read_text())['gale']
    if value['actor'] != 'AscendingCurrent_BotwGale' or value['donor'] != 'AscendingCurrent':
        raise ValueError('unreviewed native updraft identity')
    numbers = [value[key] for key in ('wind_speed', 'launch_speed', 'channel_seconds', 'duration_seconds')]
    if len(value['scale']) != 3 or any(not math.isfinite(number) or number <= 0 for number in numbers + value['scale']):
        raise ValueError('invalid Gale tuning')
    return value


def clone_updraft(files, settings):
    donor, actor = settings['donor'], settings['actor']
    actor_path = f'Actor/{donor}.engine__actor__ActorParam.bgyml'
    table_path = f'GameParameter/GameParameterTable/{donor}.engine__actor__GameParameterTable.bgyml'
    param_path = f'GameParameter/AscendingCurrentParam/{donor}.game__object__AscendingCurrentParam.bgyml'
    actor_node, table, param = (byml.Byml(files[path]).parse() for path in (actor_path, table_path, param_path))
    if actor_node['Category'] != 'System' or actor_node['Components']['GameParameterTableRef'] != '?' + table_path:
        raise ValueError('native updraft actor contract changed')
    if table['Components']['AscendingCurrentParam'] != '?' + param_path:
        raise ValueError('native updraft parameter reference changed')
    if param:
        raise ValueError('review changed native wind defaults')
    renamed = {
        actor_path: f'Actor/{actor}.engine__actor__ActorParam.bgyml',
        table_path: f'GameParameter/GameParameterTable/{actor}.engine__actor__GameParameterTable.bgyml',
        param_path: f'GameParameter/AscendingCurrentParam/{actor}.game__object__AscendingCurrentParam.bgyml',
    }
    actor_node['Components']['GameParameterTableRef'] = '?' + renamed[table_path]
    table['Components']['AscendingCurrentParam'] = '?' + renamed[param_path]
    param['WindSpeed'] = byml.Float(settings['wind_speed'])
    output, origins = dict(files), {path: path for path in files}
    for old, node in ((actor_path, actor_node), (table_path, table), (param_path, param)):
        new = renamed[old]
        output[new] = encode_byml(node, output.pop(old))
        origins.pop(old)
        origins[new] = old
    return output, origins


def build(args):
    settings = tuning()
    z = TotkZstd(args.romfs)
    output = args.output / 'gameplay'
    donor_path = f'Pack/Actor/{settings["donor"]}.pack.zs'
    original = z.decompress(args.romfs / donor_path)
    source = sarc_files(original)
    cloned, origins = clone_updraft(source, settings)
    budgets = z.decompress(args.romfs / 'System/Resource/ResourceSizeTable.Product.121.rsizetable.zs')
    floors, files = {}, {}

    def emit(path, raw, baseline, baseline_size):
        target = output / 'romfs' / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(z.compress(raw, target.name))
        if z.decompress(target) != raw:
            raise ValueError(f'compression round-trip: {path}')
        floors[path.removesuffix('.zs')] = cloned_resource_floor(parser_budget(budgets, baseline), baseline_size, len(raw))
        files[path] = hashlib.sha256(target.read_bytes()).hexdigest()

    actor = settings['actor']
    emit(f'Pack/Actor/{actor}.pack.zs', write_sarc(cloned), donor_path.removesuffix('.zs'), len(original))
    for name, raw in cloned.items():
        if name != origins[name]:
            floors[name] = cloned_resource_floor(parser_budget(budgets, origins[name]), len(source[origins[name]]), len(raw))
    for table_name in ('ActorInfo', 'GameActorInfo'):
        path = f'RSDB/{table_name}.Product.121.rstbl.byml.zs'
        raw = z.decompress(args.romfs / path)
        rows = byml.Byml(raw).parse()
        for spec in (settings,):
            if any(row['__RowId'] == spec['actor'] for row in rows):
                raise ValueError('private gameplay actor already exists in source')
            row = copy.deepcopy(next(row for row in rows if row['__RowId'] == spec['donor']))
            row['__RowId'] = spec['actor']
            rows.append(row)
        rows.sort(key=lambda item: item['__RowId'])
        data = encode_byml(rows, raw)
        target = output / 'romfs' / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(z.compress(data, target.name))
        if z.decompress(target) != data:
            raise ValueError('table round-trip failed')
        floors[path[:-3]] = max(parser_budget(budgets, path[:-3]), len(data)) + max(65536, 10 * (len(data) - len(raw)))
        files[path] = hashlib.sha256(target.read_bytes()).hexdigest()
    (output / 'restbl-forces.tsv').write_text(''.join(f'{key}\t{value}\n' for key, value in sorted(floors.items())))
    receipt = {'gale': settings, 'files': files, 'unchanged_pack_members': sum(name == old for name, old in origins.items()),
               'member_origins': origins, 'source_pack_sha256': hashlib.sha256(original).hexdigest()}
    (output / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    generated = args.output / 'generated'
    generated.mkdir(parents=True, exist_ok=True)
    header = '// SPDX-License-Identifier: MIT\n// Copyright (c) Clay Mullis\n#pragma once\n#include <array>\nnamespace botw_abilities::champions {\n'
    header += f'inline constexpr char kGaleUpdraftActor[] = "{actor}";\n'
    for field, name in (('launch_speed', 'LaunchSpeed'), ('channel_seconds', 'ChannelSeconds'), ('duration_seconds', 'DurationSeconds')):
        header += f'inline constexpr float kGale{name} = {float(settings[field])}f;\n'
    header += 'inline constexpr std::array<float, 3> kGaleUpdraftScale{' + ', '.join(f'{float(x)}f' for x in settings['scale']) + '};\n}\n'
    (generated / 'GaleResources.hpp').write_text(header)
    print(f'Champion gameplay: {actor}; native graphs and {receipt["unchanged_pack_members"]} wind members unchanged')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--romfs', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    build(parser.parse_args())
