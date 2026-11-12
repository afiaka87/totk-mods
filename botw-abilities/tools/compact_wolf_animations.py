# SPDX-License-Identifier: MIT
# Copyright (c) HanSoloBlaster, afiaka87
"""Fit Wolf Link's clips with fewer keys and join the one-shot clips into shared banks.

Every clip costs a fixed block of bookkeeping (one record per bone plus one per curve), which is as
large as the keys themselves. A bank is one long clip holding many one-shots end to end; the animation
graph plays a frame range of it, as stock animals do. Looping clips stay separate because no stock
graph loops a sub-range. Clips named with --standalone also stay separate: the stock sound user keys the
howl, roar and death voices on the original clip name or on frames counted from 0, so those clips keep
their name and start at frame 0 instead of sitting inside a bank.
"""
import argparse
import json
import math
from pathlib import Path
import struct

import numpy as np

SINGLE, DECIMAL, BYTE = 0, 1, 2
INT16, SBYTE = 4, 8
TRANSLATE = {16: 0, 20: 1, 24: 2}
ROTATE = {32: 0, 36: 1, 40: 2}
SCALE = {4: 0, 8: 1, 12: 2}
CURVE_BIT = {4: 6, 8: 7, 12: 8, 32: 9, 36: 10, 40: 11, 16: 13, 20: 14, 24: 15}
CHANNELS = (*SCALE, *TRANSLATE, *ROTATE)
BASE_SCALE, BASE_ROTATE, BASE_TRANSLATE = 8, 16, 32
BANK_FRAMES = 1000  # The two-byte frame format ends at 1023.
BANK_GAP = 16       # Unkeyed hold after each clip, so a late fade never shows the next clip.


def as_float(bits):
    return struct.unpack('<f', struct.pack('<I', bits & 0xFFFFFFFF))[0]


def single(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


def vector(text):
    return [float(part) for part in text.split(';')]


def sample(curve, count):
    """Evaluate a stored curve at every whole frame, as the engine's cubic evaluator does."""
    frames = np.array(curve['Frames'], dtype=np.float64)
    keys = np.array(curve['Keys'], dtype=np.float64) * curve['Scale']
    keys[:, 0] += as_float(curve['Offset'])
    times = np.arange(count, dtype=np.float64)
    index = np.clip(np.searchsorted(frames, times, side='right') - 1, 0, len(frames) - 1)
    following = np.minimum(index + 1, len(frames) - 1)
    span = frames[following] - frames[index]
    u = np.divide(times - frames[index], span, out=np.zeros_like(times), where=span > 0)
    u = np.clip(u, 0.0, 1.0)
    c = keys[index]
    return c[:, 0] + u * (c[:, 1] + u * (c[:, 2] + u * c[:, 3]))


def hermite(y0, y1, m0, m1, span):
    return y0, m0 * span, 3 * (y1 - y0) - span * (2 * m0 + m1), 2 * (y0 - y1) + span * (m0 + m1)


def fit(values, tolerance):
    """Choose keys greedily: each cubic reaches as far as it stays inside the tolerance."""
    last = len(values) - 1
    slopes = np.gradient(values) if last >= 2 else np.zeros(len(values))
    keys, start = [0], 0
    while start < last:
        best = start + 1
        for end in range(start + 1, last + 1):
            span = end - start
            c = hermite(values[start], values[end], slopes[start], slopes[end], span)
            u = np.arange(span + 1) / span
            fitted = c[0] + u * (c[1] + u * (c[2] + u * c[3]))
            if np.max(np.abs(fitted - values[start:end + 1])) <= tolerance:
                best = end
        keys.append(best)
        start = best
    return keys, slopes


def segment_rows(values, tolerance):
    """Cubic rows for one clip's channel; the last row holds its value through any following gap."""
    if float(values.max() - values.min()) <= tolerance * 2:
        return [0], [(float((values.max() + values.min()) / 2), 0.0, 0.0, 0.0)]
    frames, slopes = fit(values, tolerance * 0.7)
    rows = [hermite(values[a], values[b], slopes[a], slopes[b], b - a) for a, b in zip(frames, frames[1:])]
    rows.append((values[frames[-1]], 0.0, 0.0, 0.0))
    return frames, rows


def quantise(rows, tolerance):
    """Store rows in the smallest integer format that keeps them accurate."""
    rows = np.array(rows, dtype=np.float64)
    offset = float((rows[:, 0].max() + rows[:, 0].min()) / 2)
    rows[:, 0] -= offset
    peak = float(np.abs(rows).max()) or 1.0
    for kind, limit in ((SBYTE, 127), (INT16, 32767)):
        scale = peak / limit
        # The worst case adds all four rounding errors at the end of a segment.
        if kind == INT16 or scale * 2 <= tolerance * 0.5:
            break
    scale = single(scale)
    offset = single(offset)
    return dict(key_type=kind, scale=scale, offset=offset, keys=np.round(rows / scale).astype(int).tolist())


def frame_format(last):
    return BYTE if last <= 255 else DECIMAL


def evaluate(stored, frames, count):
    bits = struct.unpack('<I', struct.pack('<f', stored['offset']))[0]
    return sample(dict(Frames=[float(f) for f in frames], Keys=stored['keys'], Scale=stored['scale'], Offset=bits), count)


def curve_record(channel, frames, stored, values):
    return dict(channel=channel, frames=[int(f) for f in frames], frame_type=frame_format(frames[-1]),
                delta=float(values[-1] - values[0]), **stored)


def reach(bones):
    """Distance from each joint to the furthest joint it moves."""
    world = []
    for bone in bones:
        parent = bone['parent']
        world.append(np.array(bone['p']) if parent < 0 else world[parent] + np.array(bone['p']))
    lengths = [0.08] * len(bones)
    for index, bone in enumerate(bones):
        parent = bone['parent']
        while parent >= 0:
            lengths[parent] = max(lengths[parent], float(np.linalg.norm(world[index] - world[parent])) + 0.08)
            parent = bones[parent]['parent']
    return lengths


def rotation(angles):
    x, y, z = angles
    cx, sx, cy, sy, cz, sz = math.cos(x), math.sin(x), math.cos(y), math.sin(y), math.cos(z), math.sin(z)
    return (np.array([[cz, -sz, 0], [sz, cz, 0], [0, 0, 1]]) @
            np.array([[cy, 0, sy], [0, 1, 0], [-sy, 0, cy]]) @
            np.array([[1, 0, 0], [0, cx, -sx], [0, sx, cx]]))


def world_positions(tracks, parents):
    """Forward kinematics for one clip; tracks[bone] holds per-frame translate, rotate and scale."""
    frames = next(iter(tracks.values()))[0].shape[0]
    result = np.zeros((frames, len(parents), 3))
    for frame in range(frames):
        matrices = []
        for index, parent in enumerate(parents):
            translate, rotate, scale = tracks[index]
            local = np.eye(4)
            local[:3, :3] = rotation(rotate[frame]) * scale[frame]
            local[:3, 3] = translate[frame]
            matrices.append(local if parent < 0 else matrices[parent] @ local)
            result[frame, index] = matrices[-1][:3, 3]
    return result


def base_value(bone, channel, rest):
    """A channel's held value; a bone without a stored translation keeps its skeleton position."""
    base = bone['BaseData']
    if channel in TRANSLATE:
        source = vector(base['Translate']) if bone['FlagsBase'] & BASE_TRANSLATE else rest
        return source[TRANSLATE[channel]]
    if channel in ROTATE:
        return vector(base['Rotate'])[ROTATE[channel]]
    return vector(base['Scale'])[SCALE[channel]]


def bone_tracks(clip, count, curves, rests):
    tracks = {}
    for index, bone in enumerate(clip['BoneAnims']):
        channels = {channel: np.full(count, base_value(bone, channel, rests[index])) for channel in CHANNELS}
        channels.update(curves[index])
        tracks[index] = (np.stack([channels[c] for c in TRANSLATE], axis=1),
                         np.stack([channels[c] for c in ROTATE], axis=1),
                         np.stack([channels[c] for c in SCALE], axis=1))
    return tracks


def channel_tolerance(channel, index, lengths, world_tolerance, translate_tolerance):
    return translate_tolerance if channel in TRANSLATE else world_tolerance / lengths[index]


def joint_error(clip, count, before, after, parents, rests):
    error = world_positions(bone_tracks(clip, count, before, rests), parents) - \
        world_positions(bone_tracks(clip, count, after, rests), parents)
    return float(np.linalg.norm(error, axis=2).max())


def compact_clip(clip, lengths, parents, rests, world_tolerance, translate_tolerance):
    count = int(clip['FrameCount']) + 1
    before, after, output = {}, {}, []
    for index, bone in enumerate(clip['BoneAnims']):
        before[index], after[index] = {}, {}
        record = dict(curves=[], flags=bone['FlagsBase'],
                      constants={channel: base_value(bone, channel, rests[index]) for channel in CHANNELS})
        for curve in bone['Curves']:
            channel = curve['AnimDataOffset']
            values = sample(curve, count)
            before[index][channel] = values
            tolerance = channel_tolerance(channel, index, lengths, world_tolerance, translate_tolerance)
            frames, rows = segment_rows(values, tolerance)
            if len(frames) == 1:
                record['constants'][channel] = rows[0][0]
                after[index][channel] = np.full(count, rows[0][0])
                continue
            stored = quantise(rows, tolerance * 0.3)
            after[index][channel] = evaluate(stored, frames, count)
            record['curves'].append(curve_record(channel, frames, stored, values))
        output.append(record)
    return dict(name=clip['Name'], template=clip['Name'], frames=int(clip['FrameCount']), bones=output,
                keep_user_data=True, errors={clip['Name']: joint_error(clip, count, before, after, parents, rests)})


def layout(clips, members):
    """Place clips, longest first, into as few banks as the frame format allows."""
    banks = []
    for name in sorted(members, key=lambda item: -clips[item]['FrameCount']):
        length = int(clips[name]['FrameCount'])
        for bank in banks:
            if bank['next'] + length <= BANK_FRAMES:
                break
        else:
            bank = dict(next=0, members={})
            banks.append(bank)
        bank['members'][name] = dict(base=bank['next'], length=length)
        bank['next'] += length + BANK_GAP
    return banks


def compact_bank(name, placed, clips, lengths, parents, rests, world_tolerance, translate_tolerance):
    template = clips[next(iter(placed))]
    total = max(item['base'] + item['length'] for item in placed.values())
    source = {member: {index: {curve['AnimDataOffset']: sample(curve, int(clips[member]['FrameCount']) + 1)
                               for curve in bone['Curves']}
                       for index, bone in enumerate(clips[member]['BoneAnims'])} for member in placed}
    output, after = [], {member: {index: {} for index in range(len(template['BoneAnims']))} for member in placed}
    for index in range(len(template['BoneAnims'])):
        record = dict(curves=[], constants={}, flags=BASE_SCALE | BASE_ROTATE)
        for channel in CHANNELS:
            tolerance = channel_tolerance(channel, index, lengths, world_tolerance, translate_tolerance)
            series = {}
            for member, item in placed.items():
                bone = clips[member]['BoneAnims'][index]
                held = base_value(bone, channel, rests[index])
                series[member] = source[member][index].get(channel, np.full(item['length'] + 1, held))
            low = min(float(values.min()) for values in series.values())
            high = max(float(values.max()) for values in series.values())
            if high - low <= tolerance * 2:
                constant = (high + low) / 2
                record['constants'][channel] = constant
                for member, item in placed.items():
                    after[member][index][channel] = np.full(item['length'] + 1, constant)
                continue
            frames, rows = [], []
            for member, item in sorted(placed.items(), key=lambda pair: pair[1]['base']):
                segment, cubic = segment_rows(series[member], tolerance)
                frames.extend(item['base'] + frame for frame in segment)
                rows.extend(cubic)
            stored = quantise(rows, tolerance * 0.3)
            values = evaluate(stored, frames, total + 1)
            for member, item in placed.items():
                after[member][index][channel] = values[item['base']:item['base'] + item['length'] + 1]
            ordered = np.concatenate([series[m] for m, _ in sorted(placed.items(), key=lambda pair: pair[1]['base'])])
            record['curves'].append(curve_record(channel, frames, stored, ordered))
        output.append(record)
    errors = {member: joint_error(clips[member], item['length'] + 1, source[member], after[member], parents, rests)
              for member, item in placed.items()}
    return dict(name=name, template=template['Name'], frames=total, bones=output, keep_user_data=False, errors=errors)


def write_plan(path, records, rests):
    """Line records for the bfres writer: one clip line, then bones with their curves."""
    lines = []
    for record in records:
        keys = sum(len(curve['frames']) for bone in record['bones'] for curve in bone['curves'])
        lines.append('\t'.join(('clip', record['name'], record['template'], str(record['frames']),
                                str(keys * 16), str(int(record['keep_user_data'])))))
        for index, bone in enumerate(record['bones']):
            curve_flags = sum(1 << CURVE_BIT[curve['channel']] for curve in bone['curves'])
            constants = bone['constants']
            translate = [constants.get(channel, rests[index][axis]) for channel, axis in TRANSLATE.items()]
            scale = [constants.get(channel, 1.0) for channel in SCALE]
            moved = any(abs(value - rests[index][axis]) > 1e-6 for axis, value in enumerate(translate))
            flags = bone['flags'] | (BASE_TRANSLATE if moved else 0)
            lines.append('\t'.join(['bone', str(index), str(curve_flags), str(flags)] +
                                   [repr(float(value)) for value in translate] +
                                   [repr(float(constants.get(channel, 0.0))) for channel in ROTATE] +
                                   [repr(float(value)) for value in scale]))
            for curve in bone['curves']:
                lines.append('\t'.join(('curve', str(curve['channel']), str(curve['frame_type']), str(curve['key_type']),
                                        repr(curve['scale']), repr(curve['offset']), repr(curve['delta']),
                                        ','.join(map(str, curve['frames'])),
                                        ','.join(str(value) for row in curve['keys'] for value in row))))
    path.write_text('\n'.join(lines) + '\n', encoding='utf-8')


def key_bytes(records):
    keys = curves = data = 0
    for record in records:
        for bone in record['bones']:
            for curve in bone['curves']:
                count = len(curve['frames'])
                keys += count
                curves += 1
                data += count * ((1 if curve['frame_type'] == BYTE else 2) + (4 if curve['key_type'] == SBYTE else 8))
    return keys, curves, data


def check_flag_layout(clips):
    for clip in clips.values():
        for bone in clip['BoneAnims']:
            if sum(1 << CURVE_BIT[curve['AnimDataOffset']] for curve in bone['Curves']) != bone['FlagsCurve']:
                raise ValueError(f'Curve flags differ from the known layout: {clip["Name"]}/{bone["Name"]}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--clips', type=Path, required=True, help='JSON dump of the skeletal clips')
    parser.add_argument('--skeleton', type=Path, required=True, help='JSON dump of the skeleton')
    parser.add_argument('--keep', type=Path, required=True, help='JSON list of clip names to retain')
    parser.add_argument('--standalone', action='append', default=[],
                        help='One-shot clip to store under its own name, outside any bank (repeatable)')
    parser.add_argument('--tolerance', type=float, default=0.015, help='Largest joint movement, in metres')
    parser.add_argument('--translate-tolerance', type=float, default=0.004)
    parser.add_argument('--output', type=Path, required=True, help='Directory for fit.txt and fit.json')
    args = parser.parse_args()
    clips = {clip['Name']: clip for clip in json.loads(args.clips.read_text(encoding='utf-8-sig'))}
    check_flag_layout(clips)
    bones = json.loads(args.skeleton.read_text(encoding='utf-8-sig'))
    lengths, parents, rests = reach(bones), [bone['parent'] for bone in bones], [bone['p'] for bone in bones]
    if any([bone['Name'] for bone in clip['BoneAnims']] != [bone['name'] for bone in bones] for clip in clips.values()):
        raise ValueError('Clip bone order differs from the skeleton')
    keep = json.loads(args.keep.read_text(encoding='utf-8'))
    loops = [name for name in keep if clips[name]['Loop']]
    standalone = [name for name in keep if name in args.standalone and not clips[name]['Loop']]
    if set(args.standalone) - set(standalone):
        raise ValueError(f'Standalone clips must be kept one-shots: {sorted(set(args.standalone) - set(standalone))}')
    banks = layout(clips, [name for name in keep if not clips[name]['Loop'] and name not in standalone])
    records = [compact_clip(clips[name], lengths, parents, rests, args.tolerance, args.translate_tolerance)
               for name in loops + standalone]
    placements = {}
    for number, bank in enumerate(banks):
        name = f'Bank_{number}'
        records.append(compact_bank(name, bank['members'], clips, lengths, parents, rests,
                                    args.tolerance, args.translate_tolerance))
        placements.update({member: dict(bank=name, **item) for member, item in bank['members'].items()})
    args.output.mkdir(parents=True, exist_ok=True)
    write_plan(args.output / 'fit.txt', records, rests)
    errors = {clip: value for record in records for clip, value in record['errors'].items()}
    summary = dict(tolerance=args.tolerance, translate_tolerance=args.translate_tolerance, placements=placements,
                   loops=loops, standalone=standalone, errors=errors)
    (args.output / 'fit.json').write_text(json.dumps(summary, indent=1) + '\n', encoding='utf-8')
    keys, curves, data = key_bytes(records)
    worst = max(errors, key=errors.get)
    print(f'clips {len(records)} ({len(loops)} loops, {len(standalone)} standalone, {len(banks)} banks) '
          f'curves {curves} keys {keys} key bytes {data} '
          f'worst joint error {errors[worst]:.4f} m in {worst}')


if __name__ == '__main__':
    main()
