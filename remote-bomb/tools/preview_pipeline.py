# SPDX-License-Identifier: MIT
# Copyright (c) Clay Mullis
"""Run the private depth buffer and bloom shaders without touching the game's scene depth."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import moderngl
import numpy as np
from PIL import Image
from compile_shaders import expanded_source

ROOT = Path(__file__).resolve().parents[1]


class Pipeline:
    def __init__(self, ctx, width, height, capacity=None, glow_divisor=1, native_origin=False):
        self.ctx, self.width, self.height = ctx, width, height
        capacity = capacity or (width, height)
        self.glow_divisor = glow_divisor
        self.glow_width, self.glow_height = tuple((axis+glow_divisor-1)//glow_divisor for axis in capacity)
        self.images = []
        for divisor in (1, 1, 2, 2, 4, 4):
            base = capacity if not self.images else tuple((axis+glow_divisor-1)//glow_divisor for axis in capacity)
            size = tuple((axis+divisor-1)//divisor for axis in base)
            self.images.append(ctx.texture(size, 4, dtype='f2'))
        self.depth = ctx.depth_texture(capacity)
        self.mesh = ctx.framebuffer(self.images[:2] if glow_divisor==1 else [self.images[0],ctx.texture(capacity,4,dtype='f2')], self.depth)
        glow_capacity=tuple((axis+glow_divisor-1)//glow_divisor for axis in capacity)
        self.emission=ctx.framebuffer([ctx.texture(glow_capacity,4,dtype='f2'),self.images[1]],ctx.depth_texture(glow_capacity))
        self.filters = [ctx.framebuffer([image]) for image in self.images]
        self.result = ctx.texture((width, height), 4, dtype='f4')
        self.output = ctx.framebuffer([self.result])
        self.uniform = ctx.buffer(reserve=32)
        self.programs, self.vaos = {}, {}
        for name in ('downsample', 'blur', 'composite'):
            fragment = expanded_source(ROOT/f'shaders/{name}.frag')
            if name=='blur' and glow_divisor==1:
                fragment=fragment.replace('sampleImage(source,(pixel+kernel.xy*float(i)+.5)/sizes.xy,sizes.xy)',
                                          'texelFetch(source,imagePixel(pixel+kernel.xy*float(i),sizes.xy),0)')
            if not native_origin:
                fragment = fragment.replace('#version 450', '#version 450\n#define REMOTE_BOMB_PREVIEW', 1)
            program = ctx.program(vertex_shader=expanded_source(ROOT/'shaders/post.vert'),
                                  fragment_shader=fragment)
            self.programs[name] = program
            self.vaos[name] = ctx.vertex_array(program, [])
            for unit, texture in enumerate(('narrowBloom', 'wideBloom') if name == 'composite' else ('source',)):
                program[texture].value = unit

    def begin(self, depth=True, emission=False):
        target=self.emission if emission else self.mesh
        target.use()
        self.ctx.viewport=(0,0,self.glow_width,self.glow_height) if emission else (0,0,self.width,self.height)
        target.depth_mask=True
        target.clear(0,0,0,0,depth=1)
        self.ctx.disable(moderngl.BLEND)
        if depth:
            self.ctx.enable(moderngl.DEPTH_TEST)
        else:
            self.ctx.disable(moderngl.DEPTH_TEST)
        self.ctx.depth_func = '<'

    def composite(self):
        ctx = self.ctx
        gw,gh=self.glow_width,self.glow_height
        ctx.disable(moderngl.DEPTH_TEST | moderngl.BLEND)
        self.uniform.bind_to_uniform_block(0)
        # Image 0 stands in for the game's frame. The wide chain starts from NarrowA (image 2), as in
        # BombRender, so the game can keep NarrowB and the wide images inside Bright's memory.
        sources = (1, 2, 2, 3, 4, 5, 4, 5)
        destinations = (2, 4, 3, 2, 5, 4, 5, 4)
        for i, (source, destination) in enumerate(zip(sources, destinations)):
            divisor = 2 if i in (0, 2, 3) else 4
            w, h = (gw+divisor-1)//divisor, (gh+divisor-1)//divisor
            downsample = i < 2
            sw, sh = ((gw, gh) if i == 0 else
                      ((gw+1)//2, (gh+1)//2) if i == 1 else (w, h))
            values = np.array([[sw, sh, w, h],
                               [int(i in (2, 4, 6))*gw/self.width, int(i in (3, 5, 7))*gh/self.height, 2 if downsample else 0, 0]], dtype='f4')
            self.filters[destination].use()
            ctx.viewport = (0, 0, w, h)
            self.images[source].use(0)
            self.uniform.write(values.tobytes())
            self.vaos['downsample' if downsample else 'blur'].render(moderngl.TRIANGLES, vertices=3)
        self.output.use()
        ctx.viewport = (0, 0, self.width, self.height)
        self.output.clear(0, 0, 0, 0)
        for unit, image in enumerate((2, 4)):
            self.images[image].use(unit)
        self.uniform.write(np.array([[gw, gh, self.width, self.height], [0, 0, 0, 0]], dtype='f4').tobytes())
        self.vaos['composite'].render(moderngl.TRIANGLES, vertices=3)
        # The game blends the bloom onto its frame with ONE, ONE_MINUS_SRC_ALPHA and alpha zero.
        frame, bloom = self.read(self.filters[0]), self.read(self.output)
        frame[:, :, :3] = np.maximum(frame[:, :, :3]+bloom[:, :, :3], 0)
        return frame

    def read(self, fbo):
        raw = fbo.read(viewport=(0, 0, self.width, self.height), components=4, dtype='f4')
        return np.frombuffer(raw, dtype='f4').reshape(self.height, self.width, 4).copy()


def validate(output: Path, width=257, height=145, native_origin=False, capacity=(512,256)):
    output.mkdir(parents=True, exist_ok=True)
    ctx = moderngl.create_standalone_context(require=450)
    w, h = width, height
    pipeline = Pipeline(ctx, w, h, capacity=capacity, glow_divisor=2, native_origin=native_origin)
    program = ctx.program(vertex_shader=expanded_source(ROOT/'shaders/bomb.vert'),
                          fragment_shader=expanded_source(ROOT/'shaders/bomb.frag'))
    vao = ctx.vertex_array(program, [])
    rows = np.zeros((3420, 4), dtype='f4')
    for i, point in enumerate(((-.7, -.7, 0), (.7, -.7, 0), (0, .7, 0))):
        rows[i*3, :3] = point
        rows[i*3+1, :3] = (0, 0, 1)
        rows[i*3+2] = (1, 0, 0, 1)
    geometry = ctx.buffer(rows.tobytes())
    geometry.bind_to_uniform_block(1)
    uniforms = ctx.buffer(reserve=13*16)
    depth = ctx.texture((w, h), 1, np.ones((h, w), dtype='f4').tobytes(), dtype='f4')
    maps = [ctx.texture((1, 1), 4, np.array(value, dtype='f4').tobytes(), dtype='f4')
            for value in ((.2, .2, .2, 1), (1, 1, 1, 1), (.5, .5, 0, 1), (0, 0, 0, 1))]
    for name, unit in (('sceneDepth', 0), ('originalAlbedo', 1), ('originalEmission', 2),
                       ('originalNormal', 3), ('originalSpecular', 4)):
        program[name].value = unit

    def draw(order, depth_enabled):
        for emission in (False,True):
            pipeline.begin(depth_enabled,emission)
            uniforms.bind_to_uniform_block(0)
            depth.use(0)
            for unit, image in enumerate(maps, 1):
                image.use(unit)
            for index in order:
                values = np.zeros((13, 4), dtype='f4')
                values[0] = (w, h, .1, 100)
                values[12] = (.1, 100, w/pipeline.glow_width if emission else 1, h/pipeline.glow_height if emission else 1)  # private depth range
                values[1:4, :3] = np.eye(3)
                values[1:4, 3] = (0, 0, 3)
                values[4] = (2, 2, 0, 0)
                values[5:8, :3] = np.eye(3)
                values[7, 3] = -.4 if index else 0
                values[8] = (-.4, .8, .3, 0)
                values[9] = (0, 0, 5, 1) if index else (5, 0, 0, 1)
                uniforms.write(values.tobytes())
                vao.render(moderngl.TRIANGLES, vertices=3)
        return pipeline.read(pipeline.mesh)

    front_first = draw((0, 1), True)
    back_first = draw((1, 0), True)
    if not np.array_equal(front_first, back_first):
        raise ValueError('Private mesh depth depends on actor draw order')
    no_depth_a, no_depth_b = draw((0, 1), False), draw((1, 0), False)
    changed = np.count_nonzero(np.max(abs(no_depth_a-no_depth_b), axis=2)>.01)
    if changed < 100:
        raise ValueError('Occlusion fixture fails to distinguish depth-enabled geometry')
    draw((0, 1), True)
    composite = pipeline.composite()
    if not np.isfinite(composite).all() or np.max(composite[:, :, 0]) <= np.max(front_first[:, :, 0]):
        raise ValueError('Actual bloom does not contribute finite HDR radiance')
    alpha_error = float(np.max(abs(composite[:, :, 3]-front_first[:, :, 3])))
    if alpha_error > .0001:
        raise ValueError(f'Bloom corrupts surface coverage: max difference {alpha_error}')
    halo = (front_first[:, :, 3] == 0) & (composite[:, :, 0] > .001)
    if np.count_nonzero(halo) < 100:
        raise ValueError('No bloom outside original silhouette')
    for texture in pipeline.images[1:]:
        poison=np.zeros((texture.height,texture.width,4),dtype='f2')
        poison[:,:,:3]=[24,3,19]
        texture.write(poison.tobytes())
    draw((0,1),True)
    poisoned=pipeline.composite()
    if not np.array_equal(composite,poisoned):
        raise ValueError('Private bloom retains poisoned padding')
    blocked = np.ones((h, w), dtype='f4')
    blocked[:, :w//2] = (2.5-.1)/(100-.1)
    depth.write(blocked.tobytes())
    occluded = draw((0, 1), True)
    if np.count_nonzero(occluded[:, :w//2]) or np.count_nonzero(occluded[:, w//2:]) < 100:
        raise ValueError('Original scene-depth clipping no longer works')
    pipeline.composite()
    pipeline.begin()
    pipeline.begin(emission=True)
    empty = pipeline.composite()
    if np.count_nonzero(empty):
        raise ValueError('Private frame retains a previous bomb or bloom')
    for name, image in (('depth-and-bloom', composite), ('native-scene-occlusion', occluded)):
        rgb = np.maximum(np.flipud(image[:, :, :3]), 0)
        display = (np.clip(rgb/(1+rgb), 0, 1)**(1/2.2)*255).astype('uint8')
        Image.fromarray(display).save(output/f'{name}.png')
    receipt = dict(depth_order_independent=True, control_changed_pixels=int(changed),
                   halo_pixels=int(np.count_nonzero(halo)), scene_occlusion=True,
                   surface_alpha_max_error=alpha_error, poisoned_private_storage_unchanged=True,
                   expired_frame_zero=True, active_size=[w, h], native_origin_sampling=native_origin,
                   glow_size=[pipeline.glow_width,pipeline.glow_height], storage_size=list(capacity),
                   files={p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in output.glob('*.png')})
    (output/'pipeline-receipt.json').write_text(json.dumps(receipt, indent=2))
    print(f'Actual mesh depth/bloom: order independent; control {changed} pixels; halo {np.count_nonzero(halo)} pixels; scene clipping and empty-frame reset pass.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--width',type=int,default=257)
    parser.add_argument('--height',type=int,default=145)
    parser.add_argument('--capacity-width',type=int,default=512)
    parser.add_argument('--capacity-height',type=int,default=256)
    parser.add_argument('--native-origin',action='store_true')
    args=parser.parse_args()
    validate(args.output,args.width,args.height,args.native_origin,(args.capacity_width,args.capacity_height))
