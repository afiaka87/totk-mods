# SPDX-License-Identifier: MIT
# Copyright (c) Clay Mullis
"""Register native material sorting, discovery and inventory text from owned data."""
from __future__ import annotations
import copy
import hashlib
import struct
import byml
import oead
from build_actors import VARIANTS

def hash_name(text):
    data = text.encode()
    h = 0
    def rotate(n, k): return ((n << k) | (n >> (32-k))) & 0xffffffff
    for i in range(0, len(data)//4*4, 4):
        k = int.from_bytes(data[i:i+4], 'little')
        k = rotate(k*0xcc9e2d51 & 0xffffffff, 15)*0x1b873593 & 0xffffffff
        h = (rotate(h ^ k, 13)*5 + 0xe6546b64) & 0xffffffff
    k = int.from_bytes(data[len(data)//4*4:], 'little')
    if k:
        h ^= rotate(k*0xcc9e2d51 & 0xffffffff, 15)*0x1b873593 & 0xffffffff
    h ^= len(data)
    h ^= h >> 16; h = h*0x85ebca6b & 0xffffffff
    h ^= h >> 13; h = h*0xc2b2ae35 & 0xffffffff
    return h ^ (h >> 16)

def tag_rows(table):
    paths, tags = table['PathList'], table['TagList']
    bits = int.from_bytes(table['BitTable'], 'little')
    mask = (1 << len(tags))-1
    return [(paths[i*3:i*3+3], (bits >> (i*len(tags))) & mask) for i in range(len(paths)//3)]

def add_tags(table):
    rows = tag_rows(table)
    donor = next(bits for path, bits in rows if path[1] == 'BombFruit')
    names = table['TagList']
    for tag in ('Cook', 'CookForeign'):
        donor &= ~(1 << names.index(tag))
    original_count = len(rows)
    rank_stride = (original_count + 31)//32*32
    rank = int.from_bytes(table['RankTable'], 'little')
    rank_donor = next(i for i, (path, _) in enumerate(rows) if path[1] == 'BombFruit')
    rows = [(path, bits, i) for i, (path, bits) in enumerate(rows)]
    for actor, _, _ in VARIANTS:
        rows.append((['Work/Actor/', actor, '.engine__actor__ActorParam.gyml'], donor, rank_donor))
    rows.sort(key=lambda row: row[0][1])
    new_stride = (len(rows) + 31)//32*32
    new_rank = bytearray(len(names)*new_stride//8)
    for tag in range(len(names)):
        column = (rank >> (tag*rank_stride)) & ((1 << rank_stride)-1)
        for i, (_, _, old) in enumerate(rows):
            if (column >> old) & 1:
                bit = tag*new_stride+i
                new_rank[bit//8] |= 1 << (bit%8)
    table['PathList'] = [part for path, _, _ in rows for part in path]
    bits = sum(bits << (i*len(names)) for i, (_, bits, _) in enumerate(rows))
    table['BitTable'] = bits.to_bytes((len(rows)*len(names)+7)//8, 'little')
    table['RankTable'] = bytes(new_rank)
    return table

def add_discovery(data):
    native_bools = {r['Hash']:r for r in data['Data']['Bool']}
    added = []
    for map_hash in (0xADE0EF78, 0x34BF6C2D):
        registry = next(r for r in data['Data']['Struct'] if r['Hash'] == map_hash)
        original = copy.deepcopy(registry['DefaultValue'])
        donor = next(r for r in original if r['Hash'] == hash_name('BombFruit'))
        for actor, _, _ in VARIANTS:
            new_hash = hash_name(f'TotkRemoteBomb.{map_hash:08x}.{actor}')
            if new_hash in native_bools:
                raise ValueError('Custom GameData hash collision')
            row = copy.deepcopy(native_bools[donor['Value']])
            row['Hash'] = byml.UInt(new_hash)
            row['DefaultValue'] = False
            data['Data']['Bool'].append(row)
            registry['DefaultValue'].append({'Hash': byml.UInt(hash_name(actor)), 'Value': byml.UInt(new_hash)})
            added.append({'actor':actor,'registry':map_hash,'flag':new_hash})
        registry['DefaultValue'].sort(key=lambda row: int(row['Hash']))
    data['Data']['Bool'].sort(key=lambda row: int(row['Hash']))
    return data, added

def msbt_entries(raw):
    sections = []
    offset = 32
    for _ in range(struct.unpack_from('<H', raw, 14)[0]):
        size = struct.unpack_from('<I', raw, offset+4)[0]
        sections.append((raw[offset:offset+4], raw[offset+16:offset+16+size]))
        offset += 16 + (size+15)//16*16
    if [s[0] for s in sections] != [b'LBL1', b'TXT2']:
        raise ValueError('Unexpected inventory MSBT sections')
    lbl, txt = [s[1] for s in sections]
    groups = []
    for i in range(struct.unpack_from('<I', lbl)[0]):
        count, offset = struct.unpack_from('<II', lbl, 4+i*8)
        group = []
        for _ in range(count):
            length = lbl[offset]
            group.append((lbl[offset+1:offset+1+length].decode(), struct.unpack_from('<I', lbl, offset+1+length)[0]))
            offset += 5+length
        groups.append(group)
    count = struct.unpack_from('<I', txt)[0]
    offsets = list(struct.unpack_from(f'<{count}I', txt, 4))+[len(txt)]
    texts = [txt[offsets[i]:offsets[i+1]] for i in range(count)]
    return groups, texts

def add_messages(raw, entries):
    groups, texts = msbt_entries(raw)
    for label, text in entries.items():
        if any(name == label for group in groups for name, _ in group):
            raise ValueError('Custom message already exists')
        h = 0
        for value in label.encode(): h = (h*0x492+value) & 0xffffffff
        groups[h % len(groups)].append((label, len(texts)))
        texts.append(text.encode('utf-16le')+b'\0\0')
    def section(name, body):
        return name + struct.pack('<I', len(body)) + b'\0'*8 + body + b'\xab'*((-len(body)) % 16)
    table, labels = [], bytearray()
    start = 4+len(groups)*8
    for group in groups:
        table.append(struct.pack('<II', len(group), start+len(labels)))
        for name, index in group:
            labels.extend(bytes([len(name)])+name.encode()+struct.pack('<I', index))
    lbl = struct.pack('<I',len(groups))+b''.join(table)+labels
    offsets, start = [], 4+len(texts)*4
    for text in texts:
        offsets.append(start); start += len(text)
    txt = struct.pack('<I',len(texts))+struct.pack(f'<{len(texts)}I',*offsets)+b''.join(texts)
    header = bytearray(raw[:32])
    body = section(b'LBL1',lbl)+section(b'TXT2',txt)
    struct.pack_into('<I',header,18,len(header)+len(body))
    result = bytes(header)+body
    parsed, strings = msbt_entries(result)
    resolved = {name:strings[index].decode('utf-16le').rstrip('\0') for group in parsed for name,index in group}
    if any(resolved[name] != text for name, text in entries.items()):
        raise ValueError('MSBT label round trip failed')
    return result

def build_registration(romfs, output, z, budget):
    floors, discovery = {}, []
    for relative in ('RSDB/Tag.Product.121.rstbl.byml.zs','GameData/GameDataList.Product.110.byml.zs'):
        raw = z.decompress(romfs/relative)
        data = byml.Byml(raw).parse()
        if relative.startswith('RSDB'):
            data = add_tags(data)
        else:
            data, discovery = add_discovery(data)
        encoded = byml.Writer(data,version=7).get_bytes()
        path = output/'romfs'/relative
        path.parent.mkdir(parents=True,exist_ok=True)
        path.write_bytes(z.compress(encoded,path.name))
        if byml.Byml(z.decompress(path)).parse() != data:
            raise ValueError('Inventory registration round trip')
        floors[relative[:-3]] = max(budget(relative[:-3]),len(encoded)) + max(65536,(len(encoded)-len(raw))*10)
    mals = romfs/'Mals/USen.Product.121.sarc.zs'
    raw = z.decompress(mals)
    archive = oead.Sarc(raw)
    files = {e.name:bytes(e.data) for e in archive.get_files()}
    entries = {}
    for actor, form, damage in VARIANTS:
        name = 'Remote Bomb'+('+' if damage==24 else '')+f' ({form.capitalize()})'
        entries[actor+'_Name'] = name
        entries[actor+'_Caption'] = 'Hold or throw this rechargeable bomb, or\nattach it to an arrow. Tap L to detonate.\nReturns to Materials after recharging.'
    member = 'ActorMsg/PouchContent.msbt'
    if add_messages(files[member],{}) != files[member]:
        raise ValueError('Stock MSBT serializer is not byte-identical')
    files[member] = add_messages(files[member],entries)
    writer = oead.SarcWriter(oead.Endianness.Little)
    for name,value in files.items(): writer.files[name] = value
    encoded = bytes(writer.write()[1])
    path = output/'romfs/Mals'/mals.name
    path.parent.mkdir(parents=True,exist_ok=True)
    path.write_bytes(z.compress(encoded,path.name))
    reopened = {e.name:bytes(e.data) for e in oead.Sarc(z.decompress(path)).get_files()}
    if reopened != files:
        raise ValueError('Inventory Mals round trip')
    relative = 'Mals/'+mals.name[:-3]
    floors[relative] = max(budget(relative),len(encoded))+max(65536,(len(encoded)-len(raw))*4)
    mals_sha256 = hashlib.sha256(path.read_bytes()).hexdigest()
    for actor, _, _ in VARIANTS:
        relative = f'UI/Tex/Icon/{actor}.bntx.zs'
        path = output/'romfs'/relative
        raw = z.decompress(path)
        floors[relative[:-3]] = max(len(raw)*2+65536,budget('UI/Tex/Icon/BombFruit.bntx')+len(raw)*2)
    return {'restbl_floors':floors,'discovery':discovery,'messages':entries,
            'mals':mals.name,'mals_sha256':mals_sha256}
