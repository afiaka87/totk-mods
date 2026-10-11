# SPDX-License-Identifier: MIT
# Copyright (c) Clay Mullis
"""Package private original bomb geometry and pixels for the native renderer."""
from pathlib import Path
import argparse
import hashlib
import json
import sys
import numpy as np
from PIL import Image
from reference_mesh import load_mesh, parameter
sys.path.insert(0, str(Path(__file__).resolve().parents[4]/'scripts'))
from totk_mod_tools.textures import gpu


def build(source: Path, botw: Path, output: Path):
    lines = ['// Private owned-game assets; generated locally, never distributed.',
             '#pragma once', '#include <cstddef>', '#include <cstdint>',
             'namespace remote_bomb::assets {',
             'struct Draw { unsigned first, count; bool mask; float emission[4]; };',
             'struct Mesh { const float (*rows)[4]; const std::uint16_t *indices; unsigned indexCount; const Draw *draws; unsigned drawCount; };',
             gpu.TEXTURE_STRUCT]
    receipt = {'forms': {}}
    def numbers(values):
        return '{' + ','.join(f'{float(v):.9e}f' for v in values) + '}'
    meshes, textures = [], []
    for form in ('sphere', 'cube'):
        folder = source / form
        rows, indices, draws, model = load_mesh(folder)
        padded = np.zeros((3420, 4), dtype='f4')
        padded[:len(rows)] = rows
        lines.append(f'alignas(16) inline constexpr float {form}Rows[3420][4]={{')
        lines.extend(numbers(row) + ',' for row in padded)
        lines.append('};')
        lines.append(f'alignas(16) inline constexpr std::uint16_t {form}Indices[]={{')
        lines.extend(','.join(str(v) for v in indices[i:i+32]) + ',' for i in range(0, len(indices), 32))
        lines.append('};')
        lines.append(f'inline constexpr Draw {form}Draws[]={{')
        for draw in draws:
            material = model['materials'][draw.material]
            mode = next(item['_value'][0] for item in material['RenderInfos']
                        if item['Name'] == 'gsys_render_state_mode')
            if mode not in ('opaque', 'mask'):
                raise ValueError('Unverified original material mode: ' + mode)
            color = parameter(material, 'const_color0')[:3]
            strength = 0 if mode == 'mask' else parameter(material, 'const_value0')[0]
            lines.append('{' + f'{draw.first},{draw.count},{str(mode == "mask").lower()},' +
                         numbers((*color, strength)) + '},')
        lines.append('};')
        meshes.append('{' + f'{form}Rows,{form}Indices,{len(indices)},{form}Draws,{len(draws)}' + '}')
        form_receipt = {'vertices': len(rows)//3, 'indices': len(indices),
                        'mesh_sha256': hashlib.sha256((folder/'mesh.json').read_bytes()).hexdigest(),
                        'packed_rows_sha256': hashlib.sha256(padded.tobytes()).hexdigest(),
                        'packed_indices_sha256': hashlib.sha256(indices.tobytes()).hexdigest(), 'textures': {}}
        prefix = 'Item_RemoteBombCube' if form == 'cube' else 'Item_RemoteBomb'
        archive = gpu.read_pack_member(botw/'Pack/TitleBG.pack', f'Model/{prefix}.Tex.sbfres')
        bank = gpu.bank_in(archive)
        for suffix in ('Alb', 'Emm', 'Nrm', 'Spm'):
            name = f'{prefix}_{suffix}'
            texture = gpu.original(bank, name)
            # The reference export is the same original decoded by another tool: it may differ
            # by one rounding step per channel, never in alpha.
            path = folder / (name + '.png')
            reference = np.asarray(Image.open(path).convert('RGBA'), dtype=np.int16)
            sampled = np.frombuffer(gpu.expand(texture), np.uint8).reshape(reference.shape).astype(np.int16)
            if np.abs(sampled[..., :3] - reference[..., :3]).max() > 1 or (sampled[..., 3] != reference[..., 3]).any():
                raise ValueError('Original texture no longer matches its reference export: ' + name)
            array, value = gpu.cxx(name, texture)
            lines.extend(array)
            textures.append(value)
            form_receipt['textures'][name] = texture.receipt() | {
                    'reference_sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
        form_receipt['texture_archive_sha256'] = hashlib.sha256(archive).hexdigest()
        receipt['forms'][form] = form_receipt
    lines.append('inline constexpr Mesh meshes[]{' + ','.join(meshes) + '};')
    lines.append('inline constexpr Texture textures[]{' + ','.join(textures) + '};')
    lines.append('}')
    output.mkdir(parents=True, exist_ok=True)
    header = output / 'RemoteBombAssets.hpp'
    header.write_text('\n'.join(lines)+'\n', encoding='utf-8')
    receipt['header_sha256'] = hashlib.sha256(header.read_bytes()).hexdigest()
    (output/'asset-receipt.json').write_text(json.dumps(receipt, indent=2)+'\n', encoding='utf-8')
    print('Packed two original meshes, four material draws and eight original compressed texture maps.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', required=True, type=Path)
    parser.add_argument('--botw-romfs', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    build(args.source, args.botw_romfs, args.output)
