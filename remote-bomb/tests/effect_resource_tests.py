# SPDX-License-Identifier: MIT
# Copyright (c) Clay Mullis
"""Validate original particle texture and colour data preservation."""
import argparse
import hashlib
import json
import sys
import unittest
from pathlib import Path
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
import import_effects as effects


class OriginalEffects(unittest.TestCase):
    def test_original_emitter_census_and_colours(self):
        _, original, source_hash = effects.extract(self.source)
        self.assertEqual(source_hash, 'f4275fc47514868ce06664a6363e9f9003a6d1cae8a8158e0055ce033410d177')
        self.assertEqual(self.receipt['emitters'], original)
        self.assertEqual(len(original), 23)
        names = [(e['set'], e['name']) for e in original]
        self.assertEqual(names[3], ('BombAppear', 'Glow'))
        self.assertEqual(names[11], ('RemoteBomb_Stripe', 'Himo_main'))
        self.assertEqual(names[13], ('Test_Explosion', 'AFadeoutStill_Copy1'))

    def test_all_twenty_one_texture_pixels(self):
        images, _, _ = effects.extract(self.source)
        for i, (original, info) in enumerate(images):
            with self.subTest(texture=info['name']):
                candidate = Image.open(self.candidate/f'effect-{i}.png').convert('RGBA')
                self.assertEqual(candidate.size, original.size)
                self.assertEqual(candidate.tobytes(), original.tobytes())
                self.assertEqual(hashlib.sha256(candidate.tobytes()).hexdigest(),
                                 self.receipt['images'][i]['pixels_sha256'])

    def test_header_receipt_and_texture_channels(self):
        header = (self.candidate/'RemoteBombEffectAssets.hpp').read_bytes()
        self.assertEqual(hashlib.sha256(header).hexdigest(), self.receipt['header_sha256'])
        self.assertEqual(self.receipt['vfx_version'], 20)
        channels = Image.open(self.candidate/'effect-7.png').convert('RGBA').split()
        self.assertEqual(channels[2].getextrema(), (0, 0))
        self.assertEqual(channels[3].getextrema(), (255, 255))
        self.assertNotEqual(channels[0].tobytes(), channels[1].tobytes())


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--romfs', type=Path, required=True)
    parser.add_argument('--candidate', type=Path, required=True)
    args, rest = parser.parse_known_args()
    OriginalEffects.source, OriginalEffects.candidate = args.romfs, args.candidate
    OriginalEffects.receipt = json.loads((args.candidate/'effect-asset-receipt.json').read_text())
    unittest.main(argv=[sys.argv[0]]+rest)
