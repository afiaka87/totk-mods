# SPDX-License-Identifier: MIT
# Copyright (c) Clay Mullis
"""Preserve the original owned Cryonis DSP streams in a private TotK bank."""
from __future__ import annotations
import argparse,hashlib,json,struct,sys,zlib
from pathlib import Path
import oead
ROOT=Path(__file__).resolve().parents[4]
sys.path.insert(0,str(ROOT/'scripts'))
sys.path.insert(0,str(ROOT/'framework/bridgefs/tools'))
from totk_mod_tools.zstd_helper import TotkZstd
from bridgefs_tools.audio_bank import Addition,append_assets,read_bank,verify_preserved
NAMES=('Sys_Item_IceMaker_Making','IceMakerBlock_TouchMaking','IceMakerBlock_TouchMaking_End','IceMakerBlock_Crush')
AMBIENCE='UltraHand_Base_lp_00'
def u32(data:bytes, offset:int)->int: return struct.unpack_from('<I',data,offset)[0]
def convert(wave:bytes)->tuple[bytes,dict]:
    if wave[:6]!=b'FWAV\xff\xfe' or u32(wave,12)!=len(wave): raise ValueError('Unsupported or truncated FWAV')
    info, data=u32(wave,24),u32(wave,36)
    if wave[info:info+4]!=b'INFO' or wave[data:data+4]!=b'DATA': raise ValueError('Missing FWAV sections')
    codec,loop=wave[info+8:info+10]; rate,count=u32(wave,info+12),u32(wave,info+20)
    table=info+28
    if codec!=2 or loop or u32(wave,table)!=1 or rate!=48000: raise ValueError('Expected original mono DSP one-shot at 48 kHz')
    channel=table+u32(wave,table+8)
    payload_offset=data+8+u32(wave,channel+4)
    coeff_offset=channel+u32(wave,channel+12)
    coeff=struct.unpack_from('<16h',wave,coeff_offset)
    predictor,h1,h2=struct.unpack_from('<Hhh',wave,coeff_offset+32)
    payload=wave[payload_offset:payload_offset+((count+13)//14)*8]
    if len(payload)!=((count+13)//14)*8: raise ValueError('Truncated DSP payload')
    if predictor!=payload[0]: raise ValueError('Initial DSP predictor mismatch')
    header=struct.pack('<4sHHIHH',b'BWAV',0xFEFF,1,zlib.crc32(payload),0,1)
    metadata=struct.pack('<HHIII16hIIIIIHhhH',1,2,rate,count,count,*coeff,0x80,0x80,1,0xFFFFFFFF,0,predictor,h1,h2,0)
    result=(header+metadata).ljust(0x80,b'\0')+payload
    # Conversion changes only the container: no decoding or lossy re-encoding.
    assert result[0x80:]==payload and struct.unpack_from('<16h',result,32)==coeff
    return result,{'samples':count,'rate':rate,'dsp_payload_sha256':hashlib.sha256(payload).hexdigest(),'coefficients_preserved':True,'initial_history_preserved':True}
def waves(original:bytes):
    if original[:4]!=b'BARS' or u32(original,4)!=len(original):raise ValueError('Invalid owned BARS')
    count=u32(original,12);found={}
    for i in range(count):
        meta,audio=struct.unpack_from('<II',original,16+count*4+i*8)
        start=meta+u32(original,meta+24)+8;name=original[start:].split(b'\0',1)[0].decode('ascii')
        if name in NAMES:found[name]=convert(original[audio:audio+u32(original,audio+12)])
    return found

def ambience(clean:Path)->tuple[bytes,dict]:
    # The UI donor is mono. Preserve the left DSP stream and its native loop
    # history verbatim, centering it instead of decoding/re-encoding stereo.
    source=(clean/f'Sound/Resource/Stream/{AMBIENCE}.bwav').read_bytes()
    if source[:8]!=b'BWAV\xff\xfe\x01\x00' or struct.unpack_from('<HH',source,12)!=(0,2):
        raise ValueError('Expected complete native stereo BWAV')
    fields=list(struct.unpack_from('<HHIII16hIIIIIHhhH',source,16))
    codec,pan,rate,count,current=fields[:5]
    start,end,loop_start=fields[22],fields[24],fields[25]
    if codec!=1 or rate!=32000 or current!=count or not 0<=loop_start<end<=count:
        raise ValueError('Unexpected native ambience codec/loop')
    payload=source[start:start+((count+13)//14)*8]
    if len(payload)!=((count+13)//14)*8:raise ValueError('Truncated ambience stream')
    fields[1]=2;fields[21]=fields[22]=0x80
    header=struct.pack('<4sHHIHH',b'BWAV',0xFEFF,1,zlib.crc32(payload),0,1)
    wave=(header+struct.pack('<HHIII16hIIIIIHhhH',*fields)).ljust(0x80,b'\0')+payload
    return wave,{'source':'TotK 1.2.1 '+AMBIENCE,'source_sha256':hashlib.sha256(source).hexdigest(),
        'samples':count,'rate':rate,'loop_start':loop_start,'loop_end':end,'channel':'left, centered',
        'dsp_payload_sha256':hashlib.sha256(payload).hexdigest(),'native_loop_history_preserved':True}

def build(botw:Path,clean:Path,output:Path):
    title=(botw/'Pack/TitleBG.pack').read_bytes()
    if title[:4]==b'Yaz0':title=bytes(oead.yaz0.decompress(title))
    pillar=bytes(oead.Sarc(title).get_file('Sound/Resource/Obj_IceMakerBlock.bars').data)
    ui=(botw/'Sound/Resource/M_UI.bars').read_bytes();found=waves(ui);found.update(waves(pillar))
    if set(found)!=set(NAMES):raise ValueError('Missing original Cryonis waveforms')
    codec=TotkZstd(clean);path='Sound/Resource/UI_Static.bars.zs';stock=read_bank(codec.decompress(clean/path))
    loop,loop_receipt=ambience(clean)
    additions=tuple(Addition('TotkCryonis_'+name,found[name][0],1.0) for name in NAMES)
    result=append_assets(stock,additions+(Addition('TotkCryonis_'+AMBIENCE,loop,1.0,loop=True),),'Sys_HeartRecovery_00')
    verify_preserved(stock,read_bank(result));dest=output/'romfs'/path;dest.parent.mkdir(parents=True,exist_ok=True)
    dest.write_bytes(codec.compress(result,dest.name))
    receipt={'source_version':'BotW 1.5.0','ui_sha256':hashlib.sha256(ui).hexdigest(),'pillar_bank_sha256':hashlib.sha256(pillar).hexdigest(),
        'waves':{name:found[name][1] for name in NAMES},'ambience':loop_receipt,'stock_entries_preserved':len(stock.entries),'raw_bytes':len(result),
        'restbl_floor':len(result)+65536,'payload_sha256':hashlib.sha256(dest.read_bytes()).hexdigest()}
    (output/'audio-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n');print(json.dumps({k:v for k,v in receipt.items() if k!='waves'}))
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--botw-romfs',type=Path,required=True);p.add_argument('--totk-romfs',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();build(a.botw_romfs,a.totk_romfs,a.output)
