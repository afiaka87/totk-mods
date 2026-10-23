# SPDX-License-Identifier: MIT
# Copyright (c) Clay Mullis
"""Verify source waveform fidelity and preservation of native sound resources."""
from __future__ import annotations
import argparse
import importlib.util
import struct
import sys
import unittest
from pathlib import Path

import oead

TOOLS = Path(__file__).resolve().parents[1] / 'tools'
spec = importlib.util.spec_from_file_location('bomb_audio', TOOLS / 'import_audio.py')
audio = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audio)
parser = argparse.ArgumentParser()
parser.add_argument('--botw-romfs', required=True, type=Path)
parser.add_argument('--totk-romfs', required=True, type=Path)
parser.add_argument('--candidate', required=True, type=Path)
parser.add_argument('--xlink', required=True, type=Path)
args, test_args = parser.parse_known_args()


class AudioTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if audio.digest(args.xlink.read_bytes()) != audio.XLINK_SHA256:
            raise ValueError('Unverified XLink tool')
        cls.z = audio.TotkZstd(args.totk_romfs)
        cls.bank = audio.read_bank(cls.z.decompress(args.candidate / 'romfs' / audio.BANK))

    def test_original_waveforms_and_loop_range(self):
        packed = (args.botw_romfs / 'Pack/TitleBG.pack').read_bytes()
        if packed[:4] == b'Yaz0':
            packed = bytes(oead.yaz0.decompress(packed))
        original = bytes(oead.Sarc(packed).get_file('Sound/Resource/M_SceneStatic.bars').data)
        count = struct.unpack_from('<I', original, 12)[0]
        checked = set()
        for index in range(count):
            meta, offset = struct.unpack_from('<II', original, 16+4*count+8*index)
            name_offset = meta+8+struct.unpack_from('<I', original, meta+24)[0]
            name = original[name_offset:].split(b'\0', 1)[0].decode()
            if name not in audio.NAMES:
                continue
            with self.subTest(name=name):
                checked.add(name)
                wave = original[offset:offset+struct.unpack_from('<I', original, offset+12)[0]]
                info, data = struct.unpack_from('<I', wave, 24)[0], struct.unpack_from('<I', wave, 36)[0]
                rate, loop_start, samples = struct.unpack_from('<III', wave, info+12)
                channel = info+28+struct.unpack_from('<I', wave, info+36)[0]
                coefficient = channel+struct.unpack_from('<I', wave, channel+12)[0]
                payload = data+8+struct.unpack_from('<I', wave, channel+4)[0]
                entry = next(row for row in self.bank.entries if row.name == audio.PREFIX+name)
                native = self.bank.data[entry.audio_offset:]
                self.assertEqual(struct.unpack_from('<HHIII', native, 16), (1, 2, rate, samples, samples))
                self.assertEqual(native[32:64], wave[coefficient:coefficient+32])
                self.assertEqual(native[84:90], wave[coefficient+32:coefficient+38])
                self.assertEqual(struct.unpack_from('<II', native, 76),
                                 (samples if wave[info+9] else 0xFFFFFFFF, loop_start))
                size = ((samples+13)//14)*8
                self.assertEqual(native[128:128+size], wave[payload:payload+size])
                self.assertEqual(entry.flags, 6 if wave[info+9] else 2)
        self.assertEqual(checked, set(audio.NAMES))

    def test_stock_bank_is_byte_preserved(self):
        original = audio.read_bank(self.z.decompress(args.totk_romfs / audio.BANK))
        audio.verify_preserved(original, self.bank)
        self.assertEqual({row.name for row in self.bank.entries}-{row.name for row in original.entries},
                         {audio.PREFIX+name for name in audio.NAMES})

    def test_native_sound_database_preservation_and_routes(self):
        raw = args.candidate / 'validation.bslnk'
        raw.write_bytes(self.z.decompress(args.candidate / 'romfs' / audio.SLINK))
        decoded = args.candidate / 'validation.txt'
        audio.transcode(args.xlink, raw, decoded, 'EXKing', args.candidate / 'users.txt')
        original_raw = args.candidate / 'validation-stock.bslnk'
        original_raw.write_bytes(self.z.decompress(args.totk_romfs / audio.SLINK))
        original_text = args.candidate / 'validation-stock.txt'
        audio.transcode(args.xlink, original_raw, original_text, 'EXKing', args.candidate / 'users.txt')
        stock, candidate = original_text.read_text(encoding='utf8'), decoded.read_text(encoding='utf8')
        self.assertEqual(stock.split('Users {', 1)[0], candidate.split('Users {', 1)[0])
        before, after = audio.user_blocks(stock), audio.user_blocks(candidate)
        self.assertEqual(set(after)-set(before), {audio.USER})
        for name, original in before.items():
            self.assertEqual(after[name], original, name)
        user = after[audio.USER]
        self.assertIn('Asset = Appear[', user)
        self.assertIn('Asset = RemoteBomb_hold[', user)
        self.assertIn('Display_Explosion[', user)
        self.assertNotIn('RuntimeAssetName = "SE_BM_EXPLODE"', user)
        for name in audio.NAMES:
            self.assertIn('RuntimeAssetName = "'+audio.PREFIX+name+'"', user)
        self.assertIn('Volume = 0.300000012', user)
        self.assertIn('Volume = 0.200000003', user)


if __name__ == '__main__':
    unittest.main(argv=[sys.argv[0], *test_args])
