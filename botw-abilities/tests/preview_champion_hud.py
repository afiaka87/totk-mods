# SPDX-License-Identifier: MIT
# Copyright (c) Clay Mullis
"""Render the actual HUD shaders and prove the legacy bomb path retains its pixels."""
import argparse
import hashlib
import json
import re
import struct
import subprocess
from pathlib import Path

import moderngl
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[4]
BOMB = ROOT / 'mods/code/totk-remote-bomb'
STASIS = ROOT / 'mods/code/totk-stasis'
COLORS = ((.28,.78,1,1),(.38,1,.72,1),(1,.3,.08,1),(1,.78,.18,1))


def expand(path):
    return re.sub(r'^#include "([^"\n]+)"$', lambda match: expand(path.parent / match[1]),
                  path.read_text(), flags=re.M)


def validate(args):
    args.output.mkdir(parents=True, exist_ok=True)
    ctx = moderngl.create_standalone_context(require=450)
    vertex = (BOMB / 'shaders/hud.vert').read_text()
    current = ctx.program(vertex_shader=vertex, fragment_shader=(BOMB / 'shaders/hud.frag').read_text())
    previous_text = subprocess.check_output(['git', 'show', args.baseline_ref + ':mods/code/totk-remote-bomb/shaders/hud.frag'], cwd=ROOT).decode()
    previous = ctx.program(vertex_shader=vertex, fragment_shader=previous_text)
    uniform = ctx.buffer(reserve=48)
    uniform.bind_to_uniform_block(0)
    programs = [(program, ctx.vertex_array(program, [])) for program in (previous, current)]
    images = [Image.open(BOMB / f'build/generated/hud-{i}.png').convert('RGBA') for i in range(7)]
    textures = [ctx.texture(image.size,4,image.tobytes()) for image in images]
    header = args.assets.read_text()
    for index in range(4):
        match = re.search(r'unsigned char icon' + str(index) + r'\[\]\s*=\s*\{([^}]+)\}', header, re.S)
        if not match:
            raise AssertionError(f'missing original HUD mask {index}')
        mask = bytes(int(number.strip(),0) for number in match[1].split(',') if number.strip())
        assert len(mask) == 80*80
        rgba = Image.new('RGBA',(80,80),(255,255,255,0))
        rgba.putalpha(Image.frombytes('L',(80,80),mask))
        textures.append(ctx.texture(rgba.size,4,rgba.tobytes()))
    for texture in textures:
        texture.filter = (moderngl.LINEAR,moderngl.LINEAR)
    for program,_ in programs:
        for name,unit in (('rune',0),('ticks',1),('glow',2)):
            program[name].value = unit
    target = ctx.texture((1280,720),4)
    fbo = ctx.framebuffer([target])
    fbo.use()
    textures[4].use(1)
    textures[5].use(2)

    def draw(program_index, icon, rect, progress, cooling, packed=0, tint=(1,1,1,1)):
        textures[icon].use(0)
        uniform.write(struct.pack('<12f',*rect,progress,float(cooling),1.4 if packed<0 else 1.7,float(packed),*tint))
        programs[program_index][1].render(moderngl.TRIANGLES,vertices=6)

    regressions = 0
    for icon in range(4):
        for progress,cooling in ((1,False),(0,True),(.25,True),(.5,True),(.75,True),(1,True)):
            rendered = []
            for variant in range(2):
                fbo.clear(0,0,0,0)
                draw(variant,icon,(184.4,122.4,22,22),progress,cooling)
                rendered.append(fbo.read(components=4,alignment=1))
            assert rendered[0] == rendered[1], f'legacy bomb shader pixel change: {icon}/{progress}/{cooling}'
            regressions += 1

    screenshots = []
    pip_assertions = 0
    for phase,progress in (('used',1),('recharging',.5),('ready',1)):
        fbo.clear(0,0,0,0)
        draw(1,2,(184.4,122.4,22,22),.5,True)
        for index,capacity in enumerate((1,3,6,3)):
            charges = 0 if phase == 'recharging' else capacity if phase == 'ready' else max(0,capacity-1)
            draw(1,7+index,(234.4+50*index,122.4,22,22),progress,phase == 'recharging',
                 -(1+charges+8*capacity),COLORS[index])
        image = Image.frombytes('RGBA',(1280,720),fbo.read(components=4,alignment=1)).transpose(Image.Transpose.FLIP_TOP_BOTTOM)
        image.save(args.output / f'{phase}.png')
        assert image.getbbox() is not None
        screenshots.append((phase,image))

    for charges in range(7):
        fbo.clear(0,0,0,0)
        draw(1,9,(334.4,122.4,22,22),1,False,-(1+charges+8*6),COLORS[2])
        image = Image.frombytes('RGBA',(1280,720),fbo.read(components=4,alignment=1)).transpose(Image.Transpose.FLIP_TOP_BOTTOM)
        for pip in range(6):
            pixel = image.getpixel((round(334.4+(pip-2.5)*.21*22),round(122.4+.75*22)))
            assert (pixel[3] > 128) == (pip < charges), f'wrong pip count: {charges}/{pip}/{pixel}'
            pip_assertions += 1

    fullscreen = '#version 450\nlayout(location=0) out vec2 screenUv;\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);screenUv=p;gl_Position=vec4(p*2-1,0,1);}'
    stasis = ctx.program(vertex_shader=fullscreen, fragment_shader=expand(STASIS / 'shaders/stasis.frag'))
    block = [0.0] * (153*4)
    block[0:4] = [1280,720,.1,500]
    block[16:20] = [0,1,0,0]
    block[20] = 1
    block[28] = 100
    block[53*4+3] = 122.4/720
    block[55*4+3] = 2
    stasis_buffer = ctx.buffer(struct.pack('<'+'f'*len(block),*block))
    stasis_buffer.bind_to_uniform_block(0)
    fbo.clear(0,0,0,0)
    ctx.vertex_array(stasis,[]).render(moderngl.TRIANGLES,vertices=3)
    ring = Image.frombytes('RGBA',(1280,720),fbo.read(components=4,alignment=1)).transpose(Image.Transpose.FLIP_TOP_BOTTOM)
    bounds = ring.getbbox()
    assert bounds and abs((bounds[1]+bounds[3])*.5-122.4) < 1, bounds
    old_stasis = subprocess.check_output(['git','show',args.baseline_ref+':mods/code/totk-stasis/shaders/stasis.frag'],cwd=ROOT).decode()
    old_stasis = re.sub(r'^#include "([^"\n]+)"$',lambda match:expand(STASIS/'shaders'/match[1]),old_stasis,flags=re.M)
    old_program = ctx.program(vertex_shader=fullscreen,fragment_shader=old_stasis)
    for center in (0,.14):
        block[53*4+3] = center
        stasis_buffer.write(struct.pack('<'+'f'*len(block),*block))
        rendered = []
        for program in (stasis,old_program):
            fbo.clear(0,0,0,0)
            ctx.vertex_array(program,[]).render(moderngl.TRIANGLES,vertices=3)
            rendered.append(fbo.read(components=4,alignment=1))
        assert rendered[0] == rendered[1], f'legacy Stasis shader pixel change: {center}'

    gallery = Image.new('RGB',(720,3*140),(22,34,44))
    labels = ImageDraw.Draw(gallery)
    for row,(phase,image) in enumerate(screenshots):
        image.alpha_composite(ring)
        crop = image.crop((105,92,417,154)).resize((624,124),Image.Resampling.NEAREST)
        gallery.paste(crop,(80,row*140+12),crop)
        labels.text((8,row*140+60),phase,fill=(230,230,230))
    gallery.save(args.output / 'gallery.png')
    receipt = {'legacy_pixel_identical_cases':regressions,'legacy_stasis_pixel_identical_cases':2,'charge_pip_assertions':pip_assertions,
               'stasis_bounds':bounds,'gallery_sha256':hashlib.sha256((args.output/'gallery.png').read_bytes()).hexdigest()}
    (args.output / 'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(json.dumps(receipt))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assets',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--baseline-ref',default='7997baec')
    validate(parser.parse_args())
