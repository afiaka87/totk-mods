# SPDX-License-Identifier: MIT
# Copyright (c) Clay Mullis
"""Merge independent edits against clean resources, refusing ambiguous ownership."""

from __future__ import annotations

import copy
import re
import struct
from pathlib import Path

import byml
import oead


def indexed(rows, key, path):
    result = {row[key]: row for row in rows}
    if len(result) != len(rows):
        raise ValueError(f'{path}: duplicate {key}')
    return result


def merge_values(clean, members, path='root'):
    changed = {name: value for name, value in members.items() if value != clean}
    if not changed:
        return copy.deepcopy(clean)
    if isinstance(clean, dict) and all(isinstance(value, dict) for value in changed.values()):
        if any(clean.keys() - value.keys() for value in changed.values()):
            raise ValueError(f'{path}: deletion is not supported')
        result = {}
        for key in sorted(set(clean).union(*(value.keys() for value in changed.values()))):
            present = {name: value[key] for name, value in changed.items() if key in value}
            result[key] = (merge_values(clean[key], present, f'{path}/{key}') if key in clean
                           else agree(present, f'{path}/{key}'))
        return result
    if isinstance(clean, list) and all(isinstance(value, list) for value in changed.values()):
        all_rows = clean + [row for value in changed.values() for row in value]
        for key in ('__RowId', 'Hash'):
            if all_rows and all(isinstance(row, dict) and key in row for row in all_rows):
                base = indexed(clean, key, path)
                edits = {name: indexed(value, key, path) for name, value in changed.items()}
                merged = merge_values(base, edits, path)
                # Keep the native order and append private entries in deterministic order.
                return [merged[row[key]] for row in clean] + [
                    merged[identity] for identity in sorted(merged.keys() - base.keys())]
    return agree(changed, path)


def agree(values, path):
    first = next(iter(values.values()))
    if any(value != first for value in values.values()):
        raise ValueError(f'{path}: conflicting edits from {", ".join(values)}')
    return copy.deepcopy(first)


def encode_byml(value, original):
    if original[:2] not in (b'BY', b'YB'):
        raise ValueError('expected a BYML header')
    big_endian = original[:2] == b'BY'
    version = int.from_bytes(original[2:4], 'big' if big_endian else 'little')
    raw = byml.Writer(value, be=big_endian, version=version).get_bytes()
    if raw[:4] != original[:4] or byml.Byml(raw).parse() != value:
        raise ValueError('merged BYML does not round-trip')
    return raw


def merge_byml(clean, members):
    base = byml.Byml(clean).parse()
    values = {name: byml.Byml(raw).parse() for name, raw in members.items()}
    return encode_byml(merge_values(base, values), clean)


def saved_metadata(clean, data):
    """Extend native save allocations for new scalar flags and additive struct registries."""
    metadata = copy.deepcopy(clean['MetaData'])
    for kind, entries in data.items():
        base = indexed(clean['Data'].get(kind, []), 'Hash', kind)
        current = indexed(entries, 'Hash', kind)
        if base.keys() - current.keys():
            raise ValueError(f'{kind}: native save flags removed')
        for identity, original in base.items():
            row = current[identity]
            if row == original:
                continue
            if kind != 'Struct' or {k: v for k, v in row.items() if k != 'DefaultValue'} != {
                    k: v for k, v in original.items() if k != 'DefaultValue'}:
                raise ValueError(f'{kind}/{identity}: native save flag changed')
            before = indexed(original['DefaultValue'], 'Hash', f'Struct/{identity}')
            after = indexed(row['DefaultValue'], 'Hash', f'Struct/{identity}')
            if any(after.get(key) != value for key, value in before.items()):
                raise ValueError(f'Struct/{identity}: native registry entry changed')
        for identity in current.keys() - base.keys():
            if kind == 'Struct':
                continue
            if kind not in ('Bool', 'Int', 'UInt', 'Float', 'Enum'):
                raise ValueError(f'{kind}: new save type requires a verified allocation recipe')
            row = current[identity]
            index = row['SaveFileIndex']
            if index < -1 or index >= len(metadata['SaveDirectory']):
                raise ValueError(f'{kind}/{identity}: invalid save file index')
            for key in ('AllDataSaveOffset', 'AllDataSaveSize'):
                metadata[key] = type(metadata[key])(int(metadata[key]) + 8)
            if index >= 0:
                if not metadata['SaveDirectory'][index]:
                    raise ValueError(f'{kind}/{identity}: unused save directory')
                for key in ('SaveDataOffsetPos', 'SaveDataSize'):
                    old = metadata[key][index]
                    metadata[key][index] = type(old)(int(old) + 8)
    return metadata


def merge_gamedata(clean, members):
    base = byml.Byml(clean).parse()
    values = {name: byml.Byml(raw).parse() for name, raw in members.items()}
    capacity = {'AllDataSaveOffset', 'AllDataSaveSize', 'SaveDataOffsetPos', 'SaveDataSize'}
    for name, value in values.items():
        if set(value) != set(base) or {k: v for k, v in value['MetaData'].items() if k not in capacity} != {
                k: v for k, v in base['MetaData'].items() if k not in capacity}:
            raise ValueError(f'{name}: unsupported GameData metadata change')
    data = merge_values(base['Data'], {name: value['Data'] for name, value in values.items()}, 'Data')
    result = copy.deepcopy(base)
    result['Data'] = data
    result['MetaData'] = saved_metadata(base, data)
    return encode_byml(result, clean)


def read_msbt(raw):
    if raw[:8] != b'MsgStdBn' or raw[8:10] != b'\xff\xfe' or raw[12] != 1:
        raise ValueError('expected little-endian UTF-16 MSBT')
    sections, offset = {}, 32
    for _ in range(struct.unpack_from('<H', raw, 14)[0]):
        size = struct.unpack_from('<I', raw, offset + 4)[0]
        sections[raw[offset:offset + 4]] = raw[offset + 16:offset + 16 + size]
        offset += 16 + (size + 15) // 16 * 16
    if list(sections) != [b'LBL1', b'TXT2']:
        raise ValueError('changed MSBT requires only LBL1/TXT2; other members stay byte-identical')
    labels, text = sections[b'LBL1'], sections[b'TXT2']
    count = struct.unpack_from('<I', text)[0]
    offsets = list(struct.unpack_from(f'<{count}I', text, 4)) + [len(text)]
    strings = [text[a:b] for a, b in zip(offsets, offsets[1:])]
    group_count = struct.unpack_from('<I', labels)[0]
    groups, entries = [], {}
    for group in range(group_count):
        count, cursor = struct.unpack_from('<II', labels, 4 + group * 8)
        names = []
        for _ in range(count):
            length = labels[cursor]
            name = labels[cursor + 1:cursor + 1 + length].decode('utf-8')
            index = struct.unpack_from('<I', labels, cursor + 1 + length)[0]
            if name in entries:
                raise ValueError(f'duplicate MSBT label {name}')
            entries[name] = strings[index]
            names.append((name, index))
            cursor += length + 5
        groups.append(names)
    return groups, strings, entries


def append_msbt(clean, additions):
    groups, strings, entries = read_msbt(clean)
    if entries.keys() & additions.keys():
        raise ValueError('cannot replace stock inventory text')
    for name, value in sorted(additions.items()):
        hashed = 0
        for byte in name.encode('utf-8'):
            hashed = (hashed * 0x492 + byte) & 0xffffffff
        groups[hashed % len(groups)].append((name, len(strings)))
        strings.append(value)
    headers, labels = [], bytearray()
    for group in groups:
        headers.append(struct.pack('<II', len(group), 4 + len(groups) * 8 + len(labels)))
        for name, index in group:
            label = name.encode('utf-8')
            labels.extend(bytes([len(label)]) + label + struct.pack('<I', index))
    lbl = struct.pack('<I', len(groups)) + b''.join(headers) + labels
    offsets, cursor = [], 4 + 4 * len(strings)
    for value in strings:
        offsets.append(cursor)
        cursor += len(value)
    txt = struct.pack('<I', len(strings)) + struct.pack(f'<{len(strings)}I', *offsets) + b''.join(strings)
    body = b''.join(name + struct.pack('<I', len(data)) + b'\0' * 8 + data + b'\xab' * (-len(data) % 16)
                    for name, data in ((b'LBL1', lbl), (b'TXT2', txt)))
    header = bytearray(clean[:32])
    struct.pack_into('<I', header, 18, len(body) + 32)
    raw = bytes(header) + body
    if read_msbt(raw)[2] != entries | additions:
        raise ValueError('merged MSBT changes message bytes')
    return raw


def merge_msbt(clean, members):
    base = read_msbt(clean)[2]
    additions = {}
    for name, raw in members.items():
        if raw[:18] + raw[22:32] != clean[:18] + clean[22:32]:
            raise ValueError(f'{name}: MSBT header changed')
        entries = read_msbt(raw)[2]
        if any(entries.get(label) != value for label, value in base.items()):
            raise ValueError(f'{name}: stock MSBT message changed or removed')
        for label in entries.keys() - base.keys():
            if label in additions and additions[label] != entries[label]:
                raise ValueError(f'{name}: conflicting message {label}')
            additions[label] = entries[label]
    return append_msbt(clean, additions)


def sarc_files(raw):
    return {entry.name: bytes(entry.data) for entry in oead.Sarc(raw).get_files()}


def write_sarc(files):
    writer = oead.SarcWriter(oead.Endianness.Little)
    for name, raw in files.items():
        writer.files[name] = raw
    raw = bytes(writer.write()[1])
    if sarc_files(raw) != files:
        raise ValueError('merged archive does not round-trip')
    return raw


def merge_mals(clean, members):
    base = sarc_files(clean)
    archives = {name: sarc_files(raw) for name, raw in members.items()}
    if any(base.keys() - files.keys() for files in archives.values()):
        raise ValueError('Mals input removes stock files')
    merged = dict(base)
    for path in sorted(set(base).union(*(files.keys() for files in archives.values()))):
        edits = {name: files[path] for name, files in archives.items()
                 if path in files and files[path] != base.get(path)}
        if not edits:
            continue
        if path in base and path.endswith('.msbt'):
            merged[path] = merge_msbt(base[path], edits)
        else:
            merged[path] = agree(edits, path)
    return write_sarc(merged)


def merge_ui_sound_calls(clean, members, block):
    original = block(clean,r'^    AssetCallTables \{')
    def calls(text):
        return {m[1]:block(text,r'^      '+re.escape(m[1])+r' \{')
                for m in re.finditer(r'^      ([^\n{}]+) \{',text,re.MULTILINE)}
    base, additions = calls(original), {}
    for owner,user in members.items():
        table = block(user,r'^    AssetCallTables \{')
        if user.replace(table,original,1) != clean:
            raise ValueError(f'{owner}: non-call UI sound settings changed')
        entries = calls(table)
        if any(entries.get(key) != value for key,value in base.items()):
            raise ValueError(f'{owner}: stock UI sound call changed or removed')
        for key in entries.keys()-base.keys():
            if key in additions and additions[key] != entries[key]:
                raise ValueError(f'{owner}: conflicting UI sound call {key}')
            additions[key] = entries[key]
    combined = original.rsplit('\n',1)[0]+'\n'+''.join(additions[key]+'\n' for key in sorted(additions))+'    }'
    return clean.replace(original,combined,1)


def merge_elink_text(clean, members, user_blocks, sound_block=None):
    base = user_blocks(clean)
    prefix = clean.split('Users {', 1)[0]
    additions = {}
    ui_edits = {}
    for name, text in members.items():
        users = user_blocks(text)
        if text.split('Users {', 1)[0] != prefix:
            raise ValueError(f'{name}: effect parameter definitions changed')
        if any(users.get(user) != block for user, block in base.items()
               if not (sound_block and user == 'UI_GlobalSound')):
            raise ValueError(f'{name}: stock effect user changed or removed')
        if sound_block:
            ui_edits[name] = users['UI_GlobalSound']
        for user in users.keys() - base.keys():
            if user in additions and additions[user] != users[user]:
                raise ValueError(f'{name}: conflicting effect user {user}')
            additions[user] = users[user]
    if sound_block:
        merged = merge_ui_sound_calls(base['UI_GlobalSound'],ui_edits,sound_block)
        clean = clean.replace(base['UI_GlobalSound'],merged,1)
    return clean.rstrip()[:-1] + ''.join(additions[user] + '\n' for user in sorted(additions)) + '}\n'


def merge_elink(clean, members, work: Path, executable: Path, names: Path, xlink, sound=False):
    work.mkdir(parents=True, exist_ok=True)
    texts = {}
    for index, (owner, raw) in enumerate({'clean': clean, **members}.items()):
        binary, text = work / f'{index}.belnk', work / f'{index}.txt'
        binary.write_bytes(raw)
        xlink.transcode(executable, binary, text, 'EXKing', names)
        texts[owner] = text.read_text(encoding='utf-8')
    clean_text = texts.pop('clean')
    candidate = merge_elink_text(clean_text, texts, xlink.user_blocks, xlink.block if sound else None)
    source, binary, restored = work / 'merged.txt', work / 'merged.belnk', work / 'restored.txt'
    source.write_text(candidate, encoding='utf-8')
    xlink.transcode(executable, source, binary, 'EXKing', names)
    xlink.transcode(executable, binary, restored, 'EXKing', names)
    back = restored.read_text(encoding='utf-8')
    if (xlink.user_blocks(back) != xlink.user_blocks(candidate)
            or back.split('Users {', 1)[0] != clean_text.split('Users {', 1)[0]):
        raise ValueError('ELink round-trip changed users or parameter definitions')
    return binary.read_bytes()
