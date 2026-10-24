#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) Clay Mullis
"""Add explicit Champion sound calls without runtime sound-table substitution."""
import hashlib
import re
import zlib
from import_audio import SLINK, XLINK_SHA256, block, transcode, user_blocks


def append_calls(user, cues):
    donor = block(user, r'^      AmiiboMarker_OK\[0x[0-9a-f]+\] \{')
    identifiers = set(re.findall(r'\[0x([0-9a-f]+)\]',user))
    calls = block(user, r'^    AssetCallTables \{')
    extra = []
    for cue, receipt in cues.items():
        name = receipt['asset']
        if name in user:
            raise ValueError(f'Champion sound already present: {name}')
        identifier = f'{zlib.crc32(name.encode()):08x}'
        if identifier in identifiers:
            raise ValueError(f'private call GUID collision: {name}')
        identifiers.add(identifier)
        call = re.sub(r'AmiiboMarker_OK\[0x[0-9a-f]+\]',f'{name}[0x{identifier}]',donor)
        call = re.sub(r'(RuntimeAssetName|AssetName) = "[^"]+"',lambda m: f'{m[1]} = "{name}"',call)
        # These activation voices must remain audible during their native impact/revival mix.
        if ':NV_Custom_NoActor_' in receipt['source'] and cue not in ('MiphaUsed', 'DarukUsed'):
            call = call.replace('GroupName = "SystemInsideWorld"','GroupName = "NpcVoice"')
            call = call.replace('Priority = 0.899999976','Priority = 0.600000024')
        extra.append(call)
    return user.replace(calls,calls.rsplit('\n',1)[0]+'\n'+'\n'.join(extra)+'\n    }',1)


def build_routes(z, romfs, output, executable, cues):
    if hashlib.sha256(executable.read_bytes()).hexdigest() != XLINK_SHA256:
        raise ValueError('unreviewed XLink converter')
    work = output / 'audio/slink'
    work.mkdir(parents=True,exist_ok=True)
    names = work / 'names.txt'
    names.write_text('UI_GlobalSound\nTotkRemoteBomb\n',encoding='utf-8')
    raw = z.decompress(romfs / SLINK)
    binary, text = work / 'stock.bslnk', work / 'stock.txt'
    binary.write_bytes(raw)
    transcode(executable,binary,text,'EXKing',names)
    clean = text.read_text(encoding='utf-8')
    users = user_blocks(clean)
    changed = append_calls(users['UI_GlobalSound'],cues)
    candidate = clean.replace(users['UI_GlobalSound'],changed,1)
    source, binary, restored = work / 'candidate.txt', work / 'candidate.bslnk', work / 'restored.txt'
    source.write_text(candidate,encoding='utf-8')
    transcode(executable,source,binary,'EXKing',names)
    transcode(executable,binary,restored,'EXKing',names)
    back = restored.read_text(encoding='utf-8')
    if (user_blocks(back) != users | {'UI_GlobalSound':changed} or
            back.split('Users {',1)[0] != clean.split('Users {',1)[0]):
        raise ValueError('Champion SLink changed native users or definitions')
    return raw,binary.read_bytes()
