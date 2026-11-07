# SPDX-License-Identifier: MIT
# Copyright (c) Clay Mullis
"""Give the wolf his own sound user as a reviewed draft: the stock wolf's, plus the original's bite calls.

The waves go in a bank of their own that the sound list loads with the wolf, so they cost memory only while
he is summoned. The calls are played from code (src/wolf/SoundCues.hpp) at the original's frames.
"""
import argparse
import hashlib
import json
import re
import struct
import sys
from pathlib import Path

import byml
import oead

ROOT = Path(__file__).resolve().parents[4]
sys.path[:0] = [str(ROOT / 'scripts'), str(ROOT / 'mods/code/totk-remote-bomb/tools'),
                str(ROOT / 'framework/bridgefs/tools')]
from totk_mod_tools.zstd_helper import TotkZstd
from import_audio import SLINK, XLINK_SHA256, block, transcode, user_blocks
from bridgefs_tools.audio_bank import Bank, read_bank
sys.path.insert(0, str(Path(__file__).resolve().parent))
from champion_audio import append_records, convert_fwav, sound_record
from resource_merge import encode_byml

USER, DONOR, ORIGINAL = 'BotwWolfLink', 'Animal_Wolf_A', 'WolfLink'
BANK = f'Sound/Resource/{USER}.bars.zs'
BARSLIST = 'Sound/ResourceListPack/BarslistPack.Product.110.byml.zs'
# The original's bite calls: AS[0] Attack and Attack_ChainAttack, and the chain lunge's action slot.
ADDED = ('ChainLarge', 'ChainAttackWind', 'RoarAttack')
# Every call the code plays; Roar and ChargeFootStep are the stock wolf's own.
PLAYED = ('Roar', 'ChargeFootStep') + ADDED
GROUPS = {'LifeDefault': 'Actor', 'LifeVoice': 'ActorVoice'}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def u32(data, offset):
    return struct.unpack_from('<I', data, offset)[0]


def unyaz(raw):
    return bytes(oead.yaz0.decompress(raw)) if raw[:4] == b'Yaz0' else raw


def original_waves(bank):
    if bank[:4] != b'BARS' or u32(bank, 4) != len(bank):
        raise ValueError('Invalid original wolf bank')
    count, waves = u32(bank, 12), {}
    for index in range(count):
        meta, audio = struct.unpack_from('<II', bank, 16 + count * 4 + index * 8)
        start = meta + u32(bank, meta + 24) + 8
        waves[bank[start:].split(b'\0', 1)[0].decode('ascii')] = bank[audio:audio + u32(bank, audio + 12)]
    return waves


def call_block(calls, name):
    return block(calls, r'^      ' + name + r'\[0x[0-9a-f]+\] \{')


def wolf_user(original, stock):
    """The stock wolf's user under the private name, with the original's bite calls in the stock groups."""
    if not stock.startswith(f'  {DONOR} {{'):
        raise ValueError('Unexpected stock wolf user header')
    calls = block(stock, r'^    AssetCallTables \{')
    reference = block(original, r'^    AssetCallTables \{')
    identifiers = set(re.findall(r'\[0x([0-9a-f]+)\]', stock))
    added = []
    for name in ADDED:
        if re.search(r'^      ' + name + r'\[', calls, re.MULTILINE):
            raise ValueError('The stock wolf already has a call named ' + name)
        call = call_block(reference, name)
        for identifier in re.findall(r'\[0x([0-9a-f]+)\]', call):
            if identifier in identifiers:
                raise ValueError(f'{name}: call identifier collides with the stock wolf user')
            identifiers.add(identifier)
        groups = set(re.findall(r'GroupName = "([^"]*)"', call))
        if not groups or groups - GROUPS.keys():
            raise ValueError(f'{name}: unmapped original sound group {sorted(groups)}')
        call = re.sub(r'GroupName = "([^"]*)"', lambda m: f'GroupName = "{GROUPS[m[1]]}"', call)
        # The original's distance sets and ducking names do not exist in this game's tables.
        call = re.sub(r'^ +(DistanceParamSetName|Ducking) = [^\n]+\n', '', call, flags=re.MULTILINE)
        added.append(call)
    user = stock.replace(f'  {DONOR} {{', f'  {USER} {{', 1)
    return user.replace(calls, calls.rsplit('\n', 1)[0] + '\n' + '\n'.join(added) + '\n    }', 1)


def bank_from(template, waves, names):
    """A new bank holding only these waves, each record cloned from a simple mono record of the stock wolf bank."""
    donor = next(entry.name for entry in template.entries
                 if entry.flags == 2 and entry.metadata[0x31] == 1 and entry.metadata[6:8] == b'\0\x05'
                 and not any(u32(entry.metadata, p) for p in (0x0c, 0x14, 0x18, 0x1c, 0x20)))
    sounds, receipts = [], {}
    for name in names:
        wave, receipt = convert_fwav(waves[name])
        if receipt['channels'] != 1 or receipt['loop']:
            raise ValueError(f'{name}: expected a mono one-shot')
        sounds.append(sound_record(template, name, wave, False, donor))
        receipts[name] = receipt
    empty = Bank(template.data[:16], (), template.index_padding, 16)
    raw = append_records(empty, sounds)
    entries = {entry.name: entry for entry in read_bank(raw).entries}
    for sound in sounds:
        offset = entries[sound.name].audio_offset
        if entries[sound.name].metadata != sound.metadata or raw[offset:offset + len(sound.wave)] != sound.wave:
            raise ValueError(f'{sound.name}: bank record did not read back')
    if set(entries) != set(names):
        raise ValueError('New wolf bank holds unexpected records')
    return raw, donor, receipts


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--romfs', required=True, type=Path, help='clean TotK 1.2.1 RomFS')
    parser.add_argument('--botw-romfs', required=True, type=Path, help='BotW 1.6.0 RomFS')
    parser.add_argument('--payload', required=True, type=Path, help='verified v0.2.11 payload directory')
    parser.add_argument('--baseline-receipt', required=True, type=Path)
    parser.add_argument('--xlink', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    if digest(args.xlink.read_bytes()) != XLINK_SHA256:
        raise ValueError('unreviewed XLink converter')
    rows = {row['path']: row['sha256'] for row in
            json.loads(args.baseline_receipt.read_text(encoding='utf-8'))['artifact']['files']}
    if 'romfs/' + BARSLIST in rows or 'romfs/' + BANK in rows:
        raise ValueError('The accepted payload already ships the sound list or a wolf bank')
    baseline_slink = (args.payload / 'romfs' / SLINK).read_bytes()
    if digest(baseline_slink) != rows['romfs/' + SLINK]:
        raise ValueError('baseline sound links no longer match the accepted receipt')
    z = TotkZstd(args.romfs)

    work = args.output / 'work'
    work.mkdir(parents=True, exist_ok=True)
    names = work / 'names.txt'
    names.write_text('\n'.join((ORIGINAL, DONOR, USER, 'UI_GlobalSound', 'TotkRemoteBomb')) + '\n', encoding='utf-8')
    boot = oead.Sarc(unyaz((args.botw_romfs / 'Pack/Bootup.pack').read_bytes()))
    original_slink = unyaz(bytes(boot.get_file('SLink2/SLink2DB.sbslnk').data))
    original_binary, original_text = work / 'original.sbslnk', work / 'original.txt'
    original_binary.write_bytes(original_slink)
    transcode(args.xlink, original_binary, original_text, 'UKing', names)
    base_binary, base_text = work / 'baseline.bslnk', work / 'baseline.txt'
    base_binary.write_bytes(z.decompress(args.payload / 'romfs' / SLINK))
    transcode(args.xlink, base_binary, base_text, 'EXKing', names)
    text = base_text.read_text(encoding='utf-8')
    users = user_blocks(text)
    if USER in users:
        raise ValueError('The accepted sound links already hold the wolf user')
    user = wolf_user(user_blocks(original_text.read_text(encoding='utf-8'))[ORIGINAL], users[DONOR])
    candidate, merged_binary, restored = work / 'candidate.txt', work / 'candidate.bslnk', work / 'restored.txt'
    candidate.write_text(text.rstrip()[:-1] + user + '\n}\n', encoding='utf-8')
    transcode(args.xlink, candidate, merged_binary, 'EXKing', names)
    transcode(args.xlink, merged_binary, restored, 'EXKing', names)
    after = restored.read_text(encoding='utf-8')
    if (user_blocks(after) != users | {USER: user}
            or after.split('Users {', 1)[0] != text.split('Users {', 1)[0]):
        raise ValueError('Sound link conversion changed the accepted users or the wolf user')
    calls = block(user, r'^    AssetCallTables \{')
    for name in PLAYED:
        call_block(calls, name)
    slink_raw = merged_binary.read_bytes()

    added_calls = '\n'.join(call_block(calls, name) for name in ADDED)
    wave_names = sorted(set(re.findall(r'RuntimeAssetName = "([^"]+)"', added_calls)))
    original_bank = (args.botw_romfs / 'Sound/Resource/WolfLink.bars').read_bytes()
    template = read_bank(z.decompress(args.romfs / f'Sound/Resource/{DONOR}.bars.zs'))
    bank_raw, record_donor, waves = bank_from(template, original_waves(original_bank), wave_names)

    barslist_raw = z.decompress(args.romfs / BARSLIST)
    barslist = byml.Byml(barslist_raw).parse()
    table = barslist['BarslistTable']
    if USER in table or list(table[DONOR]) != ['Bars']:
        raise ValueError('Unexpected stock sound list entry')
    table[USER] = {'Bars': [f'{USER}.bars'] + list(table[DONOR]['Bars'])}
    barslist_merged = encode_byml(barslist, barslist_raw)

    files, floors = {}, {}
    # Stock banks and the sound list reserve their raw size plus under 600 bytes.
    for path, raw in ((SLINK, slink_raw), (BANK, bank_raw), (BARSLIST, barslist_merged)):
        target = args.output / 'romfs' / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(z.compress(raw, path))
        if z.decompress(target) != raw:
            raise ValueError('Compressed sound round trip failed: ' + path)
        files['romfs/' + path] = digest(target.read_bytes())
        floors[path.removesuffix('.zs')] = len(raw) + (4096 if path == SLINK else 1024)
    receipt = {
        'xlink_sha256': XLINK_SHA256,
        'baseline_slink_sha256': digest(baseline_slink),
        'original_slink_sha256': digest(original_slink),
        'original_bank_sha256': digest(original_bank),
        'user': USER, 'donor_user': DONOR, 'added_calls': list(ADDED), 'played_calls': list(PLAYED),
        'record_donor': record_donor, 'users_preserved': len(users),
        'bank_bytes': len(bank_raw), 'slink_bytes': {'baseline': len(base_binary.read_bytes()), 'merged': len(slink_raw)},
        'sound_list': table[USER], 'waves': waves, 'files': files, 'floors': floors,
    }
    (args.output / 'sound-receipt.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    print(f'Wolf sound: {len(users)} users preserved, {USER} added with {len(ADDED)} calls; '
          f'bank {len(bank_raw)} bytes ({len(wave_names)} waves); sound links {len(slink_raw)} bytes')


if __name__ == '__main__':
    main()
