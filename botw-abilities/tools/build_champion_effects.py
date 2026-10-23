# SPDX-License-Identifier: MIT
# Copyright (c) Clay Mullis
"""Build private Champion effect carriers from owned native effect resources."""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
import re
import struct
import sys
from pathlib import Path

import byml

ROOT = Path(__file__).resolve().parents[4]
sys.path[:0] = [str(ROOT / 'scripts'), str(ROOT / 'mods/code/totk-remote-bomb/tools'),
               str(ROOT / 'mods/code/totk-cryonis/tools')]
from totk_mod_tools.zstd_helper import TotkZstd
from import_audio import XLINK_SHA256, block, parser_budget, transcode, user_blocks
from resource_merge import encode_byml, sarc_files, write_sarc
from build_pillar import cloned_resource_floor

ELINK = 'ELink2/elink2.Product.110.belnk.zs'
BANKS = 'Effect/EffectFileInfo.Product.110.Nin_NX_NVN.byml.zs'
DONOR = 'EftLoc_AncientFacilityAltar_LightPillar'
TRIGGER_DONOR = 'EftLoc_AssassinSenior_FieldBarrier'
TABLES = ('ActorInfo', 'GameActorInfo')
COLORS = {'Mipha': (0.28, 0.78, 1.0), 'Revali': (0.38, 1.0, 0.72),
          'Daruk': (1.0, 0.3, 0.08), 'Urbosa': (1.0, 0.78, 0.18)}
# Sidon's shell is authored blue (0.2, 0.65, 0.85) and the call tint multiplies it, so orange needs red gain above 1.
SHELL_TINT = (5.0, 0.55, 0.1)
SPIRIT = {'Arrive': 'Npc_Sage_FollowSphere_Sage_Appear',
          'Leave': 'Npc_Sage_FollowSphere_Sage_Disappear',
          'Aura': 'Npc_SageRecastTime_Complete_Aura',
          'Flare': 'Npc_SageRecastTime_Complete_Flare'}


def effect_specs():
    effects = {f'{hero}{moment}': (effect, hero, 'static')
               for hero in COLORS for moment, effect in SPIRIT.items()}
    effects.update({
        'MiphaHeal': ('Dm_Npc_RevivalFairy_Resurrection', 'Mipha', 'static'),
        'MiphaTrace': ('Dm_Npc_RevivalFairy_Trace', 'Mipha', 'static'),
        'MiphaWater': ('Wpn_WearWater_Bubble_Start_Extend', 'Mipha', 'static'),
        'RevaliGust': ('Chm_Gust', 'Revali', 'static'),
        'RevaliUpdraft': ('Chm_Updraft_Strong', 'Revali', 'static'),
        'DarukShell': ('Ply_WaterBarrier', 'Daruk', 'static'),
        'DarukShellBreak': ('Ply_WaterBarrier_Break_Short', 'Daruk', 'static'),
        'DarukImpact': ('Npc_SageLight_Yunbo_Impact_String_Soul', 'Daruk', 'Sage_Soul_Goron'),
        'UrbosaRange': ('Npc_Rouge_LightningAttack_Area', 'Urbosa', 'Sage_Soul_Gerudo'),
        'UrbosaBolt': ('Chm_Lightning_Sage_Soul_Gerudo', 'Urbosa', 'static'),
    })
    return effects


def tint_for(key, hero):
    if key.startswith(('DarukAura','DarukImpact')):
        return (0.08,0.7,1.0)
    if key.startswith('DarukShell'):
        return SHELL_TINT
    return COLORS[hero]


def effect_user(template, name, effect, color):
    triggers = block(template, r'^    AlwaysTriggers \{')
    call = block(template, r'^      FieldBarrier\[0x5d2475fd\] \{')
    if triggers.count('Asset = FieldBarrier[0x5d2475fd]') != 1:
        raise ValueError('native always-on trigger changed')
    if call.count(f'RuntimeAssetName = "{TRIGGER_DONOR}"') != 1:
        raise ValueError('native field barrier call changed')
    params = block(template, r'^    UserParams \{')
    def f32_text(value):
        text = format(struct.unpack('<f', struct.pack('<f', value))[0], '.9g')
        return text if '.' in text or 'e' in text else text + '.0'

    tint = ''.join(f'          {channel} = {f32_text(value)}\n' for channel, value in zip(
        ('Red', 'Green', 'Blue'), color, strict=True))
    call = call.replace(f'RuntimeAssetName = "{TRIGGER_DONOR}"', f'RuntimeAssetName = "{effect}"')
    call = call.replace('          BitFlag = ', tint + '          BitFlag = ', 1)
    return (f'  {name} {{\n    Unknown = 0\n{params}\n{triggers}\n'
            f'    AssetCallTables {{\n{call}\n    }}\n  }}')


def clone_carrier(files, actor):
    result = {}
    for path, raw in files.items():
        data = byml.Byml(raw).parse()
        if path.startswith('Actor/'):
            data['Components']['ELinkRef'] = f'?Component/ELink/{actor}.engine__component__ELinkParam.bgyml'
        elif path.startswith('Component/ELink/'):
            data['UserName'] = actor
        else:
            raise ValueError('effect locator donor no longer has only actor and ELink files')
        result[path.replace(DONOR, actor)] = encode_byml(data, raw)
    return write_sarc(result)


def fury_range_user(z, romfs, template, native, name):
    pack = sarc_files(z.decompress(romfs / 'Pack/Actor/Sage_Soul_Gerudo.pack.zs'))
    path = 'GameParameter/GerudoSageParam/NpcGerudoSage.game__npc__GerudoSageParam.bgyml'
    params = byml.Byml(pack[path]).parse()
    first = params['FieldLightningParam'][0]
    if float(first['Range']) != 50.0 or first['XLinkKey'] != 'ThunderAttack_Lv1':
        raise ValueError('native 50-metre field calibration changed')
    call = block(native, r'^      ThunderAttack_Lv1\[0x[0-9a-f]+\] \{')
    if ('Scale = 102.800003' not in call or 'Matrix = 0x0' not in call or
            'RuntimeAssetName = "Npc_Rouge_LightningAttack_Area"' not in call):
        raise ValueError('native field placement/scale changed')
    call = re.sub(r'ThunderAttack_Lv1\[0x[0-9a-f]+\]', 'FieldBarrier[0x5d2475fd]', call)
    call = call.replace('AssetName = "ThunderAttack_Lv1"', 'AssetName = "FieldBarrier"')
    user_params = block(template, r'^    UserParams \{')
    return (f'  {name} {{\n    Unknown = 0\n{user_params}\n'
            f'    AssetCallTables {{\n{call}\n    }}\n  }}')


def build(args):
    if hashlib.sha256(args.xlink.read_bytes()).hexdigest() != XLINK_SHA256:
        raise ValueError('unreviewed XLink converter')
    z = TotkZstd(args.romfs)
    args.output.mkdir(parents=True, exist_ok=True)
    work = args.output / 'effects'
    work.mkdir(exist_ok=True)
    stock = z.decompress(args.romfs / ELINK)
    binary, source, names = work / 'stock.belnk', work / 'stock.txt', args.output / 'effect-users.txt'
    specs = effect_specs()
    actors = {key: f'EftLoc_Botw{key}' for key in specs}
    # Carriers dropped from the list would otherwise ride along in the composed payload.
    for stale in (args.output / 'romfs/Pack/Actor').glob('EftLoc_Botw*.pack.zs'):
        if stale.name.removesuffix('.pack.zs') not in actors.values():
            stale.unlink()
    names.write_text('\n'.join([DONOR, TRIGGER_DONOR, 'Sage_Soul_Gerudo', 'UI_GlobalSound',
                                'TotkRemoteBomb', *actors.values()]) + '\n', encoding='utf-8')
    binary.write_bytes(stock)
    transcode(args.xlink, binary, source, 'EXKing', names)
    text = source.read_text(encoding='utf-8')
    users = user_blocks(text)
    if users.keys() & set(actors.values()):
        raise ValueError('private Champion user already exists')
    banks_raw = z.decompress(args.romfs / BANKS)
    banks = byml.Byml(banks_raw).parse()
    bank_data = {}
    for bank in set(spec[2] for spec in specs.values()):
        bank_data[bank] = z.decompress(args.romfs / f'Effect/{bank}.Nin_NX_NVN.esetb.byml.zs')
    custom = {}
    for key, (effect, hero, bank) in specs.items():
        if effect.encode() + b'\0' not in bank_data[bank]:
            raise ValueError(f'{effect} absent from bank {bank}')
        custom[actors[key]] = (fury_range_user(z,args.romfs,users[TRIGGER_DONOR],users['Sage_Soul_Gerudo'],actors[key])
                              if key == 'UrbosaRange' else
                              effect_user(users[TRIGGER_DONOR], actors[key], effect, tint_for(key,hero)))
        if bank != 'static':
            if banks['BinaryDict'].get(bank) != bank or bank not in banks['EsetbList']:
                raise ValueError(f'unexpected bank mapping for {bank}')
            banks['BinaryDict'][actors[key]] = bank
    candidate = work / 'candidate.txt'
    candidate.write_text(text.rstrip()[:-1] + '\n'.join(custom.values()) + '\n}\n', encoding='utf-8')
    output, restored = work / 'candidate.belnk', work / 'restored.txt'
    transcode(args.xlink, candidate, output, 'EXKing', names)
    transcode(args.xlink, output, restored, 'EXKing', names)
    after = restored.read_text(encoding='utf-8')
    if (user_blocks(after) != users | custom
            or after.split('Users {', 1)[0] != text.split('Users {', 1)[0]):
        raise ValueError('effect conversion changed existing users or private calls')
    resource_table = z.decompress(args.romfs / 'System/Resource/ResourceSizeTable.Product.121.rsizetable.zs')
    floors, files, receipts = {}, {}, {}

    def emit(path, raw, original=b'', donor_resource=None):
        target = args.output / 'romfs' / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(z.compress(raw, target.name))
        if z.decompress(target) != raw:
            raise ValueError(f'{path}: compression round-trip failed')
        resource = path.removesuffix('.zs')
        baseline = parser_budget(resource_table, donor_resource or (resource if original else f'Pack/Actor/{DONOR}.pack'))
        floors[resource] = max(baseline, len(raw)) + max(
            65536, (len(raw) - len(original)) * 10)
        files[path] = hashlib.sha256(target.read_bytes()).hexdigest()

    emit(ELINK, output.read_bytes(), stock)
    emit(BANKS, encode_byml(banks, banks_raw), banks_raw)
    donor = sarc_files(z.decompress(args.romfs / f'Pack/Actor/{DONOR}.pack.zs'))
    for key, actor in actors.items():
        raw = clone_carrier(donor, actor)
        emit(f'Pack/Actor/{actor}.pack.zs', raw)
        for member, value in sarc_files(raw).items():
            source = member.replace(actor, DONOR)
            floors[member] = cloned_resource_floor(parser_budget(resource_table, source),
                                                    len(donor[source]), len(value))
        receipts[key] = {'actor': actor, 'effect': specs[key][0], 'bank': specs[key][2],
                         'tint': None if key == 'UrbosaRange' else tint_for(key,specs[key][1]), 'pack_bytes': len(raw)}
        if key == 'UrbosaRange':
            receipts[key].update(manual_handle=True, native_radius_m=50, native_call_scale=102.800003,
                                 expansion_seconds=1.0, expansion_timing='visual tuning; not original timing proof')
    for table in TABLES:
        path = f'RSDB/{table}.Product.121.rstbl.byml.zs'
        original = z.decompress(args.romfs / path)
        rows = byml.Byml(original).parse()
        donor_row = next(row for row in rows if row['__RowId'] == DONOR)
        for actor in actors.values():
            row = copy.deepcopy(donor_row)
            row['__RowId'] = actor
            if 'ELinkUserName' in row:
                row['ELinkUserName'] = actor
            rows.append(row)
        rows.sort(key=lambda row: row['__RowId'])
        emit(path, encode_byml(rows, original), original)
    (args.output / 'restbl-forces.tsv').write_text(''.join(f'{key}\t{value}\n' for key, value in sorted(floors.items())),
                                                 encoding='utf-8')
    receipt = {'stock_elink_sha256': hashlib.sha256(stock).hexdigest(), 'stock_users_preserved': len(users),
               'xlink_sha256': XLINK_SHA256, 'effects': receipts, 'files': files,
               'trigger_donor': TRIGGER_DONOR, 'actor_donor': DONOR,
               'runtime_verified': False}
    (args.output / 'effects-receipt.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    generated = args.output / 'generated'
    generated.mkdir(exist_ok=True)
    header = ('// SPDX-License-Identifier: MIT\n// Copyright (c) Clay Mullis\n#pragma once\n'
              '#include <array>\n#include <cstdint>\nnamespace botw_abilities::champions {\n'
              'enum class Effect : std::uint8_t {\n    ' + ', '.join(specs) + ', Count\n};\n'
              f'inline constexpr std::array<const char*, {len(specs)}> kEffectActors{{{{\n')
    header += ''.join(f'    "{actors[key]}",\n' for key in specs) + '}};\n}\n'
    (generated / 'ChampionEffects.hpp').write_text(header, encoding='utf-8')
    print(f'Champion effects: {len(actors)} private carriers, {len(users)} stock users preserved')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--romfs', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--xlink', required=True, type=Path)
    build(parser.parse_args())
