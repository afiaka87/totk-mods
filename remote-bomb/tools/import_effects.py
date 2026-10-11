# SPDX-License-Identifier: MIT
# Copyright (c) Clay Mullis
"""Recover original bomb particle textures and colour curves from the owned RomFS."""
from __future__ import annotations
import argparse
import hashlib
import json
import math
import struct
import sys
from pathlib import Path

import oead
from PIL import Image

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT/'scripts'))
from totk_mod_tools.textures.bntx import Bank

SETS = ('BombAppear', 'RemoteBomb', 'RemoteBomb_Stripe', 'Test_Explosion', 'Test_Explosion_1')
NAMES = ('abstract04', 'abstract06', 'abstract09', 'ancientstring', 'animeglow_02',
         'circleglow', 'dripmask', 'flawuzu_03', 'hazemask', 'indwarp01', 'indwarp03',
         'kingsmoke', 'ofx_line', 'particles00', 'stripemask01', 'test_g_beam',
         'test_ohtasan_elec', 'test_ohtasan_elec01', 'torchmask', 'ukglow01', 'ukglow05')
NULL = 0xffffffff


def sections(data: bytes) -> list[dict]:
    if data[:4] != b'VFXB' or struct.unpack_from('<H', data, 10)[0] != 20:
        raise ValueError('Expected original VFXB version 20')
    rows, seen = [], set()

    def walk(offset: int, parent: str = '') -> None:
        while offset not in (0, NULL):
            if offset in seen or not 32 <= offset <= len(data)-32:
                raise ValueError('Invalid/cyclic particle section')
            seen.add(offset)
            if data[offset:offset+32] == bytes(32):
                break
            size, sub, following, _, payload, _, count = struct.unpack_from('<7I', data, offset+4)
            signature = data[offset:offset+4].decode('ascii')
            if size < 32 or offset+size > len(data):
                raise ValueError('Particle section exceeds its bundle')
            name = ''
            if signature in ('ESET', 'EMTR'):
                start = offset+payload+16
                stop = data.find(b'\0', start, start+64)
                if stop < start or stop >= offset+size:
                    raise ValueError('Invalid original emitter name')
                name = data[start:stop].decode('utf-8')
            rows.append(dict(offset=offset, size=size, signature=signature, data=payload,
                             count=count, name=name, parent=parent))
            if sub not in (0, NULL):
                walk(offset+sub, name or parent)
            if following in (0, NULL):
                break
            offset += following

    walk(struct.unpack_from('<H', data, 22)[0])
    return rows


def source_bundle(source: Path) -> tuple[bytes, str]:
    archive = oead.Sarc((source/'Pack/Bootup.pack').read_bytes())
    raw = bytes(archive.get_file('Effect/GameResident.sesetlist').data)
    return (bytes(oead.yaz0.decompress(raw)) if raw[:4] == b'Yaz0' else raw,
            hashlib.sha256(raw).hexdigest())


def extract(source: Path) -> tuple[list[tuple[Image.Image, dict]], list[dict], str]:
    data, source_hash = source_bundle(source)
    rows = sections(data)
    bank_row = next(row for row in rows if row['signature'] == 'GRTF')
    start = bank_row['offset']+bank_row['data']
    bank = Bank(data[start:start+bank_row['size']])
    id_row = next(row for row in rows if row['signature'] == 'GTNT')
    cursor = id_row['offset']+id_row['data']
    ids = {}
    for _ in bank.textures:
        texture_id, following, _ = struct.unpack_from('<QII', data, cursor)
        stop = data.index(b'\0', cursor+16)
        ids[texture_id] = data[cursor+16:stop].decode('utf-8')
        if following:
            cursor += following

    emitters = []
    for row in rows:
        if row['signature'] != 'EMTR' or row['parent'] not in SETS:
            continue
        start = row['offset']+row['data']+80
        if start+2568 > row['offset']+row['size']:
            raise ValueError('Original emitter parameter block is truncated')
        counts = struct.unpack_from('<5I', data, start+16)
        if max(counts) > 8:
            raise ValueError('Unexpected original colour key count')
        curves = [[list(struct.unpack_from('<4f', data, start+880+curve*128+i*16))
                   for i in range(counts[curve])] for curve in range(4)]
        colors = [list(struct.unpack_from('<4f', data, start+2392+i*16)) for i in range(2)]
        scale = [list(struct.unpack_from('<4f', data, start+1456+i*16)) for i in range(counts[4])]
        if not all(math.isfinite(value) for vector in colors+scale+sum(curves, []) for value in vector):
            raise ValueError('Original emitter colour contains a nonfinite value')
        textures = [ids.get(struct.unpack_from('<Q', data, start+2472+i*32)[0]) for i in range(3)]
        if any(name is not None and name not in NAMES for name in textures):
            raise ValueError('Uncatalogued original bomb texture')
        emitters.append(dict(set=row['parent'], name=row['name'], colors=colors,
                             curves=curves, scale=scale, textures=textures,
                             parameter_sha256=hashlib.sha256(data[start:start+2568]).hexdigest()))
    if len(emitters) != 23 or {e['set'] for e in emitters} != set(SETS):
        raise ValueError('Original bomb emitter census changed')
    images = []
    for name in NAMES:
        texture = bank.get(name)
        if texture.format_name == 'R8G8_UNORM':
            rg = bank.linear(texture)
            pixels = bytes(channel for i in range(0, len(rg), 2) for channel in (rg[i], rg[i+1], 0, 255))
        elif texture.format_name == 'RGBA8':
            pixels = bank.linear(texture)
        else:
            pixels = bank.decode(texture).data
        image = Image.frombytes('RGBA', (texture.width, texture.height), pixels)
        images.append((image, dict(name=name, format=texture.format_name, width=texture.width,
                                  height=texture.height, pixels_sha256=hashlib.sha256(pixels).hexdigest())))
    return images, emitters, source_hash


def vector(values) -> str:
    return '{'+','.join(f'{float(value):.9e}f' for value in values)+'}'


def build(source: Path, output: Path) -> dict:
    output.mkdir(parents=True, exist_ok=True)
    images, emitters, source_hash = extract(source)
    lines = ['// Private owned-game particle data; generated locally, never distributed.',
             '#pragma once', '#include "RemoteBombAssets.hpp"',
             'namespace remote_bomb::effect_assets {',
             'struct Curve { unsigned count; float keys[8][4]; };',
             'struct Emitter { float colors[2][4]; Curve curves[4]; Curve scale; int textures[3]; };']
    lines.append('enum EmitterId { '+','.join(e['set']+'_'+e['name'] for e in emitters)+' };')
    lines.append('enum TextureId { '+','.join(NAMES)+' };')
    textures = []
    for i, (image, _) in enumerate(images):
        raw = image.tobytes()
        lines.append(f'alignas(16) inline constexpr unsigned char image{i}[]={{')
        lines.extend(','.join(str(v) for v in raw[j:j+48])+',' for j in range(0, len(raw), 48))
        lines.append('};')
        textures.append(f'{{image{i},{image.width},{image.height}}}')
        image.save(output/f'effect-{i}.png')
    lines.append('inline constexpr assets::Texture textures[]{'+','.join(textures)+'};')
    lines.append('inline constexpr Emitter emitters[]{')
    for emitter in emitters:
        colors = '{'+','.join(vector(v) for v in emitter['colors'])+'}'
        curves = '{'+','.join('{'+str(len(keys))+',{'+','.join(vector(v) for v in keys)+'}}'
                             for keys in emitter['curves'])+'}'
        indices = '{'+','.join(str(NAMES.index(n)) if n else '-1' for n in emitter['textures'])+'}'
        scale = '{'+str(len(emitter['scale']))+',{'+','.join(vector(v) for v in emitter['scale'])+'}}'
        lines.append('{'+colors+','+curves+','+scale+','+indices+'},')
    lines += ['};', '}']
    header = ('\n'.join(lines)+'\n').encode()
    (output/'RemoteBombEffectAssets.hpp').write_bytes(header)
    receipt = dict(original_sha256=source_hash, vfx_version=20, images=[i for _, i in images],
                   emitters=emitters, header_sha256=hashlib.sha256(header).hexdigest())
    (output/'effect-asset-receipt.json').write_text(json.dumps(receipt, indent=2)+'\n', encoding='utf-8')
    return receipt


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = build(args.source, args.output)
    print('Recovered 23 original emitters and 21 original particle textures:', result['header_sha256'])
