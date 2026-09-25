# SPDX-License-Identifier: MIT
"""Compare the emitted label TGSI with direct bilinear reads of the real baked atlas.

This intentionally supports only the label shader's small instruction subset.
It checks compiler IR semantics, not NVN state, rasterization, or GPU execution.
"""
import argparse
import math
from pathlib import Path
import re
import struct


def operand(text):
    if text in ('SAMP[0]', '2D'):
        return text, 0, 'xyzw'
    match = re.fullmatch(r'(CONST)\[2\]\[ADDR\[0\]\.x\](?:\.([xyzw]+))?', text)
    if match:
        return 'CONST', 0, match[2] or 'xyzw'
    match = re.fullmatch(r'(TEMP|IN|OUT|IMM|ADDR)\[(\d+)\](?:\.([xyzw]+))?', text)
    if not match:
        raise ValueError(f'Unsupported operand: {text}')
    return match[1], int(match[2]), match[3] or 'xyzw'


def signed(value):
    value = int(value) & 0xffffffff
    return value if value < 0x80000000 else value - 0x100000000


def parse(ir):
    immediates = {}
    for index, kind, values in re.findall(r'IMM\[(\d+)\] (\w+) \{([^}]+)\}', ir):
        raw = [int(v.strip(), 0) for v in values.split(',')]
        immediates['IMM', int(index)] = [struct.unpack('<f', struct.pack('<I', v))[0]
                                       for v in raw] if kind == 'FLT32' else raw
    program = []
    for op, args in re.findall(r'^[ \t]*\d+:[ \t]+(\w+)[ \t]*([^\r\n]*)', ir, re.M):
        program.append((op, [operand(s.strip()) for s in args.split(',')] if args else []))
    return immediates, program


def execute(parsed, words, pixel, tile, anchor_depth=-1.0, scene_depth=1.0):
    initial, program = parsed
    registers = dict(initial)
    registers['IN', 0] = [*pixel, 0, 0]
    registers['IN', 1] = list(tile)
    registers['IN', 2] = [0.25,0.75,0,0]
    registers['IN', 3] = [anchor_depth,0,0,0]
    stack = []

    def read(ref):
        bank, index, lanes = ref
        values = words[int(registers['ADDR', 0][0])] if bank == 'CONST' else registers[bank, index]
        return [values['xyzw'.index(c)] for c in lanes]

    for op, args in program:
        if op == 'ENDIF':
            stack.pop()
            continue
        if op == 'UIF':
            stack.append(all(stack) and bool(read(args[0])[0]))
            continue
        if not all(stack):
            continue
        if op == 'END':
            break
        dest, *sources = args
        if op == 'TEX_LZ':
            assert sources[1][0] == 'SAMP[0]' and sources[2][0] == '2D'
            assert read(sources[0])[:2] == [0.25,0.75]
            registers[dest[0],dest[1]] = [scene_depth]*4
            continue
        vectors = [read(ref) for ref in sources]
        result = []
        for lane in range(4):
            a, b, c = [(v[lane] if len(v) == 4 else v[0]) for v in vectors] + [0] * (3-len(vectors))
            if op in ('MOV', 'UARL'): value = a
            elif op == 'MOV_SAT':
                raw = struct.unpack('<f', struct.pack('<I', a & 0xffffffff))[0] if isinstance(a, int) else a
                value = min(1., max(0., raw))
            elif op == 'FLR': value = float(math.floor(a))
            elif op == 'FRC': value = a-math.floor(a)
            elif op == 'F2I': value = int(a)
            elif op == 'U2F': value = float(int(a) & 0xffffffff)
            elif op == 'ISLT': value = 0xffffffff if signed(a) < signed(b) else 0
            elif op == 'ISGE': value = 0xffffffff if signed(a) >= signed(b) else 0
            elif op == 'FSGE': value = 0xffffffff if a >= b else 0
            elif op == 'FSLT': value = 0xffffffff if a < b else 0
            elif op == 'USEQ': value = 0xffffffff if a == b else 0
            elif op == 'UADD': value = (int(a)+int(b)) & 0xffffffff
            elif op == 'UMUL': value = (int(a)*int(b)) & 0xffffffff
            elif op == 'UMAD': value = (int(a)*int(b)+int(c)) & 0xffffffff
            elif op == 'UDIV': value = (int(a) & 0xffffffff)//int(b)
            elif op == 'UMOD': value = (int(a) & 0xffffffff)%int(b)
            elif op == 'USHR': value = (int(a) & 0xffffffff) >> int(b)
            elif op == 'AND': value = int(a) & int(b)
            elif op == 'MUL': value = a*b
            elif op == 'LRP': value = a*b+(1-a)*c
            else: raise ValueError(f'Unsupported instruction: {op}')
            result.append(value)
        bank, index, lanes = dest
        previous = registers.setdefault((bank, index), [0]*4)
        for component in lanes:
            lane = 'xyzw'.index(component)
            previous[lane] = result[lane]
    return registers['OUT', 0]


def check(ir, pixels):
    if len(pixels) != 54272:
        raise ValueError('Unexpected atlas length')
    parsed = parse(ir)
    words = [struct.unpack_from('<4I', pixels, i) for i in range(0, len(pixels), 16)]
    cases = 0
    for tile in range(125):
        size = 16 if tile < 96 else 32
        offset = tile*256 if tile < 96 else 96*256+(tile-96)*1024
        color = 0xc3812fa7
        coordinates = (-.25, .5, 3.75, 4.25, size-1.25, size-.25, size+.25)
        for y in coordinates:
            for x in coordinates:
                px, py = math.floor(x), math.floor(y)
                fx, fy = x-px, y-py
                def sample(dx, dy):
                    sx, sy = max(0, min(size-1, px+dx)), max(0, min(size-1, py+dy))
                    return pixels[offset+sy*size+sx]/255
                alpha = ((1-fx)*sample(0, 0)+fx*sample(1, 0))*(1-fy)
                alpha += ((1-fx)*sample(0, 1)+fx*sample(1, 1))*fy
                expected = [((color >> (8*i)) & 255)/255 for i in range(4)]
                expected[3] *= alpha
                actual = execute(parsed, words, (x, y), (offset, size, size, color))
                if any(abs(a-b) > 0.000002 for a, b in zip(actual, expected)):
                    raise AssertionError(f'tile={tile} pixel=({x},{y}): IR={actual}, expected={expected}')
                cases += 1
    # Opaque synthetic pixels isolate depth comparison from glyph coverage.
    opaque=[(0xffffffff,)*4]*3392
    for anchor in (-1.0,0.0,0.3,0.8):
        for depth in (-1.0,0.0,0.2,0.3,0.6,0.9,1.0,float('nan'),float('inf')):
            actual=execute(parsed,opaque,(4.25,3.75),(0,16,16,0xffffffff),anchor,depth)
            # World depth must no longer affect the draw-on-glass labels.
            expected=[1,1,1,1]
            assert all(abs(a-b)<0.000002 for a,b in zip(actual,expected)),(anchor,depth,actual)
            cases+=1
    return cases


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--ir', type=Path, required=True)
    parser.add_argument('--atlas', type=Path, required=True)
    args = parser.parse_args()
    data = args.atlas.read_text(encoding='utf-8').split('kPixels[kPixelPoolBytes] = {', 1)[1].split('};', 1)[0]
    pixels = bytes(map(int, re.findall(r'\d+', data)))
    assert len(pixels) in (54272,162816)
    count = sum(check(args.ir.read_text(encoding='utf-8'),pixels[i:i+54272]) for i in range(0,len(pixels),54272))
    print(f'Label IR checks passed: {count} pixel/depth cases across {len(pixels)//54272} font banks')
