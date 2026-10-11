# SPDX-License-Identifier: MIT
# Copyright (c) Clay Mullis
"""Render the world-effect shaders with the extracted original particle data."""
from __future__ import annotations
import argparse
import hashlib
import json
import struct
from pathlib import Path
import moderngl
import numpy as np
from PIL import Image, ImageDraw
from compile_shaders import expanded_source

ROOT = Path(__file__).resolve().parents[1]


def curve(keys, time, channel, initial):
    value, previous = initial, 0.
    for key in keys:
        if time <= key[3] and key[3] > previous:
            weight = max(0., min(1., (time-previous)/(key[3]-previous)))
            return value+(key[channel]-value)*weight
        value, previous = key[channel], key[3]
    return value


def render(assets: Path, output: Path) -> dict:
    output.mkdir(parents=True, exist_ok=True)
    original = json.loads((assets/'effect-asset-receipt.json').read_text())
    emitters = {(e['set'], e['name']): e for e in original['emitters']}
    names = {image['name']: index for index, image in enumerate(original['images'])}
    ctx = moderngl.create_standalone_context(require=450)
    program = ctx.program(vertex_shader=expanded_source(ROOT/'shaders/effects.vert'),
                          fragment_shader=expanded_source(ROOT/'shaders/effects.frag'))
    target = ctx.texture((640, 360), 4, dtype='f4')
    fbo = ctx.framebuffer([target])
    depth = ctx.texture((640, 360), 1, struct.pack('<f', 1.)*(640*360), dtype='f4')
    depth.use(0)
    images = [Image.open(assets/f'effect-{i}.png').convert('RGBA') for i in range(21)]
    textures = [ctx.texture(image.size, 4, image.tobytes()) for image in images]
    for texture in textures:
        texture.filter = moderngl.LINEAR, moderngl.LINEAR
    for name, unit in (('sceneDepth', 0), ('originalMask', 1), ('originalNoise', 2), ('originalGlow', 3)):
        program[name].value = unit
    uniform = ctx.buffer(reserve=160)
    uniform.bind_to_uniform_block(0)
    vao = ctx.vertex_array(program, [])
    ctx.enable(moderngl.BLEND)
    ctx.blend_func = moderngl.ONE, moderngl.ONE, moderngl.ZERO, moderngl.ONE

    def layer(group, name, radius, progress, kind, opacity, count=1, center=(0., 0., 0.)):
        emitter = emitters[group, name]
        values = np.zeros((10, 4), dtype='f4')
        values[0] = [640, 360, .1, 100]
        values[1:4, :3] = np.eye(3)
        values[1:4, 3] = [0, 0, 18 if group.startswith('Test_') else 2.5]
        values[4] = [1.2, 2.1333333, 0, 0]
        values[5:8, :3] = np.eye(3)
        values[5:8, 3] = center
        alpha = curve(emitter['curves'][1], progress, 0, emitter['colors'][0][3])
        values[8] = [radius*curve(emitter['scale'], progress, 0, 1.), progress, kind, opacity*alpha]
        values[9, :3] = [curve(emitter['curves'][0], progress, c, emitter['colors'][0][c]) for c in range(3)]
        for unit, texture_name in enumerate(emitter['textures'], 1):
            textures[names[texture_name or 'ukglow05']].use(unit)
        uniform.write(values.tobytes())
        vao.render(moderngl.TRIANGLES, vertices=count*6)

    receipt = {}
    fixtures = []
    for event, ages in (('appearance', (0., 2., 4., 10., 19., 20.)),
                        ('explosion', (0., 2., 4., 12., 25., 30.))):
        for age in ages:
            fbo.use()
            fbo.clear(0, 0, 0, 0)
            if event == 'appearance' and 2. <= age < 20.:
                p = (age-2.)/18.
                size = .3+.25*p
                layer('BombAppear', 'Glow', size*2, p, 6, .18)
                layer('BombAppear', 'Ring', size, p, 1, .25*(1-p))
                layer('BombAppear', 'Flear0', size*3, p, 5, .2)
                layer('BombAppear', 'Ptcl_main', size, p, 3, .3, 12)
            if event == 'explosion' and age < 30.:
                p = age/30.
                size = min(6., .4+age*1.3)
                layer('Test_Explosion_1', 'Ring', size, p, 1, .12*(1-p))
                layer('Test_Explosion_1', 'Spike_2', size*1.2, p, 2, .15, 16)
                if age >= 2.:
                    layer('Test_Explosion_1', 'InfinityStill', size*1.15, p, 7, .8*(1-p))
                    layer('Test_Explosion', 'Plasma_Shurink_Copy1', size*.78, p, 0, 1.5)
                    layer('Test_Explosion', 'Spark_1', size, p, 3, .4, 32)
            raw = np.flipud(np.frombuffer(fbo.read(components=4, dtype='f4'), dtype='f4').reshape(360, 640, 4))
            if not np.isfinite(raw).all():
                raise ValueError('Nonfinite effect fixture')
            coverage = int(np.count_nonzero(np.max(raw[:, :, :3], axis=2)>.005))
            if age == ages[-1] and coverage:
                raise ValueError('Expired effect remains visible')
            if age in (4., 10., 12.) and coverage < 20:
                raise ValueError('Active original-data effect is invisible')
            rgb = np.maximum(raw[:, :, :3], 0)
            color = (np.clip(rgb/(1+rgb), 0, 1)**(1/2.2)*255).astype('uint8')
            image = Image.fromarray(color)
            file = output/f'{event}-{age:g}.png'
            image.save(file)
            receipt[file.name] = dict(sha256=hashlib.sha256(file.read_bytes()).hexdigest(),
                                      visible_pixels=coverage, age_frames=age,
                                      tone_mapping='Offline display only; no simulated bloom')
            fixtures.append(image)
    sheet = Image.new('RGB', (960, 360), (16, 24, 32))
    draw = ImageDraw.Draw(sheet)
    for i, fixture in enumerate(fixtures):
        fixture = fixture.resize((160, 90))
        sheet.paste(fixture, ((i%6)*160, (i//6)*180+25))
        draw.text(((i%6)*160+4, (i//6)*180+4), list(receipt)[i], fill='white')
    sheet.save(output/'gallery.png')
    (output/'preview-receipt.json').write_text(json.dumps(receipt, indent=2)+'\n')
    return receipt


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assets', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    result = render(args.assets, args.output)
    print(len(result), 'actual effect-shader fixtures; live coverage, finite output and expiration pass')
