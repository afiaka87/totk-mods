# SPDX-License-Identifier: MIT
# Copyright (c) Clay Mullis
"""Encode the owned cube maps in the accepted native sphere's TXTG layout."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys

import numpy as np
from PIL import Image
import texture2ddecoder
import zstandard as zstd

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0,str(ROOT/'scripts'))
from totk_mod_tools.textures.swizzle import swizzle, deswizzle


def build(args):
    dest=args.output/'TexToGo'
    dest.mkdir(parents=True,exist_ok=True)
    scratch=args.output/'encoded'; scratch.mkdir(exist_ok=True)
    proof=[]
    for suffix in ('Alb','Emm','Nrm','Spm'):
        template=args.input/'TexToGo'/f'RemoteBombBall_{suffix}.txtg'
        shutil.copy2(template,dest/template.name)
        source=args.reference/f'Item_RemoteBombCube_{suffix}.png'
        image=Image.open(source).convert('RGBA')
        header=bytearray(template.read_bytes()[:0x50])
        if header[4:8]!=b'6PK0' or struct.unpack_from('<H',header,0x3c)[0]!=0x101:
            raise ValueError('Expected accepted ASTC4x4 TXTG template')
        w,h=image.size
        levels=max(w,h).bit_length()
        struct.pack_into('<HHHB',header,8,w,h,1,levels)
        name=f'TotkNativeCube_{suffix}'
        header[0x1c:0x3c]=hashlib.sha256(name.encode()).digest()
        surfaces=[]
        for mip in range(levels):
            mw,mh=max(1,w>>mip),max(1,h>>mip)
            resized=image if mip==0 else image.resize((mw,mh),Image.Resampling.LANCZOS)
            png=scratch/f'{suffix}-{mip}.png'; astc=png.with_suffix('.astc')
            resized.save(png)
            subprocess.run([str(args.encoder),'-cl',str(png),str(astc),'4x4','-thorough','-silent','-j','2'],check=True)
            encoded=astc.read_bytes()
            if encoded[:7]!=bytes.fromhex('13aba15c040401'):
                raise ValueError('Unexpected ASTC container')
            blocks=encoded[16:]; ew,eh=(mw+3)//4,(mh+3)//4
            bh=min(16,1 << max(0,((eh+7)//8-1).bit_length()))
            tiled=swizzle(blocks,ew,eh,16,bh)
            if bytes(deswizzle(tiled,ew,eh,16,bh))!=blocks:
                raise ValueError('Native texture swizzle round trip failed')
            surfaces.append(zstd.ZstdCompressor(level=20).compress(tiled))
            if mip==0:
                rgba=np.frombuffer(texture2ddecoder.decode_astc(blocks,mw,mh,4,4),dtype=np.uint8).reshape(mh,mw,4)[:,:,[2,1,0,3]]
                error=float(np.abs(rgba.astype(np.int16)-np.array(image).astype(np.int16)).mean())
                if error>5: raise ValueError(f'{suffix}: excessive texture error {error}')
                Image.fromarray(rgba).save(scratch/f'{suffix}-decoded.png')
        payload=header+b''.join(struct.pack('<HBB',0,mip,1) for mip in range(levels))
        payload+=b''.join(struct.pack('<II',len(data),6) for data in surfaces)+b''.join(surfaces)
        target=dest/f'{name}.txtg'; target.write_bytes(payload)
        proof.append(dict(name=name,width=w,height=h,mips=levels,mean_error=error,
                          source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                          sha256=hashlib.sha256(payload).hexdigest()))
    (args.output/'texture-receipt.json').write_text(json.dumps(proof,indent=2)+'\n')
    print('Encoded four native cube maps with full mip chains; swizzle and decoded pixels checked')


if __name__=='__main__':
    parser=argparse.ArgumentParser()
    for key in ('input','output','reference','encoder'):parser.add_argument('--'+key,type=Path,required=True)
    build(parser.parse_args())
