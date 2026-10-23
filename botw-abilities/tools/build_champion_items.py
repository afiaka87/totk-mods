# SPDX-License-Identifier: MIT
# Copyright (c) Clay Mullis
"""Author private Champion Key Items and original artwork without replacing Sage vows."""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
import struct
import subprocess
import sys
from pathlib import Path

import astc_encoder as astc
import byml
import oead
from PIL import Image

ROOT = Path(__file__).resolve().parents[4]
sys.path[:0] = [str(ROOT / 'scripts'), str(ROOT / 'mods/code/totk-remote-bomb/tools')]
from totk_mod_tools.textures import gpu, swizzle
from totk_mod_tools.textures.bntx import Bank
from totk_mod_tools.zstd_helper import TotkZstd
from import_audio import parser_budget
from resource_merge import append_msbt, encode_byml, read_msbt, sarc_files, write_sarc
from build_champion_state import BLESSINGS
from build_champion_effects import COLORS
from build_champion_audio import unyaz
from build_wheel_icons import encode

DONOR = 'Obj_CaveWellHonor_00'
ICON_TEMPLATE = 'UI/Tex/Icon/Obj_SageSoul_Rito.bntx.zs'
ICON_INDEX = {'Mipha': 1, 'Revali': 2, 'Daruk': 0, 'Urbosa': 3}
LAYOUT = {'Mipha': 'B', 'Revali': 'G', 'Daruk': 'R', 'Urbosa': 'Y'}
MALS = 'Mals/USen.Product.121.sarc.zs'
MESSAGE_MEMBER = 'ActorMsg/PouchContent.msbt'


def source_messages(raw):
    if raw[:10] != b'MsgStdBn\xff\xfe':
        raise ValueError('expected original little-endian MSBT')
    sections, offset = {}, 32
    for _ in range(struct.unpack_from('<H', raw, 14)[0]):
        size = struct.unpack_from('<I', raw, offset + 4)[0]
        end = offset + 16 + (size + 15) // 16 * 16
        tag = raw[offset:offset + 4]
        if tag in sections or end > len(raw):
            raise ValueError('invalid original MSBT section')
        sections[tag] = raw[offset:end]
        offset = end
    view = bytearray(raw[:32] + sections[b'LBL1'] + sections[b'TXT2'])
    struct.pack_into('<H', view, 14, 2)
    struct.pack_into('<I', view, 18, len(view))
    return read_msbt(bytes(view))[2]


def inventory_text(botw):
    boot = oead.Sarc(unyaz((botw / 'Pack/Bootup_USen.pack').read_bytes()))
    messages = oead.Sarc(unyaz(bytes(boot.get_file('Message/Msg_USen.product.ssarc').data)))
    raw = bytes(messages.get_file('ActorType/Item.msbt').data)
    original = source_messages(raw)
    entries = {}
    controls = {'Rito': (0xcd0c, 'X'), 'Goron': (0xcd0e, 'ZL'), 'Gerudo': (0xcd0d, 'Y')}
    for _, region, _ in BLESSINGS:
        actor = 'Obj_BotwHeroSoul_' + region
        for source, target in (('Name', 'Name'), ('Desc', 'Caption')):
            value = original[f'Obj_HeroSoul_{region}_{source}']
            if source == 'Desc' and region in controls:
                code, button = controls[region]
                control = struct.pack('<5H', 14, 1, 7, 2, code)
                if value.count(control) != 1:
                    raise ValueError(f'original button control changed: {region}')
                value = value.replace(control, button.encode('utf-16le'))
            if b'\x0e\0' in value or b'\x0f\0' in value:
                raise ValueError('untranslated BotW text control')
            entries[f'{actor}_{target}'] = value
    return entries, hashlib.sha256(raw).hexdigest()


def extend_rows(rows, table):
    before = {row['__RowId']: row for row in rows}
    if len(before) != len(rows):
        raise ValueError('duplicate native inventory row')
    result = copy.deepcopy(rows)
    for _, region, _ in BLESSINGS:
        actor = 'Obj_BotwHeroSoul_' + region
        if actor in before:
            raise ValueError(f'private item already exists: {actor}')
        row = copy.deepcopy(before[DONOR])
        row['__RowId'] = actor
        if table == 'ActorInfo':
            row['ActorName'] = actor
        if table == 'PouchActorInfo':
            if row['PouchCategory'] != 'KeyItem' or row['PouchSpecialDeal'] != 'None':
                raise ValueError('inventory donor changed')
        result.append(row)
    return sorted(result, key=lambda row: row['__RowId'])


def build(args):
    z = TotkZstd(args.totk_romfs)
    work = args.output / 'icons'
    work.mkdir(parents=True, exist_ok=True)
    table = z.decompress(args.totk_romfs / 'System/Resource/ResourceSizeTable.Product.121.rsizetable.zs')
    floors, receipt = {}, {'items': {}, 'resources': {}}

    def write(path, raw, original, donor_path=None):
        destination = args.output / 'romfs' / path
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(z.compress(raw, destination.name))
        if z.decompress(destination) != raw:
            raise ValueError(f'compression changed bytes: {path}')
        base = parser_budget(table, donor_path or path[:-3])
        floors[path[:-3]] = max(base, len(raw)) + max(65536, (len(raw) - len(original)) * 10)
        receipt['resources'][path] = hashlib.sha256(destination.read_bytes()).hexdigest()

    for name in ('ActorInfo', 'GameActorInfo', 'PouchActorInfo'):
        path = f'RSDB/{name}.Product.121.rstbl.byml.zs'
        raw = z.decompress(args.totk_romfs / path)
        rows = byml.Byml(raw).parse()
        updated = extend_rows(rows, name)
        write(path, encode_byml(updated, raw), raw)
        by_id = {row['__RowId']: row for row in updated}
        if any(by_id[row['__RowId']] != row for row in rows):
            raise ValueError('native inventory row changed')
    raw = z.decompress(args.totk_romfs / MALS)
    files = sarc_files(raw)
    messages, receipt['source_messages_sha256'] = inventory_text(args.botw_romfs)
    files[MESSAGE_MEMBER] = append_msbt(files[MESSAGE_MEMBER], messages)
    write(MALS, write_sarc(files), raw)
    receipt['messages'] = {name: value.decode('utf-16le').rstrip('\0') for name, value in messages.items()}
    boot = oead.Sarc(unyaz((args.botw_romfs / 'Pack/Bootup.pack').read_bytes()))
    common = oead.Sarc(unyaz(bytes(boot.get_file('Layout/Common.sblarc').data)))
    atlas = bytes(common.get_file('timg/__Combined.bntx').data)
    bank = Bank(atlas)
    receipt['source_atlas_sha256'] = hashlib.sha256(atlas).hexdigest()
    template = z.decompress(args.totk_romfs / ICON_TEMPLATE)
    target_info = Bank(template).textures[0]
    if (target_info.width, target_info.height, target_info.format_id, target_info.mip_count) != (144, 144, 0x2d06, 1):
        raise ValueError('native Sage-vow icon geometry changed')
    tasks, compressed, uploads = [], {}, []
    for hero, region, _ in BLESSINGS:
        actor = 'Obj_BotwHeroSoul_' + region
        name = f'Nt_IconSinJuNew_{ICON_INDEX[hero]:02d}^d'
        layout = bytes(common.get_file(f'blyt/PaEiketuEf{LAYOUT[hero]}_00.bflyt').data)
        if name.encode() not in layout:
            raise ValueError('original Champion HUD layout does not reference its symbol')
        texture = bank.get(name)
        if (texture.width, texture.height, texture.format_id, texture.mip_count) != (80, 80, 0x0201, 1):
            raise ValueError('original Champion symbol geometry changed')
        mask = bank.linear(texture)
        uploads.append(gpu.GpuTexture(name, 80, 80, 'R8_UNORM', 1, (gpu.ONE, gpu.ONE, gpu.ONE, gpu.R), mask))
        image = Image.new('RGBA', (80, 80), tuple(round(c * 255) for c in COLORS[hero]) + (0,))
        image.putalpha(Image.frombytes('L', (80, 80), mask))
        image = image.resize((144, 144), Image.Resampling.LANCZOS)
        image.save(work / f'{hero}.png')
        blocks, decoded = encode(image.tobytes(), astc)
        ew, eh = target_info.elements
        surface = swizzle.swizzle(blocks, ew, eh, 16, target_info.block_height)
        changed = bytearray(template)
        changed[target_info.data_offset:target_info.data_offset + len(surface)] = surface
        source, target = work / f'{hero}-input.bntx', work / f'{actor}.bntx'
        source.write_bytes(changed)
        tasks.append({'actor': actor, 'input': str(source.resolve()), 'output': str(target.resolve())})
        compressed[actor] = blocks
        receipt['items'][hero] = {'actor': actor, 'source_texture': name,
                                  'hud_mask_sha256': hashlib.sha256(mask).hexdigest(), 'hud_bytes': len(mask),
                                  'inventory_tint': COLORS[hero], 'inventory_size': 144,
                                  'astc_mean_channel_error': sum(abs(a - b) for a, b in zip(image.tobytes(), decoded)) / len(decoded)}
    tasks_path = work / 'rename-tasks.json'
    tasks_path.write_text(json.dumps(tasks, indent=2), encoding='utf-8')
    helper = Path(__file__).with_name('rename_champion_icons.ps1')
    process = subprocess.run(['powershell.exe', '-NoProfile', '-File', str(helper.resolve()),
                              '-Toolbox', str(args.toolbox.resolve()), '-Tasks', str(tasks_path.resolve())],
                             capture_output=True, text=True)
    (work / 'rename.log').write_text(process.stdout + process.stderr, encoding='utf-8')
    if process.returncode:
        raise ValueError('icon container rename failed; see icons/rename.log')
    for task in tasks:
        actor = task['actor']
        raw = Path(task['output']).read_bytes()
        converted = Bank(raw)
        info = converted.textures[0]
        if (len(converted.textures) != 1 or info.name != actor
                or (info.width, info.height, info.format_id, info.mip_count) != (144, 144, 0x2d06, 1)
                or converted.linear(info) != compressed[actor] or b'Obj_SageSoul_Rito' in raw):
            raise ValueError(f'inventory icon round trip failed: {actor}')
        write(f'UI/Tex/Icon/{actor}.bntx.zs', raw, template, ICON_TEMPLATE[:-3])
    header = ['// Private original-game HUD masks; generated locally.', '#pragma once',
              'namespace botw_abilities::champions::hud {', gpu.TEXTURE_STRUCT]
    values = []
    for index, upload in enumerate(uploads):
        lines, value = gpu.cxx(f'icon{index}', upload)
        header.extend(lines)
        values.append(value)
    header += ['inline constexpr Texture kIcons[]{' + ','.join(values) + '};', '}']
    generated = args.output / 'generated'
    generated.mkdir(exist_ok=True)
    (generated / 'ChampionHudAssets.hpp').write_text('\n'.join(header) + '\n', encoding='utf-8')
    (args.output / 'restbl-forces.tsv').write_text(''.join(f'{name}\t{floor}\n' for name, floor in sorted(floors.items())), encoding='utf-8')
    (args.output / 'items-receipt.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    print('Four private Key Items, original names/descriptions, native inventory icons and exact HUD masks')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('botw-romfs', 'totk-romfs', 'toolbox', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    build(parser.parse_args())
