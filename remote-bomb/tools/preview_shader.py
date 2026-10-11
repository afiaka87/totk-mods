# SPDX-License-Identifier: MIT
# Copyright (c) Clay Mullis
"""Render the bomb shader with the extracted original meshes and pixels."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import re
import moderngl
import numpy as np
from PIL import Image
from reference_mesh import load_mesh, parameter
from preview_pipeline import Pipeline

ROOT = Path(__file__).resolve().parents[1]

def shader(path: Path):
    return re.sub(r'^#include "([^"\n]+)"$',
                  lambda match: shader(path.parent / match[1]),
                  path.read_text(), flags=re.M)

def render(reference: Path, output: Path, icons=False):
    output.mkdir(parents=True, exist_ok=True)
    ctx = moderngl.create_standalone_context(require=450)
    program = ctx.program(vertex_shader=shader(ROOT / 'shaders/bomb.vert'),
                          fragment_shader=shader(ROOT / 'shaders/bomb.frag'))
    w = h = 480 if icons else 768
    pipeline = Pipeline(ctx, w, h)
    depth = ctx.texture((w, h), 1, np.ones((h, w), dtype='f4').tobytes(), dtype='f4')
    depth.use(0)
    for name, unit in [('sceneDepth', 0), ('originalAlbedo', 1), ('originalEmission', 2),
                       ('originalNormal', 3), ('originalSpecular', 4)]:
        program[name].value = unit
    uniforms = ctx.buffer(reserve=13*16)
    uniforms.bind_to_uniform_block(0)
    receipt = {}
    for form in ('sphere', 'cube'):
        source = reference / form
        rows, indices, draws, model = load_mesh(source)
        padded = np.zeros((3420, 4), dtype='f4')
        padded[:len(rows)] = rows
        geometry = ctx.buffer(padded.tobytes())
        geometry.bind_to_uniform_block(1)
        index_buffer = ctx.buffer(indices.tobytes())
        vao = ctx.vertex_array(program, [], index_buffer, index_element_size=2)
        prefix = 'Item_RemoteBombCube' if form == 'cube' else 'Item_RemoteBomb'
        textures = []
        for unit, suffix in enumerate(('Alb', 'Emm', 'Nrm', 'Spm'), 1):
            image = Image.open(source / f'{prefix}_{suffix}.png').convert('RGBA')
            texture = ctx.texture(image.size, 4, image.tobytes())
            texture.filter = (moderngl.LINEAR, moderngl.LINEAR)
            texture.use(unit)
            textures.append(texture)
        for angle in ((45,) if icons else (0, 45, 125)):
            yaw = np.radians(angle)
            eye = np.array([1.25*np.sin(yaw), .82, 1.25*np.cos(yaw)])
            target_point = np.array([0, .26, 0])
            backward = (eye-target_point) / np.linalg.norm(eye-target_point)
            right = np.cross([0, 1, 0], backward)
            right /= np.linalg.norm(right)
            up = np.cross(backward, right)
            basis = np.stack([right, up, backward], axis=1)
            values = np.zeros((13, 4), dtype='f4')
            values[0] = [w, h, .1, 1000]
            values[12] = [.1, 1000, 0, 0]  # private depth range
            values[1:4, :3] = basis
            values[1:4, 3] = eye
            values[4] = [2.2, 2.2, 0, 0]
            values[5:8, :3] = np.eye(3)
            values[8] = [-.4, .8, .3, 0]
            pipeline.begin()
            uniforms.bind_to_uniform_block(0)
            depth.use(0)
            for unit, texture in enumerate(textures, 1):
                texture.use(unit)
            for draw in draws:
                material = model['materials'][draw.material]
                mask = next(item['_value'][0] for item in material['RenderInfos']
                            if item['Name'] == 'gsys_render_state_mode') == 'mask'
                values[8, 3] = float(mask)
                emission = parameter(material, 'const_color0')[:3]
                strength = parameter(material, 'const_value0')[0] if not mask else 0
                values[9] = [*emission, strength]
                uniforms.write(values.tobytes())
                vao.render(moderngl.TRIANGLES, vertices=draw.count, first=draw.first)
            rendered = np.flipud(pipeline.composite())
            color = rendered[:, :, :3] if icons else rendered[:, :, :3] + np.array([.015, .025, .035])*(1-rendered[:, :, 3:4])
            color = np.power(np.clip(color/(1+color), 0, 1), 1/2.2)
            file = output / f'{form}-{angle}.png'
            pixels = np.concatenate((color, np.clip(rendered[:,:,3:4],0,1)),axis=2) if icons else color
            Image.fromarray((pixels*255).astype('uint8')).save(file)
            receipt[file.name] = {'sha256': hashlib.sha256(file.read_bytes()).hexdigest(),
                                  'vertices': len(rows)//3, 'indices': len(indices)}
        vao.release()
        geometry.release()
        index_buffer.release()
        for texture in textures:
            texture.release()
    (output / 'preview-receipt.json').write_text(json.dumps(receipt, indent=2))
    print('Rendered six actual GLSL/original-mesh previews; material transfer remains a candidate.')

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--reference', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    render(args.reference, args.output)
