# SPDX-License-Identifier: MIT
# Copyright (c) Clay Mullis
"""Bake owned BotW Gale clips after checking the stock TotK skeleton matches."""
from __future__ import annotations

import argparse
import bisect
import hashlib
import json
import math
import struct
import subprocess
import zlib
from pathlib import Path

CLIPS = ('Nml_Wait_To_Squat_Wait', 'Squat_Wait_FootL', 'Squat_Wait_FootR')
VERSION = 2
STEP = 0.25
HEADER = struct.Struct('<4sHHHHfI')
BONE = struct.Struct('<32shH')
CLIP = struct.Struct('<32sHHI')
TRANSFORM = struct.Struct('<7f')
ROTATION = struct.Struct('<4h')
ANIMATED_POSITION, ANIMATED_ROTATION = 1, 2


def vector(value: str) -> tuple[float, ...]:
    values = tuple(float(v) for v in value.split(';'))
    if len(values) != 3 or not all(math.isfinite(v) for v in values):
        raise ValueError('Expected finite XYZ vector')
    return values


def curve_offset(curve: dict) -> float:
    # The parser exposes a float curve's offset as its raw 32-bit pattern.
    offset = curve['Offset']
    if not isinstance(offset, int) or not -2**31 <= offset < 2**31:
        raise ValueError('Expected a raw 32-bit curve offset')
    return struct.unpack('<f', struct.pack('<i', offset))[0]


def curve_value(curve: dict, frame: float) -> float:
    times, keys = curve['Frames'], curve['Keys']
    if curve['CurveType'] != 0 or not times or len(times) != len(keys):
        raise ValueError('Expected cubic keys')
    if any(b <= a for a, b in zip(times, times[1:])):
        raise ValueError('Curve times must increase')
    if any(len(key) != 4 for key in keys):
        raise ValueError('Expected four polynomial coefficients')
    index = max(0, bisect.bisect_right(times, frame) - 1)
    t = 0.0 if index == len(times) - 1 else max(0.0, (frame - times[index]) / (times[index + 1] - times[index]))
    a, b, c, d = keys[index]
    result = (a + t * (b + t * (c + t * d))) * curve['Scale'] + curve_offset(curve)
    if not math.isfinite(result):
        raise ValueError('Nonfinite curve value')
    return result


def quaternion(xyz: tuple[float, ...]) -> tuple[float, ...]:
    x, y, z = (v / 2 for v in xyz)
    sx, sy, sz = math.sin(x), math.sin(y), math.sin(z)
    cx, cy, cz = math.cos(x), math.cos(y), math.cos(z)
    return (sx * cy * cz - cx * sy * sz, cx * sy * cz + sx * cy * sz,
            cx * cy * sz - sx * sy * cz, cx * cy * cz + sx * sy * sz)


def local_transform(bone: dict, track: dict | None, frame: float) -> tuple[float, ...]:
    fields = {'Scale': vector(bone['Scale']), 'Rotate': vector(bone['Rotation']),
              'Translate': vector(bone['Position'])}
    if track:
        for key, flag in (('Scale', 8), ('Rotate', 16), ('Translate', 32)):
            if track['FlagsBase'] & flag:
                fields[key] = vector(track['BaseData'][key])
        for curve in track['Curves']:
            offset = curve['AnimDataOffset']
            group = next(((name, base) for name, base in (('Scale', 4), ('Translate', 16), ('Rotate', 32))
                          if offset in (base, base + 4, base + 8)), None)
            if group is None:
                raise ValueError(f'Unsupported bone channel {offset}')
            key, base = group
            values = list(fields[key])
            values[(offset - base) // 4] = curve_value(curve, frame)
            fields[key] = tuple(values)
    if any(abs(v - 1.0) > 1e-6 for v in fields['Scale']):
        raise ValueError(f"Scaled bone {bone['Name']} needs a different pose format")
    return (*fields['Translate'], *quaternion(fields['Rotate']))


def verify_skeleton(source: dict, target: dict) -> list[dict]:
    bones = source['Bones']
    if not bones or len(bones) != len(target['Bones']) or len(bones) > 128:
        raise ValueError('Skeleton bone count changed')
    if len({b['Name'] for b in bones}) != len(bones):
        raise ValueError('Duplicate bone name')
    for index, (a, b) in enumerate(zip(bones, target['Bones'], strict=True)):
        for field in ('Name', 'ParentIndex', 'FlagsRotation'):
            if a[field] != b[field]:
                raise ValueError(f"Skeleton differs at {a['Name']}: {field}")
        if not -1 <= a['ParentIndex'] < index or (index > 0 and a['ParentIndex'] == -1):
            raise ValueError('Skeleton is not one parent-first tree')
        if a['FlagsRotation'] != 4096:
            raise ValueError('Expected Euler XYZ skeleton')
        for field in ('Scale', 'Rotation', 'Position'):
            if vector(a[field]) != vector(b[field]):
                raise ValueError(f"Skeleton differs at {a['Name']}: {field}")
    return bones


def name_bytes(name: str) -> bytes:
    raw = name.encode('ascii')
    if len(raw) >= 32 or b'\0' in raw:
        raise ValueError(f'Unsupported pose name {name!r}')
    return raw


def quantize(value: float) -> int:
    return max(-32767, min(32767, round(value * 32767)))


def rotation_error(rotation: bytes) -> float:
    exact = struct.unpack('<4f', rotation)
    stored = [quantize(v) / 32767 for v in exact]
    norm = math.sqrt(sum(v * v for v in stored) * sum(v * v for v in exact))
    dot = abs(sum(a * b for a, b in zip(exact, stored))) / norm
    return 2 * math.degrees(math.acos(min(1.0, dot)))


def pack_samples(bones: list[dict], rows: list[list[bytes]]) -> tuple[bytes, bytes, bytes, float]:
    """Store unchanging channels once and animated rotations as 16-bit components."""
    flags = [(ANIMATED_POSITION if any(row[b][:12] != rows[0][b][:12] for row in rows) else 0) |
             (ANIMATED_ROTATION if any(row[b][12:] != rows[0][b][12:] for row in rows) else 0)
             for b in range(len(bones))]
    bone_data = b''.join(BONE.pack(name_bytes(b['Name']), b['ParentIndex'], f) for b, f in zip(bones, flags))
    payload, error = bytearray(), 0.0
    for row in rows:
        for transform, flag in zip(row, flags):
            if flag & ANIMATED_POSITION:
                payload.extend(transform[:12])
            if flag & ANIMATED_ROTATION:
                payload.extend(ROTATION.pack(*(quantize(v) for v in struct.unpack('<4f', transform[12:]))))
                error = max(error, rotation_error(transform[12:]))
    return bone_data, b''.join(rows[0]), bytes(payload), error


def bake(botw: dict, totk: dict) -> tuple[bytes, dict]:
    bones = verify_skeleton(botw['Skeleton'], totk['Skeleton'])
    clips = {clip['Name']: clip for clip in botw['Clips']}
    if set(clips) != set(CLIPS) or len(botw['Clips']) != len(CLIPS):
        raise ValueError('Expected all three Gale clips exactly once')
    clip_data, rows, details = bytearray(), [], []
    names = {b['Name'] for b in bones}
    first_sample = 0
    for name in CLIPS:
        clip = clips[name]
        if clip['FlagsScale'] != 512 or clip['FlagsRotate'] != 4096 or clip['Baked']:
            raise ValueError(f'Unsupported animation format: {name}')
        tracks = {track['Name']: track for track in clip['BoneAnims']}
        if len(tracks) != len(clip['BoneAnims']) or tracks.keys() - names:
            raise ValueError('Duplicate or unbound animation bone')
        frames = clip['FrameCount']
        if not isinstance(frames, int) or not 0 < frames < 512:
            raise ValueError('Invalid clip duration')
        if clip['Loop'] != (name != CLIPS[0]):
            raise ValueError('Unexpected loop mode')
        samples = int(frames / STEP) + 1
        clip_data.extend(CLIP.pack(name_bytes(name), samples, int(clip['Loop']), first_sample))
        for sample in range(samples):
            rows.append([TRANSFORM.pack(*local_transform(bone, tracks.get(bone['Name']), sample * STEP))
                         for bone in bones])
        details.append({'name': name, 'frames': frames, 'samples': samples, 'animated_bones': len(tracks)})
        first_sample += samples
    bone_data, rest, poses, error = pack_samples(bones, rows)
    body = bone_data + clip_data + rest + poses
    data = HEADER.pack(b'CBGP', VERSION, len(bones), len(CLIPS), TRANSFORM.size, STEP, zlib.crc32(body)) + body
    flags = [BONE.unpack_from(bone_data, i * BONE.size)[2] for i in range(len(bones))]
    return data, {'bones': len(bones), 'clips': details, 'sample_step_frames': STEP,
                  'animated_positions': sum(bool(f & ANIMATED_POSITION) for f in flags),
                  'animated_rotations': sum(bool(f & ANIMATED_ROTATION) for f in flags),
                  'rotation_encoding': 'int16 components / 32767', 'max_rotation_error_degrees': error,
                  'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}


def export(toolbox: Path, source: Path, output: Path) -> dict:
    subprocess.run(['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
                    str(Path(__file__).with_name('export_champion_pose.ps1')),
                    '-ToolboxDir', str(toolbox), '-InputBfres', str(source), '-OutputJson', str(output)],
                   check=True, stdout=subprocess.DEVNULL)
    return json.loads(output.read_text(encoding='utf-8-sig'))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--botw-romfs', type=Path, required=True)
    parser.add_argument('--totk-romfs', type=Path, required=True)
    parser.add_argument('--toolbox', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--receipt', type=Path)
    args = parser.parse_args()
    import oead
    import zstandard

    args.work.mkdir(parents=True, exist_ok=True)
    sources = (args.botw_romfs / 'Model/Player_Animation.sbfres',
               args.totk_romfs / 'Model/Player_Animation.Skeleton.bfres.mc')
    original = [p.read_bytes() for p in sources]
    if original[0][:4] != b'Yaz0' or original[1][:8] != b'MCPK\x01\x01\0\0':
        raise ValueError('Unexpected animation containers')
    decoded = (bytes(oead.yaz0.decompress(original[0])),
               zstandard.ZstdDecompressor().decompress(b'\x28\xb5\x2f\xfd' + original[1][12:], max_output_size=1024*1024))
    documents = []
    for label, raw in zip(('botw', 'totk'), decoded, strict=True):
        if raw[:4] != b'FRES':
            raise ValueError('Expected decoded BFRES')
        source = args.work / f'{label}.bfres'
        source.write_bytes(raw)
        documents.append(export(args.toolbox.resolve(), source.resolve(), (args.work / f'{label}.json').resolve()))
    data, receipt = bake(*documents)
    receipt['sources'] = [{'name': p.name, 'sha256': hashlib.sha256(raw).hexdigest()}
                          for p, raw in zip(sources, original, strict=True)]
    parser_dll = args.toolbox / 'Lib/Syroot.NintenTools.NSW.Bfres.dll'
    receipt['parser_sha256'] = hashlib.sha256(parser_dll.read_bytes()).hexdigest()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(data)
    receipt_path = args.receipt or args.output.with_suffix('.receipt.json')
    receipt_path.parent.mkdir(parents=True, exist_ok=True)
    receipt_path.write_text(json.dumps(receipt, indent=2) + '\n')
    generated = receipt_path.parent / 'generated'
    generated.mkdir(parents=True, exist_ok=True)
    (generated / 'GalePoseInfo.hpp').write_text(
        '// SPDX-License-Identifier: MIT\n// Copyright (c) Clay Mullis\n#pragma once\n'
        '#include <cstddef>\n#include <cstdint>\nnamespace botw_abilities::champions {\n'
        f'inline constexpr std::size_t kGalePoseBytes = {len(data)};\n'
        f'inline constexpr std::uint32_t kGalePoseChecksum = 0x{zlib.crc32(data[HEADER.size:]):08X};\n'
        '}\n')
    print(json.dumps(receipt))


if __name__ == '__main__':
    main()
