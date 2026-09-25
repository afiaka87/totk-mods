# SPDX-License-Identifier: MIT
"""Bake only Survey's letters/icons, offline. No font container ships or loads at runtime."""
from __future__ import annotations
import argparse
import ast
import hashlib
import json
from pathlib import Path
import re
import sys
from io import BytesIO
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / "scripts"))
from totk_mod_tools.textures.debug_font import FontAtlas, GlyphTable
from totk_mod_tools.textures import png

ATLAS_BYTES = 96 * 16 * 16 + 29 * 32 * 32
FONT_NAMES = ('Rodin regular',)
FONT_FILES = ('RodinM.bfotf',)

def letters(path: Path):
    raw = path.read_bytes()
    # BFTTFutil's Win-format wrapper; verified against this local archive's header.
    if raw[:4] != bytes.fromhex('d99b871a'):
        raise ValueError('Unexpected BFOTF wrapper')
    key = (2785117442).to_bytes(4, 'big')
    decoded = bytes(c ^ key[i % 4] for i, c in enumerate(raw))
    if int.from_bytes(decoded[4:8], 'big') != len(raw)-8 or decoded[8:12] != b'OTTO':
        raise ValueError('Invalid decoded OpenType length/signature')
    face = ImageFont.truetype(BytesIO(decoded[8:]), 13)
    masks, advances = [], []
    for c in list(range(32,127)) + [0x2019]:
        char = chr(c)
        if c != 32 and bytes(face.getmask(char)) == bytes(face.getmask('\U0010ffff')):
            raise ValueError(f'{path.name}: absent glyph U+{c:04X}')
        l,t,r,b = face.getbbox(char, anchor='ls')
        if l+1 < 0 or t+12 < 0 or r+1 > 16 or b+12 > 16:
            raise ValueError(f'{path.name}: clipped glyph U+{c:04X}')
        image = Image.new('L', (16,16))
        ImageDraw.Draw(image).text((1,12), char, font=face, fill=255, anchor='ls')
        masks.append(image.tobytes())
        advances.append(round(face.getlength(char)*64))
    return masks, advances

def name_strings(source: Path) -> list[str]:
    block = re.search(r'const char kNameBlob\[\]\s*=\s*(.*?);', source.read_text(encoding="utf-8"), re.S)
    if not block:
        raise ValueError("Name blob declaration changed")
    strings = b''.join(ast.literal_eval('b'+s) for s in re.findall(r'"(?:[^"\\]|\\.)*"', block[1])).decode('utf-8').split('\0')
    if any(not (32 <= ord(c) <= 126 or c == '\u2019') for s in strings for c in s):
        raise ValueError("A label needs a character outside the baked alphabet")
    return strings

def bake(mod: Path, output: Path, font_dir: Path) -> dict:
    font = mod / "romfs/Lib/sead/nvn_font/nvn_font_jis1_mipmap.xtx"
    table = mod / "romfs/Lib/sead/nvn_font/nvn_font_jis1_tbl.bin"
    spec_path = mod / "design/icons.json"
    spec = json.loads(spec_path.read_text(encoding="utf-8"))
    names = name_strings(mod / "src/generated/GlyphTables.cpp")
    atlas, glyphs = FontAtlas.load(font), GlyphTable.load(table)
    if atlas.levels[0].glyph != 32 or atlas.levels[1].glyph != 16 or len(spec['icons']) != 29:
        raise ValueError("Unexpected source atlas geometry or icon count")
    icons = [atlas.read_cell(glyphs.index_of(int(i['code_point'], 16)), 0) for i in spec['icons']]
    banks, metrics = [], []
    sources = [font_dir / name for name in FONT_FILES]
    for source in sources:
        chars, advance = letters(source)
        masks = chars + icons
        if any(not any(mask) for mask in masks[1:]):
            raise ValueError('Missing letter or icon pixels')
        banks.append(b''.join(masks)); metrics.append(advance)
    data = b''.join(banks)
    assert len(data) == ATLAS_BYTES and ATLAS_BYTES % 256 == 0
    output.mkdir(parents=True, exist_ok=True)
    header = ['// Generated from local assets; do not distribute as public source.', '#pragma once',
              '#include <cstdint>', 'namespace zonai_survey::atlas {',
              f'inline constexpr unsigned kLongestName = {max(map(len, names))};',
              f'inline constexpr unsigned kFontCount = {len(banks)}, kFontBankBytes = {ATLAS_BYTES};',
              'inline constexpr const char* kFontNames[] = {' + ','.join(json.dumps(n) for n in FONT_NAMES) + '};',
              'inline constexpr unsigned short kAdvances[96] = {' + ','.join(map(str,metrics[0])) + '};',
              f'inline constexpr unsigned kPixelBytes = {len(data)};',
              'inline constexpr unsigned kPixelPoolBytes = (kPixelBytes+4095)&~4095u;',
              'alignas(4096) inline unsigned char kPixels[kPixelPoolBytes] = {']
    header += [','.join(map(str, data[i:i+32])) + ',' for i in range(0, len(data), 32)]
    header += ['};', '}']
    (output / "LabelAtlas.hpp").write_text('\n'.join(header) + '\n', encoding="utf-8")
    preview = bytearray(512 * 160)
    for i, mask in enumerate(masks):
        size = 16 if i < 96 else 32
        x, y = (i % 16) * 32, (i // 16) * 20 if i < 96 else 120 + ((i-96)//16)*20
        if i >= 96:
            x = ((i-96) % 16)*32
        for dy in range(16):
            for dx in range(16):
                preview[(y+dy)*512+x+dx] = mask[(dy*size//16)*size + dx*size//16]
    png.write(output / "label-atlas-preview.png", png.Image(512, 160, 1, bytes(preview)))
    comparison=Image.new('RGB',(640,120),(25,35,38))
    for row,(bank,advances) in enumerate(zip(banks,metrics)):
        x=8.0
        for char in FONT_NAMES[row]+': Apple / Blue Nightshade / Strong Construct Bow 0123':
            index=ord(char)-32
            mask=Image.frombytes('L',(16,16),bank[index*256:(index+1)*256])
            comparison.paste((240,245,255),(round(x),row*36+8),mask)
            x+=advances[index]/64
    comparison.resize((1280,240),Image.Resampling.NEAREST).save(output/'font-comparison.png')
    receipt = {'pixel_bytes': len(data), 'ascii_glyphs': 95, 'extra_codepoints': ['U+2019'], 'icons': 29,
               'longest_name': max(map(len, names)), 'names': len(names)-1,
               'pixels_sha256': hashlib.sha256(data).hexdigest(),
               'source_sha256': {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in (font, table, spec_path)}}
    receipt.update(fonts=list(FONT_NAMES), font_bank_bytes=ATLAS_BYTES,
                   font_sources={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sources},
                   icon_pixels_sha256=hashlib.sha256(b''.join(icons)).hexdigest(),
                   point_size=13, cell_baseline=[1,12], advance_units=64)
    (output / "label-atlas-receipt.json").write_text(json.dumps(receipt, indent=2)+'\n', encoding="utf-8")
    return receipt

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--mod', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--font-dir', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(bake(args.mod, args.output, args.font_dir)))
