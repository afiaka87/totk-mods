# SPDX-License-Identifier: MIT
# Copyright (c) Clay Mullis
"""Import owned bomb waveforms and append a private native SLink user."""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
import subprocess
import sys
import zlib
from pathlib import Path

import oead

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / 'scripts'))
sys.path.insert(0, str(ROOT / 'framework/bridgefs/tools'))
from totk_mod_tools.zstd_helper import TotkZstd
from bridgefs_tools.audio_bank import Addition, append_assets, read_bank, verify_preserved

USER = 'TotkRemoteBomb'
PREFIX = USER + '_'
SLINK = 'SLink2/slink2.Product.110.bslnk.zs'
BANK = 'Sound/Resource/UI_Static.bars.zs'
NAMES = ('Bomb_appear01', 'RemoteBomb_hold', 'RemoteBomb_Cancel', 'RemoteBomb', 'RemoteBomb_smallExplode')
XLINK_SHA256 = '832a23ce5c4f7cdc9d1439fdce11302a5787bd60ef6730a29c3565af79455e43'


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from('<I', data, offset)[0]


def convert(wave: bytes) -> tuple[bytes, dict]:
    if wave[:6] != b'FWAV\xff\xfe' or u32(wave, 12) != len(wave):
        raise ValueError('Expected a complete owned little-endian FWAV')
    info, data = u32(wave, 24), u32(wave, 36)
    if wave[info:info+4] != b'INFO' or wave[data:data+4] != b'DATA':
        raise ValueError('Missing FWAV sections')
    table = info + 28
    loop = wave[info+9]
    rate, start, count = u32(wave, info+12), u32(wave, info+16), u32(wave, info+20)
    if wave[info+8] != 2 or loop not in (0, 1) or u32(wave, table) != 1 or rate != 48000:
        raise ValueError('Expected original mono DSP at 48 kHz')
    if not 0 < count <= rate * 10 or not 0 <= start < count:
        raise ValueError('Invalid FWAV sample range')
    channel = table + u32(wave, table+8)
    coefficient = channel + u32(wave, channel+12)
    coefficients = struct.unpack_from('<16h', wave, coefficient)
    predictor, h1, h2 = struct.unpack_from('<Hhh', wave, coefficient+32)
    loop_context = struct.unpack_from('<Hhh', wave, coefficient+38)
    offset = data + 8 + u32(wave, channel+4)
    payload = wave[offset:offset+((count+13)//14)*8]
    if len(payload) != ((count+13)//14)*8 or predictor != payload[0]:
        raise ValueError('Truncated DSP payload or invalid initial predictor')
    header = struct.pack('<4sHHIHH', b'BWAV', 0xFEFF, 1, zlib.crc32(payload), 0, 1)
    metadata = struct.pack('<HHIII16hIIIIIHhhH', 1, 2, rate, count, count, *coefficients,
                           0x80, 0x80, 1, count if loop else 0xFFFFFFFF, start,
                           predictor, h1, h2, 0)
    result = (header + metadata).ljust(0x80, b'\0') + payload
    return result, {'source_sha256': digest(wave), 'samples': count, 'rate': rate,
                    'loop': bool(loop), 'loop_start': start,
                    'dsp_payload_sha256': digest(payload), 'loop_context': loop_context,
                    'coefficients_and_initial_history_preserved': True}


def original_waves(bank: bytes) -> dict[str, tuple[bytes, dict]]:
    if bank[:4] != b'BARS' or u32(bank, 4) != len(bank):
        raise ValueError('Invalid original bomb bank')
    count = u32(bank, 12)
    result = {}
    for index in range(count):
        meta, audio = struct.unpack_from('<II', bank, 16+count*4+index*8)
        name_start = meta + u32(bank, meta+24) + 8
        name = bank[name_start:].split(b'\0', 1)[0].decode('ascii')
        if name in NAMES:
            result[name] = convert(bank[audio:audio+u32(bank, audio+12)])
    if set(result) != set(NAMES):
        raise ValueError('Original bomb waveforms are missing')
    return result


def block(text: str, pattern: str) -> str:
    match = re.search(pattern, text, re.MULTILINE)
    if match is None:
        raise ValueError('Missing XLink block: ' + pattern)
    start, opening = match.start(), text.index('{', match.start())
    depth, quoted, escaped = 1, False, False
    for index in range(opening+1, len(text)):
        char = text[index]
        if escaped:
            escaped = False
        elif char == '\\' and quoted:
            escaped = True
        elif char == '"':
            quoted = not quoted
        elif not quoted:
            if char == '{':
                depth += 1
            elif char == '}':
                depth -= 1
                if not depth:
                    return text[start:index+1]
    raise ValueError('Unterminated XLink block')


def user_blocks(text: str) -> dict[str, str]:
    users = block(text, r'^Users \{')
    matches = list(re.finditer(r'^  ([^\s{}][^\n{}]*) \{', users, re.MULTILINE))
    result = {}
    for index, match in enumerate(matches):
        end = matches[index+1].start() if index+1 < len(matches) else len(users)-1
        result[match[1]] = users[match.start():end].rstrip()
    return result


def bomb_user(original: str, native: str) -> str:
    reference = user_blocks(original)['RemoteBomb']
    donor = user_blocks(native)['BarrelBomb']
    calls = block(reference, r'^    AssetCallTables \{')
    native_calls = block(donor, r'^    AssetCallTables \{')
    mappings = (('Appear', 'Appear'), ('RemoteBomb_hold', 'RemoteBomb_hold'), ('Cancel', 'Cancel'),
                ('Chemical_Explosion', 'Display_Explosion'),
                ('Chemical_Explosion_Inside', 'Display_Explosion_Inside'))
    for source, target in mappings:
        call = block(calls, r'^      ' + source + r'\[0x[0-9a-f]+\] \{')
        call = call.replace(source + '[', target + '[').replace('AssetName = "' + source + '"',
                                                                'AssetName = "' + target + '"')
        call = re.sub(r'(RuntimeAssetName = ")([^"]+)', r'\1' + PREFIX + r'\2', call)
        call = re.sub(r'GroupName = "[^"]+"', 'GroupName = "' + ('Chemical' if 'Explosion' in target else 'Actor') + '"', call)
        call = re.sub(r'^          (DistanceParamSetName|Ducking) = [^\n]+\n', '', call, flags=re.MULTILINE)
        if target == 'Display_Explosion':
            old = block(native_calls, r'^      Display_Explosion\[0x[0-9a-f]+\] \{')
            native_calls = native_calls.replace(old, call, 1)
        else:
            native_calls = native_calls[:-1] + call + '\n    }'
    result = donor.replace('  BarrelBomb {', '  ' + USER + ' {', 1).replace(
        block(donor, r'^    AssetCallTables \{'), native_calls, 1)
    always = block(reference, r'^    AlwaysTriggers \{')
    return result.replace('    AssetCallTables {', always + '\n    AssetCallTables {', 1)


def transcode(executable: Path, source: Path, output: Path, game: str, names: Path) -> None:
    process = subprocess.run([str(executable), '-i', str(source), '-o', str(output),
                              '-g', game, '-n', str(names)], capture_output=True, text=True)
    if process.returncode or process.stdout.strip() or process.stderr.strip() or not output.is_file():
        raise ValueError('XLink conversion refused: ' + (process.stdout + process.stderr)[-1200:])


def parser_budget(table: bytes, path: str) -> int:
    if table[:6] != b'RESTBL':
        raise ValueError('Expected clean native resource table')
    width, count, overflow = struct.unpack_from('<III', table, 10)
    for index in range(overflow):
        start = 22 + count*8 + index*(width+4)
        if table[start:start+width].split(b'\0', 1)[0].decode() == path:
            return u32(table, start+width)
    key = zlib.crc32(path.encode())
    for index in range(count):
        checksum, size = struct.unpack_from('<II', table, 22+index*8)
        if checksum == key:
            return size
    raise ValueError('Missing clean parser budget: ' + path)


def build(botw: Path, clean: Path, output: Path, xlink: Path, actor_forces: Path) -> dict:
    if digest(xlink.read_bytes()) != XLINK_SHA256:
        raise ValueError('Unverified XLink tool; expected pinned v1.1.4')
    output.mkdir(parents=True, exist_ok=True)
    title = (botw / 'Pack/TitleBG.pack').read_bytes()
    if title[:4] == b'Yaz0':
        title = bytes(oead.yaz0.decompress(title))
    original_bank = bytes(oead.Sarc(title).get_file('Sound/Resource/M_SceneStatic.bars').data)
    waves = original_waves(original_bank)
    boot = (botw / 'Pack/Bootup.pack').read_bytes()
    if boot[:4] == b'Yaz0':
        boot = bytes(oead.yaz0.decompress(boot))
    original_slink = bytes(oead.Sarc(boot).get_file('SLink2/SLink2DB.sbslnk').data)
    if original_slink[:4] == b'Yaz0':
        original_slink = bytes(oead.yaz0.decompress(original_slink))
    z = TotkZstd(clean)
    native_slink = z.decompress(clean / SLINK)
    original_path, native_path = output / 'original.sbslnk', output / 'stock.bslnk'
    original_path.write_bytes(original_slink)
    native_path.write_bytes(native_slink)
    names = output / 'users.txt'
    names.write_text('RemoteBomb\nBarrelBomb\n' + USER + '\n')
    original_text, native_text = output / 'original.txt', output / 'stock.txt'
    transcode(xlink, original_path, original_text, 'UKing', names)
    transcode(xlink, native_path, native_text, 'EXKing', names)
    old = native_text.read_text(encoding='utf8')
    if USER in user_blocks(old):
        raise ValueError('Clean SLink input already contains custom user')
    custom = bomb_user(original_text.read_text(encoding='utf8'), old)
    (output / 'user.txt').write_text(custom + '\n', encoding='utf8')
    candidate_text, candidate_raw = output / 'candidate.txt', output / 'candidate.bslnk'
    candidate_text.write_text(old.rstrip()[:-1] + custom + '\n}\n', encoding='utf8')
    transcode(xlink, candidate_text, candidate_raw, 'EXKing', names)
    roundtrip = output / 'candidate-roundtrip.txt'
    transcode(xlink, candidate_raw, roundtrip, 'EXKing', names)
    decoded = roundtrip.read_text(encoding='utf8')
    if old.split('Users {', 1)[0] != decoded.split('Users {', 1)[0]:
        raise ValueError('Native XLink parameter definitions changed')
    before, after = user_blocks(old), user_blocks(decoded)
    if set(after) - set(before) != {USER} or any(after.get(name) != data for name, data in before.items()):
        raise ValueError('Existing native SLink users changed')
    stock_bank = read_bank(z.decompress(clean / BANK))
    additions = tuple(Addition(PREFIX + name, waves[name][0], 1., loop=waves[name][1]['loop']) for name in NAMES)
    bank = append_assets(stock_bank, additions, 'Sys_HeartRecovery_00')
    verify_preserved(stock_bank, read_bank(bank))
    floors = dict(line.split('\t') for line in actor_forces.read_text().splitlines() if line)
    resources = ((BANK, bank), (SLINK, candidate_raw.read_bytes()))
    table = z.decompress(clean / 'System/Resource/ResourceSizeTable.Product.121.rsizetable.zs')
    for path, raw in resources:
        destination = output / 'romfs' / path
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(z.compress(raw, destination.name))
        if z.decompress(destination) != raw:
            raise ValueError('Compressed audio round trip failed: ' + path)
        clean_size = len(z.decompress(clean / path))
        floors[path[:-3]] = max(len(raw)+65536, parser_budget(table, path[:-3])+max(0, len(raw)-clean_size)*10+65536)
    receipt = {'source_version': 'BotW 1.5.0', 'scene_bank_sha256': digest(original_bank),
               'original_slink_sha256': digest(original_slink), 'native_slink_sha256': digest(native_slink),
               'xlink_sha256': XLINK_SHA256, 'waves': {name: waves[name][1] for name in NAMES},
               'stock_bank_entries_preserved': len(stock_bank.entries), 'stock_users_preserved': len(before),
               'custom_user': USER, 'restbl_floors': {key: int(value) for key, value in floors.items()},
               'payloads': {path: digest((output/'romfs'/path).read_bytes()) for path, _ in resources},
               'native_adaptations': 'native Actor/Chemical routing and distance attenuation; inner blast/cancel caller parity open'}
    (output / 'audio-receipt.json').write_text(json.dumps(receipt, indent=2)+'\n')
    (output / 'restbl-forces.tsv').write_text(''.join(f'{key}\t{value}\n' for key, value in sorted(floors.items())))
    print(f'Imported {len(waves)} original cues; preserved {len(before)} native users and {len(stock_bank.entries)} stock bank entries.')
    return receipt


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--botw-romfs', required=True, type=Path)
    parser.add_argument('--totk-romfs', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--xlink', required=True, type=Path)
    parser.add_argument('--actor-forces', required=True, type=Path)
    args = parser.parse_args()
    build(args.botw_romfs, args.totk_romfs, args.output, args.xlink, args.actor_forces)
