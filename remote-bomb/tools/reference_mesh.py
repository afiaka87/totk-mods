# SPDX-License-Identifier: MIT
# Copyright (c) Clay Mullis
"""Read privately extracted original geometry; keep body and grip draw ranges separate."""
from __future__ import annotations
from dataclasses import dataclass
import base64
import json
import struct
from pathlib import Path
import numpy as np

@dataclass(frozen=True)
class Draw:
    first: int
    count: int
    material: int

def parameter(material, name):
    param = next(item for item in material['ShaderParams'] if item['Name'] == name)
    blob = base64.b64decode(material['ShaderParamData'])
    return struct.unpack_from('<' + 'f' * (param['DataSize'] // 4), blob, param['DataOffset'])

def load_mesh(source: Path):
    data = json.loads((source / 'mesh.json').read_text(encoding='utf-8-sig'))
    if len(data['models']) != 1:
        raise ValueError('Expected the original bomb model')
    model = data['models'][0]
    bones = model['skeleton']['Bones']
    if len(bones) != 1 or not bones[0]['TransformIdentity']:
        raise ValueError('A nonidentity skeleton needs an explicit bone transform')
    rows, indices, draws = [], [], []
    for shape in model['shapes']:
        base = len(rows) // 3
        vertices = shape['vertices']
        first = len(indices)
        if shape['bone'] != 0 or len(shape['indices']) % 3:
            raise ValueError('Unexpected original mesh topology')
        for vertex in vertices:
            normal, tangent, bitangent = (np.asarray(vertex[k], dtype=np.float32)[:3]
                                         for k in ('nrm', 'tan', 'bitan'))
            handedness = -1.0 if np.dot(np.cross(normal, tangent), bitangent) < 0 else 1.0
            rows.extend((vertex['pos'] + [vertex['uv'][0]],
                         vertex['nrm'] + [vertex['uv'][1]],
                         vertex['tan'][:3] + [handedness]))
        if max(shape['indices'], default=0) >= len(vertices):
            raise ValueError('Original index escapes its shape')
        indices.extend(base + index for index in shape['indices'])
        draws.append(Draw(first, len(shape['indices']), shape['material']))
    if len(rows) > 3420 or max(indices, default=0) >= 65536:
        raise ValueError('Original mesh exceeds the verified uniform/index capacity')
    return np.asarray(rows, dtype='f4'), np.asarray(indices, dtype='u2'), draws, model
