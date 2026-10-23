# SPDX-License-Identifier: MIT
# Copyright (c) Clay Mullis
"""Extract the exact inspected game-data input for the experimental native sphere."""
import argparse
import hashlib
from pathlib import Path
import stat
import sys
import zipfile

SHA256 = 'b5794c7c0ae2e3b43ef06239585773d316a95e1b091086a63fdbaca70ddd712a'

def extract(source, output):
    if hashlib.sha256(source.read_bytes()).hexdigest() != SHA256:
        raise ValueError('Native sphere input differs from the inspected ZIP')
    with zipfile.ZipFile(source) as archive:
        if len(archive.infolist()) != 18 or sum(i.file_size for i in archive.infolist()) > 1024*1024:
            raise ValueError('Unexpected archive dimensions')
        for entry in archive.infolist():
            name=entry.filename.replace('\\','/')
            if name.startswith('/') or '..' in name.split('/') or ':' in name or stat.S_ISLNK(entry.external_attr>>16):
                raise ValueError('Unsafe archive path')
            if entry.is_dir(): continue
            target=output/name
            target.parent.mkdir(parents=True,exist_ok=True)
            target.write_bytes(archive.read(entry))

if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--archive',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--romfs',type=Path,required=True)
    args=parser.parse_args()
    extract(args.archive,args.output)
    sys.path.insert(0,str(Path(__file__).resolve().parents[4]/'scripts'))
    from totk_mod_tools.zstd_helper import TotkZstd
    animation=args.output/'Model/SpObj_TimerBomb_A.anim.bfres.zs'
    animation.with_suffix('').write_bytes(TotkZstd(args.romfs).decompress(animation))
