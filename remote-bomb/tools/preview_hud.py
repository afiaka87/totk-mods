# SPDX-License-Identifier: MIT
# Copyright (c) Clay Mullis
"""Render the rune and gauge shaders over the extracted original artwork."""
from __future__ import annotations
import argparse
import hashlib
import json
import struct
from pathlib import Path
import moderngl
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]


def render(assets: Path, output: Path) -> dict:
    output.mkdir(parents=True,exist_ok=True)
    ctx=moderngl.create_standalone_context(require=450)
    program=ctx.program(vertex_shader=(ROOT/'shaders/hud.vert').read_text(),
                        fragment_shader=(ROOT/'shaders/hud.frag').read_text())
    target=ctx.texture((1280,720),4)
    fbo=ctx.framebuffer([target])
    uniform=ctx.buffer(reserve=48)
    uniform.bind_to_uniform_block(0)
    vao=ctx.vertex_array(program,[])
    images=[Image.open(assets/f'hud-{i}.png').convert('RGBA') for i in range(7)]
    textures=[ctx.texture(image.size,4,image.tobytes()) for image in images]
    for texture in textures:
        texture.filter=(moderngl.LINEAR,moderngl.LINEAR)
    for name,unit in (('rune',0),('ticks',1),('glow',2)):
        program[name].value=unit
    textures[4].use(1);textures[5].use(2)
    receipt={}
    for form in range(4):
        textures[form].use(0)
        previous=-1
        for phase,progress in (('ready',1.),('empty',0.),('quarter',.25),('half',.5),('three-quarter',.75),('full',1.)):
            fbo.use();fbo.clear(0,0,0,0)
            uniform.write(struct.pack('<12f',515,510,40,40,progress,float(phase!='ready'),1.7,0,1,1,1,1))
            vao.render(moderngl.TRIANGLES,vertices=6)
            raw=fbo.read(components=4,alignment=1)
            image=Image.frombytes('RGBA',(1280,720),raw).transpose(Image.Transpose.FLIP_TOP_BOTTOM)
            if image.getbbox() is None or not all(0<=n<=1280 for n in image.getbbox()):
                raise ValueError('Missing gauge fixture')
            crop=image.crop((530,465,620,555))
            visible=sum(alpha>16 for alpha in crop.getchannel('A').tobytes())
            if phase=='full':
                quadrants=[0]*4
                for y in range(90):
                    for x in range(90):
                        if 28**2<(x-45)**2+(y-45)**2<40**2 and crop.getpixel((x,y))[3]>32:
                            quadrants[int(x>=45)+2*int(y>=45)]+=1
                if min(quadrants)<30:
                    raise ValueError('A full recharge must cover all four circular quadrants')
            if phase!='ready':
                if visible<previous:
                    raise ValueError('Original recharge arc loses coverage while filling')
                previous=visible
            path=output/f'form-{form}-{phase}.png'
            crop.save(path)
            receipt[path.name]={'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),
                                'visible_pixels':visible,'bounds':image.getbbox()}
    if len({receipt[f'form-{i}-ready.png']['sha256'] for i in range(4)})!=4:
        raise ValueError('Shape/base-plus fixtures do not exercise distinct original icons')
    sheet=Image.new('RGB',(6*180,4*150),(22,34,44))
    for form in range(4):
        for col,phase in enumerate(('ready','empty','quarter','half','three-quarter','full')):
            image=Image.open(output/f'form-{form}-{phase}.png').convert('RGBA')
            sheet.paste(image,(col*180+45,form*150+30),image)
    sheet.save(output/'gallery.png')
    (output/'preview-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    return receipt


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assets',required=True,type=Path)
    parser.add_argument('--output',required=True,type=Path)
    args=parser.parse_args()
    result=render(args.assets,args.output)
    print(len(result),'actual shader fixtures; distinct original icons and monotonically filling gauges pass')
