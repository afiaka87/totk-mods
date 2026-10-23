# SPDX-License-Identifier: MIT
# Copyright (c) Clay Mullis
"""Import the original blessing effects, playback voices and recharge announcements."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

import oead

ROOT = Path(__file__).resolve().parents[4]
sys.path[:0] = [str(ROOT / 'scripts'), str(ROOT / 'framework/bridgefs/tools'),
               str(ROOT / 'mods/code/totk-remote-bomb/tools')]
from bridgefs_tools.audio_bank import read_bank, verify_preserved
from totk_mod_tools.zstd_helper import TotkZstd
from import_audio import parser_budget
from champion_audio import append_records, convert_fwav, sound_record, u32
from champion_slink import SLINK, build_routes

BANK = 'Sound/Resource/UI_Static.bars.zs'
SOURCES = {
    ('Bootup.pack', 'Sound/Resource/M_UI.bars'): {
        'Ready': 'Sys_DivineProtection_Charged'},
    ('TitleBG.pack', 'Sound/Resource/M_SceneStatic.bars'): {
        'Heal': 'Pl_KagoHeel00', 'GaleCharge': 'Pl_Kago_WindRiseUp00',
        'GaleLaunch': 'Pl_Kago_Wind', 'DarukBlock': 'Pl_KagoShield_Count03',
        'DarukBreak': 'Pl_KagoShield_Broken00', 'FuryCharge': 'Pl_Kago_UrubozaRange',
        'FuryStrike': 'Pl_Kago_Lightning00', 'ActivationReady': 'Pl_Kago_Ready'},
    ('TitleBG.pack', 'Sound/Resource/AscendingCurrent_PlayerKago.bars'): {
        'Updraft': 'Env_BrownNoise_Ascending_Kago'},
    ('TitleBG_USen.pack', 'Voice/USen/NV_Custom_NoActor.bars'): {
        'RevaliReady': 'NV_Custom_NoActor_ChargedWind00', 'RevaliUsed': 'NV_Custom_NoActor_StartWind01',
        'UrbosaReady': 'NV_Custom_NoActor_ChargedThunder00', 'UrbosaUsed': 'NV_Custom_NoActor_StartThunder01',
        'MiphaUsed': 'NV_Custom_NoActor_StartWater00', 'DarukUsed': 'NV_Custom_NoActor_StartFire00',
        'MiphaReady': 'NV_Custom_NoActor_ChargedWater00', 'DarukReady': 'NV_Custom_NoActor_ChargedFire00',
        'RevaliChargeVoice': 'NV_Custom_NoActor_StartWind00', 'UrbosaChargeVoice': 'NV_Custom_NoActor_StartThunder00'},
}


def unyaz(data):
    return bytes(oead.yaz0.decompress(data)) if data[:4] == b'Yaz0' else data


def source_waves(bank):
    if bank[:4] != b'BARS' or u32(bank, 4) != len(bank):
        raise ValueError('invalid source BARS')
    count = u32(bank, 12)
    result = {}
    for index in range(count):
        meta, audio = struct.unpack_from('<II', bank, 16 + count * 4 + index * 8)
        if audio == 0xffffffff:
            continue
        endian = {b'\xfe\xff': '>', b'\xff\xfe': '<'}.get(bank[meta + 4:meta + 6])
        if bank[meta:meta + 4] != b'AMTA' or endian is None:
            raise ValueError('invalid source audio metadata')
        name_start = meta + struct.unpack_from(endian + 'I', bank, meta + 24)[0] + 8
        name = bank[name_start:].split(b'\0', 1)[0].decode('ascii')
        if not name or name in result:
            raise ValueError('missing or duplicate source sound name')
        result[name] = bank[audio:audio + u32(bank, audio + 12)]
    return result


def build(args):
    z = TotkZstd(args.totk_romfs)
    clean_raw = z.decompress(args.totk_romfs / BANK)
    clean = read_bank(clean_raw)
    work = args.output / 'audio'
    work.mkdir(parents=True, exist_ok=True)
    sounds, receipts = [], {}
    packs = {}
    for (pack_name, member), mapping in SOURCES.items():
        if pack_name not in packs:
            packs[pack_name] = oead.Sarc(unyaz((args.botw_romfs / 'Pack' / pack_name).read_bytes()))
        raw_bank = bytes(packs[pack_name].get_file(member).data)
        waves = source_waves(raw_bank)
        for cue, name in mapping.items():
            raw = waves[name]
            wave, receipt = convert_fwav(raw)
            asset = 'BotwChampions_' + cue
            sounds.append(sound_record(clean, asset, wave, receipt['loop']))
            (work / f'{cue}.bfwav').write_bytes(raw)
            (work / f'{cue}.bwav').write_bytes(wave)
            receipt.update(source=f'Pack/{pack_name}:{member}:{name}', asset=asset,
                           bwav_sha256=hashlib.sha256(wave).hexdigest(),
                           bank_sha256=hashlib.sha256(raw_bank).hexdigest())
            receipts[cue] = receipt
    if len(sounds) != sum(map(len, SOURCES.values())):
        raise ValueError('the complete source cue set is required')
    raw = append_records(clean, sounds)
    merged = read_bank(raw)
    verify_preserved(clean, merged)
    by_name = {e.name: e for e in merged.entries}
    for sound in sounds:
        entry = by_name[sound.name]
        if entry.metadata != sound.metadata or raw[entry.audio_offset:entry.audio_offset + len(sound.wave)] != sound.wave:
            raise ValueError(f'audio record changed: {sound.name}')
    output = args.output / 'romfs' / BANK
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(z.compress(raw, output.name))
    if z.decompress(output) != raw:
        raise ValueError('audio compression changed bytes')
    table = z.decompress(args.totk_romfs / 'System/Resource/ResourceSizeTable.Product.121.rsizetable.zs')
    floor = max(parser_budget(table, BANK[:-3]), len(clean_raw)) + len(raw) - len(clean_raw) + 65536
    clean_link, link = build_routes(z,args.totk_romfs,args.output,args.xlink,receipts)
    link_output = args.output / 'romfs' / SLINK
    link_output.parent.mkdir(parents=True,exist_ok=True)
    link_output.write_bytes(z.compress(link,link_output.name))
    if z.decompress(link_output) != link:
        raise ValueError('sound routing compression changed bytes')
    link_floor = max(parser_budget(table,SLINK[:-3]),len(link)) + max(65536,(len(link)-len(clean_link))*10)
    (args.output / 'audio-restbl-forces.tsv').write_text(
        f'{BANK[:-3]}\t{floor}\n{SLINK[:-3]}\t{link_floor}\n', encoding='utf-8')
    generated = args.output / 'generated'
    generated.mkdir(exist_ok=True)
    header = ('// Private original-game sound identifiers; generated locally.\n#pragma once\n'
              '#include <array>\n#include "champions/AudioCue.hpp"\n'
              'namespace botw_abilities::champions::audio {\n'
              f'inline constexpr std::array<const char*, {len(sounds)}> kAssets{{\n')
    header += ''.join(f'    "{s.name}",\n' for s in sounds) + '};\n'
    header += f'inline constexpr std::array<bool, {len(sounds)}> kLoops{{\n'
    header += ''.join(f'    {str(r["loop"]).lower()},\n' for r in receipts.values()) + '};\n'
    header += f'inline constexpr std::array<float, {len(sounds)}> kSeconds{{\n'
    header += ''.join(f'    {r["samples"]}.0f/{r["rate"]}.0f,\n' for r in receipts.values()) + '};\n'
    header += ''.join(f'static_assert(unsigned(Cue::{cue}) == {index});\n'
                      for index, cue in enumerate(receipts))
    header += 'static_assert(unsigned(Cue::Count) == kAssets.size());\n}\n'
    (generated / 'ChampionSounds.hpp').write_text(header, encoding='utf-8')
    receipt = {'source_version': 'owned BotW RomFS; effective version unverified', 'voice_language': 'USen', 'sounds': receipts,
               'native_sounds_preserved': len(clean.entries), 'added_bytes': len(raw) - len(clean_raw),
               'output_sha256': hashlib.sha256(output.read_bytes()).hexdigest(), 'parser_floor': floor}
    (args.output / 'audio-receipt.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    print(f'Champion audio: {len(sounds)} original sounds, {len(raw) - len(clean_raw):,} added bytes; native bank preserved')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--botw-romfs', type=Path, required=True)
    parser.add_argument('--totk-romfs', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--xlink', type=Path, required=True)
    build(parser.parse_args())
