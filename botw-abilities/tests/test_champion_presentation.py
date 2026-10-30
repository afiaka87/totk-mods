# SPDX-License-Identifier: MIT
# Copyright (c) Clay Mullis
import copy
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parents[1] / 'tools'))
from champion_audio import convert_fwav
from build_champion_items import extend_rows, DONOR
from build_champion_effects import effect_specs, tint_for, COLORS, SHELL_TINT


def fwav(channels=1, rate=32000, loop=False):
    raw = bytearray(512)
    raw[:6] = b'FWAV\xff\xfe'
    struct.pack_into('<I', raw, 12, len(raw))
    struct.pack_into('<I', raw, 24, 64)
    struct.pack_into('<I', raw, 36, 384)
    raw[64:68], raw[384:388] = b'INFO', b'DATA'
    raw[72:74] = bytes([2, int(loop)])
    struct.pack_into('<IIII', raw, 76, rate, 7 if loop else 0, 14, 0)
    struct.pack_into('<I', raw, 92, channels)
    for i in range(channels):
        channel, coefficients, payload = 128 + i * 96, 144 + i * 96, 392 + i * 64
        struct.pack_into('<I', raw, 100 + i * 8, channel - 92)
        struct.pack_into('<I', raw, channel + 4, payload - 392)
        struct.pack_into('<I', raw, channel + 12, coefficients - channel)
        struct.pack_into('<16hHhh', raw, coefficients, *([i + 1] * 16), i, i * 10, -i * 10)
        raw[payload:payload + 8] = bytes([i]) + bytes([10 + i]) * 7
    return bytes(raw)


class ChampionPresentationTests(unittest.TestCase):
    def test_effects_use_their_champion_tints(self):
        effects = effect_specs()
        self.assertEqual(len(effects), 26)
        self.assertEqual(effects['UrbosaBolt'], ('Chm_Lightning_Sage_Soul_Gerudo', 'Urbosa', 'static'))
        for name, (_, hero, _) in effects.items():
            if name in ('DarukAura', 'DarukImpact'):
                self.assertEqual(tint_for(name, hero), (0.08, 0.7, 1.0))
            elif name.startswith('DarukShell'):
                self.assertEqual(tint_for(name, hero), SHELL_TINT)
            else:
                self.assertEqual(tint_for(name, hero), COLORS[hero])

    def test_daruk_shell_is_sidon_shell_tinted_orange(self):
        effects = effect_specs()
        self.assertEqual(effects['DarukShell'], ('Ply_WaterBarrier', 'Daruk', 'static'))
        self.assertEqual(effects['DarukShellBreak'], ('Ply_WaterBarrier_Break_Short', 'Daruk', 'static'))
        red, green, blue = (a * b for a, b in zip((0.2, 0.65, 0.85), SHELL_TINT, strict=True))
        self.assertGreater(SHELL_TINT[0], 1.0)
        self.assertTrue(red > green > blue > 0)

    def test_mipha_uses_the_native_water_burst_with_her_own_blue_tint(self):
        effects = effect_specs()
        effect, hero, bank = effects['MiphaWater']
        self.assertEqual(effect, 'Wpn_WearWater_Bubble_Start_Extend')
        self.assertEqual((hero, bank), ('Mipha', 'static'))
        self.assertNotEqual(COLORS[hero], COLORS['Daruk'])

    def test_stereo_channels_keep_distinct_payloads_and_decoder_state(self):
        result, info = convert_fwav(fwav(2, 48000, True))
        self.assertEqual((info['channels'], info['loop_start']), (2, 7))
        offsets = []
        for i in range(2):
            at = 16 + i * 76
            self.assertEqual(struct.unpack_from('<H', result, at + 2)[0], i)
            self.assertEqual(struct.unpack_from('<16h', result, at + 16), tuple([i + 1] * 16))
            self.assertEqual(struct.unpack_from('<Hhh', result, at + 68), (i, i * 10, -i * 10))
            self.assertEqual(struct.unpack_from('<II', result, at + 60), (14, 7))
            offset = struct.unpack_from('<I', result, at + 52)[0]
            self.assertEqual(result[offset:offset + 8], bytes([i]) + bytes([10 + i]) * 7)
            offsets.append(offset)
        self.assertGreater(offsets[1], offsets[0])

    def test_voice_rate_is_not_resampled(self):
        result, _ = convert_fwav(fwav())
        self.assertEqual(struct.unpack_from('<I', result, 20)[0], 32000)
        self.assertEqual(struct.unpack_from('<I', result, 76)[0], 0xffffffff)

    def test_invalid_channel_count_and_truncation_refused(self):
        raw = bytearray(fwav())
        struct.pack_into('<I', raw, 92, 3)
        with self.assertRaisesRegex(ValueError, 'geometry'):
            convert_fwav(bytes(raw))
        with self.assertRaisesRegex(ValueError, 'complete'):
            convert_fwav(fwav()[:-1])

    def test_items_preserve_native_vow_and_do_not_inherit_sage_action(self):
        rows = [{'__RowId': DONOR, 'PouchCategory': 'KeyItem', 'PouchSpecialDeal': 'None', 'PouchSortKey': 420},
                {'__RowId': 'Obj_SageSoul_Rito', 'PouchCategory': 'KeyItem', 'PouchSpecialDeal': 'SageSoul_Wind'}]
        original = copy.deepcopy(rows)
        result = extend_rows(rows, 'PouchActorInfo')
        self.assertEqual(rows, original)
        self.assertEqual(len(result), 6)
        self.assertEqual(next(r for r in result if r['__RowId'] == 'Obj_SageSoul_Rito'), original[1])
        added = [r for r in result if r['__RowId'].startswith('Obj_BotwHeroSoul_')]
        self.assertEqual(len(added), 4)
        self.assertTrue(all(r['PouchSpecialDeal'] == 'None' for r in added))
        with self.assertRaises(ValueError):
            extend_rows(result, 'PouchActorInfo')


if __name__ == '__main__':
    unittest.main()
