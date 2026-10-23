# SPDX-License-Identifier: MIT
"""Fail packaging on missing, stale, corrupt or oversized private locale inputs."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import zlib


def verify(directory):
    receipt=json.loads((directory/'locale-receipt.json').read_text())
    packs=receipt['packs']
    locales={'JPja','USen','USes','USfr','EUen','EUes','EUfr','EUde','EUit','EUnl','EUru','KRko','CNzh','TWzh'}
    expected={f'{version}/{locale}.bin' for version in ('121','140','141','142')
              for locale in locales | ({'USpt'} if version!='121' else set())}
    actual={p.relative_to(directory/'Locale').as_posix() for p in (directory/'Locale').rglob('*') if p.is_file()}
    if set(packs)!=expected or actual!=expected:
        raise ValueError('Missing or unexpected locale pack/version')
    root=Path(__file__).resolve().parents[4]
    for name,digest in receipt['inputs'].items():
        if hashlib.sha256((root/name).read_bytes()).hexdigest()!=digest:
            raise ValueError(f'Stale locale inputs: {name}; rerun bake_locales.py')
    catalog=(directory/'ActorCatalog.hpp').read_text()
    entries=re.findall(r'\{0x([0-9a-f]+)u,(\d+),(\d+),(\d+),0\}',catalog)
    raw=b''.join(struct.pack('<IBBBB',int(h,16),int(c),int(v),int(i),0) for h,c,v,i in entries)
    if len(entries)!=2053 or zlib.crc32(raw)!=receipt['catalog_crc']:
        raise ValueError('Actor catalog does not match locale packs')
    total=0
    for name in sorted(expected):
        data=bytearray((directory/'Locale'/name).read_bytes())
        if hashlib.sha256(data).hexdigest()!=packs[name]['sha256'] or len(data)>163840:
            raise ValueError(f'Corrupt/oversized locale pack: {name}')
        stored=struct.unpack_from('<I',data,12)[0]
        struct.pack_into('<I',data,12,0)
        if (data[:4]!=b'ZSL1' or zlib.crc32(data)!=stored or
            struct.unpack_from('<I',data,8)[0]!=len(data) or
            struct.unpack_from('<I',data,16)[0]!=receipt['catalog_crc'] or
            data[72:76].decode()!=Path(name).stem):
            raise ValueError(f'Invalid locale header: {name}')
        total+=len(data)
    print(f'Locale packs OK: {len(expected)} packs, {total} disk bytes, fixed 163840-byte resident arena')


if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--directory',type=Path,required=True)
    verify(parser.parse_args().directory)
