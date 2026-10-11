# SPDX-License-Identifier: MIT
# Copyright (c) Clay Mullis
"""Recover original bomb rune icons and recharge masks from the owned RomFS."""
from __future__ import annotations
import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

import oead
from PIL import Image

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT/'scripts'))
from totk_mod_tools.textures import gpu
from totk_mod_tools.textures.bntx import Bank

ICONS = ('Obj_RemoteBombBall', 'Obj_RemoteBombCube',
         'Obj_RemoteBombBall2', 'Obj_RemoteBombCube2')
GAUGES = ('Sd_ItemGaugeR_04^s', 'Sd_ItemGaugeRGlow_04^s', 'Sd_ItemGaugeRGlowFull_04^s')


def decoded(raw: bytes) -> bytes:
    return bytes(oead.yaz0.decompress(raw)) if raw[:4] == b'Yaz0' else raw


def pixels(bank: Bank, name: str) -> tuple[Image.Image, dict, gpu.GpuTexture]:
    texture = bank.get(name)
    image = bank.decode(texture)
    rgba = Image.frombytes('RGBA', (image.width, image.height),
                           bank.linear(texture) if texture.format_name == 'RGBA8' else image.data)
    if texture.format_name == 'BC4':
        mask = rgba.getchannel('R')
        rgba = Image.new('RGBA', rgba.size, (255, 255, 255, 0))
        rgba.putalpha(mask)
    if not 8 <= image.width <= 512 or not 8 <= image.height <= 512:
        raise ValueError('Unexpected original HUD dimensions: ' + texture.name)
    if rgba.getchannel('A').getextrema()[0] != 0 or rgba.getchannel('A').getextrema()[1] == 0:
        raise ValueError('Expected a transparent original icon/mask: ' + texture.name)
    # The game samples the original blocks; a recharge mask reads as white with the mask as alpha.
    upload = gpu.original(bank, name, (gpu.ONE, gpu.ONE, gpu.ONE, gpu.R) if texture.format_name == 'BC4' else None)
    if gpu.expand(upload) != rgba.tobytes():
        raise ValueError('Original HUD texture does not sample as its decode: ' + texture.name)
    return rgba, {'name': texture.name, 'width': image.width, 'height': image.height,
                  'format': texture.format_name, 'pixels_sha256': hashlib.sha256(rgba.tobytes()).hexdigest(),
                  'upload': upload.receipt()}, upload


def build(source: Path, output: Path) -> dict:
    output.mkdir(parents=True, exist_ok=True)
    receipt = {'originals': {}, 'images': []}
    recovered = []
    for name in ICONS:
        raw = (source/'UI/StockItem'/f'{name}.sbitemico').read_bytes()
        fres = decoded(raw)
        positions = [match.start() for match in re.finditer(b'BNTX', fres)]
        if fres[:4] != b'FRES' or len(positions) != 1:
            raise ValueError('Expected one embedded stock icon bank: ' + name)
        bank = Bank(fres[positions[0]:])
        if len(bank.textures) != 1:
            raise ValueError('Expected one original rune icon: ' + name)
        recovered.append(pixels(bank, bank.textures[0].name))
        receipt['originals'][name] = hashlib.sha256(raw).hexdigest()
    boot = (source/'Pack/Bootup.pack').read_bytes()
    arc = oead.Sarc(decoded(boot))
    common = decoded(bytes(arc.get_file('Layout/Common.sblarc').data))
    common_arc = oead.Sarc(common)
    atlas = bytes(common_arc.get_file('timg/__Combined.bntx').data)
    bank = Bank(atlas)
    for name in GAUGES:
        recovered.append(pixels(bank, name))
    layout = bytes(common_arc.get_file('blyt/PaAppGauge_00.bflyt').data)
    if not all(name.encode() in layout for name in GAUGES):
        raise ValueError('Original recharge layout does not reference the recovered masks')
    receipt['originals']['common_atlas'] = hashlib.sha256(atlas).hexdigest()
    receipt['originals']['gauge_layout'] = hashlib.sha256(layout).hexdigest()
    lines = ['// Private owned-game HUD artwork; generated locally, never distributed.',
             '#pragma once', '#include "RemoteBombAssets.hpp"',
             'namespace remote_bomb::hud_assets {']
    textures = []
    for i, (image, info, upload) in enumerate(recovered):
        array, value = gpu.cxx(f'image{i}', upload)
        lines.extend(array)
        textures.append(value)
        image.save(output/f'hud-{i}.png')
        receipt['images'].append(info)
    lines += ['inline constexpr assets::Texture textures[]{'+','.join(textures)+'};','}']
    header = ('\n'.join(lines)+'\n').encode()
    (output/'RemoteBombHudAssets.hpp').write_bytes(header)
    receipt['header_sha256'] = hashlib.sha256(header).hexdigest()
    (output/'hud-asset-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    return receipt


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    result=build(args.source,args.output)
    print('Recovered four original bomb icons and three original recharge masks:', result['header_sha256'])
