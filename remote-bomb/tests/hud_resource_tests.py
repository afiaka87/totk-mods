# SPDX-License-Identifier: MIT
# Copyright (c) Clay Mullis
"""Check the generated HUD against owned original pixels, not the render recipe."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import unittest
import oead
from PIL import Image

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0,str(ROOT/'scripts'))
from totk_mod_tools.textures.bntx import Bank

parser=argparse.ArgumentParser()
parser.add_argument('--romfs',required=True,type=Path)
parser.add_argument('--candidate',required=True,type=Path)
args,unittest_args=parser.parse_known_args()


def decoded(data):
    return bytes(oead.yaz0.decompress(data)) if data[:4]==b'Yaz0' else data


class HudResourceTests(unittest.TestCase):
    def test_original_rgba_channels_are_unchanged(self):
        names=('Obj_RemoteBombBall','Obj_RemoteBombCube','Obj_RemoteBombBall2','Obj_RemoteBombCube2')
        for i,name in enumerate(names):
            source=decoded((args.romfs/'UI/StockItem'/f'{name}.sbitemico').read_bytes())
            bank=Bank(source[source.index(b'BNTX'):])
            texture=bank.get(name)
            self.assertEqual((texture.width,texture.height,texture.format_name),(144,144,'RGBA8'))
            actual=Image.open(args.candidate/f'hud-{i}.png').convert('RGBA')
            self.assertEqual(actual.tobytes(),bank.linear(texture))

    def test_original_mask_channel_is_preserved(self):
        boot=oead.Sarc(decoded((args.romfs/'Pack/Bootup.pack').read_bytes()))
        common=oead.Sarc(decoded(bytes(boot.get_file('Layout/Common.sblarc').data)))
        bank=Bank(bytes(common.get_file('timg/__Combined.bntx').data))
        for i,name in enumerate(('Sd_ItemGaugeR_04^s','Sd_ItemGaugeRGlow_04^s','Sd_ItemGaugeRGlowFull_04^s'),4):
            texture=bank.get(name)
            self.assertEqual(texture.format_name,'BC4')
            source=bank.decode(texture)
            mask=Image.frombytes('RGBA',(source.width,source.height),source.data).getchannel('R')
            actual=Image.open(args.candidate/f'hud-{i}.png').convert('RGBA')
            self.assertEqual(actual.getchannel('A').tobytes(),mask.tobytes())
            self.assertEqual(actual.getchannel('R').getextrema(),(255,255))

    def test_receipt_matches_generated_pixels_and_header(self):
        receipt=json.loads((args.candidate/'hud-asset-receipt.json').read_text())
        self.assertEqual(receipt['header_sha256'],hashlib.sha256((args.candidate/'RemoteBombHudAssets.hpp').read_bytes()).hexdigest())
        self.assertEqual(len(receipt['images']),7)
        for i,entry in enumerate(receipt['images']):
            actual=Image.open(args.candidate/f'hud-{i}.png').convert('RGBA')
            self.assertEqual(entry['pixels_sha256'],hashlib.sha256(actual.tobytes()).hexdigest())


if __name__=='__main__':
    unittest.main(argv=[sys.argv[0],*unittest_args])
