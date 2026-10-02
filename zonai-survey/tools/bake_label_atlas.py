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

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / "scripts"))
from totk_mod_tools.textures.debug_font import FontAtlas, GlyphTable
from totk_mod_tools.textures.native_font import contour_cells
from totk_mod_tools.textures import png

LETTER_BYTES = 96 * 16 * 16
ICON_BYTES = 29 * 32 * 32
ATLAS_BYTES = LETTER_BYTES + ICON_BYTES
FONT_NAME, FONT_FILE = 'Rodin bold', 'RodinB.bfotf'


def name_strings(source: Path) -> list[str]:
    block = re.search(r'const char kNameBlob\[\]\s*=\s*(.*?);', source.read_text(encoding="utf-8"), re.S)
    if not block:
        raise ValueError("Name blob declaration changed")
    strings = b''.join(ast.literal_eval('b'+s) for s in re.findall(r'"(?:[^"\\]|\\.)*"', block[1])).decode('utf-8').split('\0')
    if any(not (32 <= ord(c) <= 126 or c == '\u2019') for s in strings for c in s):
        raise ValueError("A label needs a character outside the baked alphabet")
    return strings


def array(values, width=32) -> list[str]:
    values = list(values)
    return [','.join(map(str, values[i:i+width])) + ',' for i in range(0, len(values), width)]


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
    if any(not any(mask) for mask in icons):
        raise ValueError('Missing icon pixels')
    source = font_dir / FONT_FILE
    raw = source.read_bytes()
    chars, advances = contour_cells(raw, 16)
    if any(not any(mask) for mask in chars[1:]):
        raise ValueError('Missing letter pixels')
    letters = b''.join(chars)
    assert len(letters) == LETTER_BYTES
    bank = letters + b''.join(icons)
    assert len(bank) == ATLAS_BYTES and ATLAS_BYTES % 256 == 0
    output.mkdir(parents=True, exist_ok=True)
    header = ['// Generated from local assets; do not distribute as public source.', '#pragma once',
              '#include <cstdint>', 'namespace zonai_survey::atlas {',
              f'inline constexpr unsigned kLongestName = {max(map(len, names))};',
              'inline constexpr unsigned short kAdvances[96] = {' + ','.join(map(str, advances)) + '};',
              f'inline constexpr unsigned kPixelBytes = {len(bank)};',
              'inline constexpr unsigned kPixelPoolBytes = (kPixelBytes+4095)&~4095u;',
              'alignas(4096) inline unsigned char kPixels[kPixelPoolBytes] = {']
    header += array(bank)
    header += ['};', '}']
    (output / "LabelAtlas.hpp").write_text('\n'.join(header) + '\n', encoding="utf-8")
    masks = chars + icons
    preview = bytearray(512 * 160)
    for i, mask in enumerate(masks):
        size = 16 if i < 96 else 32
        if i<96: mask=bytes(max(0,min(255,round(255*(0.5+(v-128)/16)))) for v in mask)
        x, y = (i % 16) * 32, (i // 16) * 20 if i < 96 else 120 + ((i-96)//16)*20
        if i >= 96:
            x = ((i-96) % 16)*32
        for dy in range(16):
            for dx in range(16):
                preview[(y+dy)*512+x+dx] = mask[(dy*size//16)*size + dx*size//16]
    png.write(output / "label-atlas-preview.png", png.Image(512, 160, 1, bytes(preview)))
    receipt = {'pixel_bytes': len(bank), 'ascii_glyphs': 95, 'extra_codepoints': ['U+2019'], 'icons': 29,
               'longest_name': max(map(len, names)), 'names': len(names)-1,
               'pixels_sha256': hashlib.sha256(bank).hexdigest(),
               'letters_sha256': hashlib.sha256(letters).hexdigest(),
               'source_sha256': {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in (font, table, spec_path)}}
    receipt.update(baker_sha256=hashlib.sha256((ROOT/'scripts/totk_mod_tools/textures/native_font.py').read_bytes()).hexdigest(), font=FONT_NAME,
                   font_source={source.name: hashlib.sha256(raw).hexdigest()},
                   icon_pixels_sha256=hashlib.sha256(b''.join(icons)).hexdigest(),
                   point_size=156, source_cell_size=192, source_oversampling=12, encoding="signed-distance-letters/coverage-icons", distance_zero=128, distance_units=16, cell_baseline=[1,12], advance_units=64)
    (output / "label-atlas-receipt.json").write_text(json.dumps(receipt, indent=2)+'\n', encoding="utf-8")
    return receipt

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--mod', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--font-dir', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(bake(args.mod, args.output, args.font_dir)))
