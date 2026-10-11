"""Retain self-dumped mono DSP data while adapting FWAV to a private TotK bank."""
from __future__ import annotations
import argparse, hashlib, json, struct, sys, zlib
from pathlib import Path
ROOT=Path(__file__).resolve().parents[4]
sys.path.insert(0,str(ROOT/'scripts'))
sys.path.insert(0,str(ROOT/'framework/bridgefs/tools'))
from totk_mod_tools.zstd_helper import TotkZstd
from bridgefs_tools.audio_bank import Addition, append_assets, read_bank, verify_preserved

NAMES=('Bitalock_Start_02','Bitalock_Hit_06','Bitalock_Hitend_06','Bitalock_End_06','Bitalock_End_07','Bitalock_End_08')
TIMER='BitaLock_Timer03'
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
def build(source:Path,expression:Path,clean:Path,output:Path)->None:
    original=source.read_bytes(); count=u32(original,12)
    if original[:4]!=b'BARS' or original[8:10]!=b'\xff\xfe' or u32(original,4)!=len(original): raise ValueError('Expected complete original BARS')
    found={}
    for i in range(count):
        meta,audio=struct.unpack_from('<II',original,16+count*4+i*8)
        name_offset=meta+u32(original,meta+24)+8
        name=original[name_offset:].split(b'\0',1)[0].decode('ascii')
        if name in NAMES:
            size=u32(original,audio+12)
            found[name]=convert(original[audio:audio+size])
    if set(found)!=set(NAMES): raise ValueError('Missing original Stasis waveforms')
    timer_bank=expression.read_bytes();timer_count=u32(timer_bank,12)
    for i in range(timer_count):
        meta,audio=struct.unpack_from('<II',timer_bank,16+timer_count*4+i*8)
        name_offset=meta+u32(timer_bank,meta+24)+8
        name=timer_bank[name_offset:].split(b'\0',1)[0].decode('ascii')
        if name==TIMER: found[name]=convert(timer_bank[audio:audio+u32(timer_bank,audio+12)])
    if TIMER not in found: raise ValueError('Missing original countdown waveform')
    codec=TotkZstd(clean)
    path='Sound/Resource/UI_Static.bars.zs'
    bank=read_bank(codec.decompress(clean/path))
    additions=tuple(Addition('TotkStasis_'+name,found[name][0],1.0) for name in (*NAMES,TIMER))
    result=append_assets(bank,additions,'Sys_HeartRecovery_00')
    after=read_bank(result); verify_preserved(bank,after)
    destination=output/'romfs'/path; destination.parent.mkdir(parents=True,exist_ok=True)
    destination.write_bytes(codec.compress(result,Path(path).name))
    receipt={'source_sha256':hashlib.sha256(original).hexdigest(),'expression_bank_sha256':hashlib.sha256(timer_bank).hexdigest(),'source_version':'BotW 1.5.0','stock_entries_preserved':len(bank.entries),'new_entries':len(after.entries),'bank_raw_bytes':len(result),'restbl_floor':len(result)+65536,'waves':{n:found[n][1] for n in (*NAMES,TIMER)},'payload_sha256':hashlib.sha256(destination.read_bytes()).hexdigest()}
    output.mkdir(parents=True,exist_ok=True)
    (output/'audio-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(json.dumps({k:v for k,v in receipt.items() if k!='waves'}))
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);p.add_argument('--expression-bank',type=Path,required=True);p.add_argument('--clean-romfs',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();build(a.source,a.expression_bank,a.clean_romfs,a.output)
