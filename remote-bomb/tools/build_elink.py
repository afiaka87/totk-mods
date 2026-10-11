# SPDX-License-Identifier: MIT
# Copyright (c) Clay Mullis
"""Preserve native bomb interaction effects and give the bomb the Time Bomb's own explosion."""
import argparse
import hashlib
import json
import re
import sys
from pathlib import Path
import byml
from import_audio import XLINK_SHA256, block, parser_budget, transcode, user_blocks

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT/'scripts'))
from totk_mod_tools.zstd_helper import TotkZstd

RESOURCE = 'ELink2/elink2.Product.110.belnk.zs'
BANKS = 'Effect/EffectFileInfo.Product.110.Nin_NX_NVN.byml.zs'
USER = 'TotkRemoteBomb'
# The Time Bomb's effect user; its explosion exists only in the effect bank of the same name.
TIMER_BOMB = 'SpObj_TimerBomb_A_01'
TIMER_EXPLOSION = 'Obj_ZonauTimerBomb_Explosion'


def timer_bomb_explosion(timer_bomb: str) -> str:
    """The game looks up an exploding object's own user before the shared Chemical user."""
    call = block(timer_bomb, r'^      Chemical_Explosion\[0x[0-9a-f]+\] \{')
    if (call.count(f'RuntimeAssetName = "{TIMER_EXPLOSION}"') != 1 or 'RuntimeAssetName' in call.replace(
            f'RuntimeAssetName = "{TIMER_EXPLOSION}"', '') or re.search(r'^ +(Red|Green|Blue) = ', call, re.MULTILINE)):
        raise ValueError('Time Bomb explosion route changed')
    return call


def bank_table(z: TotkZstd, source: Path, output: Path) -> tuple[bytes, bytes]:
    """Map the private effect user to the Time Bomb's effect bank so its explosion is loaded."""
    stock = z.decompress(source/BANKS)
    data = byml.Byml(stock).parse()
    banks = data['BinaryDict']
    if USER in banks or banks.get(TIMER_BOMB) != TIMER_BOMB or TIMER_BOMB not in data['EsetbList']:
        raise ValueError('Unexpected effect bank table')
    banks[USER] = TIMER_BOMB
    payload = byml.Writer(data, version=int.from_bytes(stock[2:4], 'little')).get_bytes()
    if payload[:4] != stock[:4] or byml.Byml(payload).parse() != data:
        raise ValueError('Effect bank table round trip failed')
    destination = output/'romfs'/BANKS
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(z.compress(payload, destination.name))
    if z.decompress(destination) != payload:
        raise ValueError('Effect bank table compression round trip failed')
    return stock, payload


def build(source: Path, output: Path, xlink: Path, previous_floors: Path) -> dict:
    if hashlib.sha256(xlink.read_bytes()).hexdigest() != XLINK_SHA256:
        raise ValueError('Unreviewed XLink tool')
    output.mkdir(parents=True, exist_ok=True)
    z = TotkZstd(source)
    stock = z.decompress(source/RESOURCE)
    stock_path = output/'stock.belnk'
    stock_path.write_bytes(stock)
    names = output/'users.txt'
    names.write_text('BarrelBomb\n'+USER+'\n')
    stock_text = output/'stock.txt'
    transcode(xlink, stock_path, stock_text, 'EXKing', names)
    original = stock_text.read_text(encoding='utf-8')
    users = user_blocks(original)
    if USER in users or 'RuntimeAssetName = "@Blank"' not in original:
        raise ValueError('Expected clean native users and an established blank-effect route')
    donor = users['BarrelBomb']
    call = block(donor, r'^      Display_Explosion\[0x[0-9a-f]+\] \{')
    if call.count('RuntimeAssetName = "Chm_Explosion"') != 1:
        raise ValueError('Native bomb explosion route changed')
    if re.search(r'^      Chemical_Explosion\[', donor, re.MULTILINE):
        raise ValueError('Native bomb already overrides the shared explosion')
    blank = call.replace('RuntimeAssetName = "Chm_Explosion"', 'RuntimeAssetName = "@Blank"')
    custom = donor.replace('  BarrelBomb {', '  '+USER+' {', 1).replace(
        call, blank+'\n'+timer_bomb_explosion(users[TIMER_BOMB]), 1)
    candidate_text, candidate_binary = output/'candidate.txt', output/'candidate.belnk'
    candidate_text.write_text(original.rstrip()[:-1]+custom+'\n}\n', encoding='utf-8')
    transcode(xlink, candidate_text, candidate_binary, 'EXKing', names)
    roundtrip = output/'candidate-roundtrip.txt'
    transcode(xlink, candidate_binary, roundtrip, 'EXKing', names)
    restored = roundtrip.read_text(encoding='utf-8')
    after = user_blocks(restored)
    if original.split('Users {', 1)[0] != restored.split('Users {', 1)[0]:
        raise ValueError('Native effect parameter definitions changed')
    if set(after)-set(users) != {USER} or any(after.get(name) != value for name, value in users.items()):
        raise ValueError('Existing native effect users changed')
    if after[USER] != custom:
        raise ValueError('Private effect user changed during conversion')
    payload = candidate_binary.read_bytes()
    destination = output/'romfs'/RESOURCE
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(z.compress(payload, destination.name))
    if z.decompress(destination) != payload:
        raise ValueError('Native effect compression round trip failed')
    table = z.decompress(source/'System/Resource/ResourceSizeTable.Product.121.rsizetable.zs')
    floors = dict(line.split('\t') for line in previous_floors.read_text().splitlines() if line)
    floors[RESOURCE[:-3]] = max(len(payload)+65536,
        parser_budget(table, RESOURCE[:-3])+max(0, len(payload)-len(stock))*10+65536)
    bank_stock, banks = bank_table(z, source, output)
    floors[BANKS[:-3]] = max(len(banks)+65536,
        parser_budget(table, BANKS[:-3])+max(0, len(banks)-len(bank_stock))*10+65536)
    (output/'restbl-forces.tsv').write_text(''.join(f'{key}\t{value}\n' for key, value in sorted(floors.items())))
    receipt = dict(stock_sha256=hashlib.sha256(stock).hexdigest(),
                   payload_sha256=hashlib.sha256(destination.read_bytes()).hexdigest(),
                   xlink_sha256=XLINK_SHA256, users_preserved=len(users), custom_user=USER,
                   suppression='Only private Display_Explosion; other actor effect keys preserved',
                   explosion=f'{TIMER_BOMB} Chemical_Explosion ({TIMER_EXPLOSION}), unchanged, private user only',
                   effect_bank={USER: TIMER_BOMB},
                   effect_bank_sha256=hashlib.sha256((output/'romfs'/BANKS).read_bytes()).hexdigest(),
                   restbl_floors={key: int(value) for key, value in floors.items()})
    (output/'elink-receipt.json').write_text(json.dumps(receipt, indent=2)+'\n')
    print(f"Preserved {len(users)} native effect users; private explosion is the Time Bomb's")
    return receipt


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--xlink', required=True, type=Path)
    parser.add_argument('--previous-floors', required=True, type=Path)
    args = parser.parse_args()
    build(args.source, args.output, args.xlink, args.previous_floors)
