# SPDX-License-Identifier: MIT
# Copyright (c) Clay Mullis
import copy
import importlib.util
import math
import struct
import unittest
import zlib
from pathlib import Path

spec = importlib.util.spec_from_file_location('pose_builder', Path(__file__).parents[1] / 'tools/build_champion_pose.py')
pose = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pose)


def bone(name='Root', parent=-1):
    return {'Name': name, 'ParentIndex': parent, 'FlagsRotation': 4096,
            'Scale': '1;1;1', 'Rotation': '0;0;0', 'Position': '0;0;0'}


def curve():
    return {'CurveType': 0, 'Frames': [2.0, 6.0], 'Keys': [[1, 0, 3, -2], [2, 0, 0, 0]],
            'Scale': 2.0, 'Offset': raw(-1.0), 'AnimDataOffset': 16}


def raw(value):
    return struct.unpack('<i', struct.pack('<f', value))[0]


def source():
    return {'Skeleton': {'Bones': [bone(), bone('Hand', 0)]},
            'Clips': [{'Name': name, 'FrameCount': 4, 'Loop': i > 0, 'Baked': False,
                       'FlagsScale': 512, 'FlagsRotate': 4096, 'BoneAnims': []}
                      for i, name in enumerate(pose.CLIPS)]}


class PoseBuildTests(unittest.TestCase):
    def test_cubic_time_normalization_and_clamping(self):
        self.assertEqual(pose.curve_value(curve(), -2), 1)
        self.assertEqual(pose.curve_value(curve(), 4), 2)
        self.assertEqual(pose.curve_value(curve(), 6), 3)
        self.assertEqual(pose.curve_value(curve(), 12), 3)
        invalid = curve()
        invalid['Frames'] = [2, 2]
        with self.assertRaises(ValueError):
            pose.curve_value(invalid, 2)
        invalid = curve()
        invalid['Offset'] = -1.0
        with self.assertRaises(ValueError):
            pose.curve_value(invalid, 2)

    def test_quantized_keys_use_the_float_offset(self):
        quantized = {'CurveType': 0, 'Frames': [0.0, 48.0], 'Keys': [[8071, 0, 0, 0], [8071, 0, 0, 0]],
                     'Scale': 8.228262e-07, 'Offset': 1040170676, 'AnimDataOffset': 32}
        self.assertAlmostEqual(pose.curve_value(quantized, 0), 0.1315167, places=4)

    def test_unanimated_channels_keep_bind_pose(self):
        target = bone()
        target['Position'] = '2;3;4'
        target['Rotation'] = f'0;0;{math.pi / 2}'
        track = {'FlagsBase': 0, 'Curves': [curve()]}
        local = pose.local_transform(target, track, 4)
        self.assertEqual(local[:3], (2, 3, 4))
        self.assertAlmostEqual(local[5], math.sqrt(.5))
        self.assertAlmostEqual(local[6], math.sqrt(.5))
        target['Scale'] = '1;2;1'
        with self.assertRaises(ValueError):
            pose.local_transform(target, track, 4)

    def test_rejects_skeleton_drift_and_cycles(self):
        original = source()['Skeleton']
        for field, value in [('Name', 'Other'), ('ParentIndex', 1), ('Position', '0;1;0'),
                             ('Rotation', '0;0;1'), ('FlagsRotation', 0)]:
            changed = copy.deepcopy(original)
            changed['Bones'][1][field] = value
            with self.assertRaises(ValueError, msg=field):
                pose.verify_skeleton(original, changed)
        cyclic = copy.deepcopy(original)
        cyclic['Bones'][1]['ParentIndex'] = 1
        with self.assertRaises(ValueError):
            pose.verify_skeleton(cyclic, cyclic)

    def test_asset_has_exact_ranges_and_crc(self):
        document = source()
        data, receipt = pose.bake(document, document)
        magic, version, bones, clips, stride, step, crc = struct.unpack_from('<4sHHHHfI', data)
        self.assertEqual((magic, version, bones, clips, stride, step), (b'CBGP', 2, 2, 3, 28, .25))
        self.assertEqual(crc, zlib.crc32(data[20:]))
        self.assertEqual(len(data), 20 + 2*36 + 3*40 + 2*28)
        self.assertEqual((receipt['animated_positions'], receipt['animated_rotations']), (0, 0))
        for i in range(3):
            _, samples, loop, first = struct.unpack_from('<32sHHI', data, 20 + 2*36 + i*40)
            self.assertEqual((samples, loop, first), (17, int(i > 0), i*17))
        self.assertEqual(receipt['bytes'], len(data))

    def test_animated_channels_are_flagged_and_rotations_quantized(self):
        document = source()
        rotate = dict(curve(), AnimDataOffset=32, Scale=.5, Offset=0)
        document['Clips'][1]['BoneAnims'] = [{'Name': 'Hand', 'FlagsBase': 0, 'Curves': [curve(), rotate]}]
        data, receipt = pose.bake(document, document)
        flags = [struct.unpack_from('<32shH', data, 20 + i*36)[2] for i in range(2)]
        self.assertEqual(flags, [0, 3])
        self.assertEqual(len(data), 20 + 2*36 + 3*40 + 2*28 + 51*(12 + 8))
        self.assertEqual((receipt['animated_positions'], receipt['animated_rotations']), (1, 1))
        self.assertLess(receipt['max_rotation_error_degrees'], .01)
        expected = pose.local_transform(bone('Hand', 0), document['Clips'][1]['BoneAnims'][0], 2.0)
        row = 20 + 2*36 + 3*40 + 2*28 + (17 + 8) * 20
        self.assertEqual(struct.unpack_from('<3f', data, row), struct.unpack('<3f', struct.pack('<3f', *expected[:3])))
        stored = [v / 32767 for v in struct.unpack_from('<4h', data, row + 12)]
        for a, b in zip(stored, expected[3:]):
            self.assertAlmostEqual(a, b, delta=1/32767)

    def test_rejects_missing_clips_unbound_bones_and_other_channels(self):
        original = source()
        changed = copy.deepcopy(original)
        changed['Clips'].pop()
        with self.assertRaises(ValueError):
            pose.bake(changed, original)
        changed = copy.deepcopy(original)
        changed['Clips'][0]['BoneAnims'] = [{'Name': 'Absent'}]
        with self.assertRaises(ValueError):
            pose.bake(changed, original)
        track = {'FlagsBase': 0, 'Curves': [dict(curve(), AnimDataOffset=44)]}
        with self.assertRaises(ValueError):
            pose.local_transform(bone(), track, 0)


if __name__ == '__main__':
    unittest.main()
