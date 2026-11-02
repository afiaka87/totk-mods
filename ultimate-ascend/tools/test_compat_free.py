"""Compare real chained ARM callbacks against the unmodified community acceleration hook."""
import argparse
import json
import struct
import sys
from pathlib import Path
from elftools.elf.elffile import ELFFile
from test_patch_runtime import Machine, BASE, HEAP, GPRS, STACK, STOP
from unicorn import arm64_const as arm


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('root','layout','patch','out'): p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();sys.path.insert(0,str(a.root/'scripts'))
    from patch_gaps.formats import parse_pchtxt
    m=Machine(a.layout,a.root)
    from community_profile_data import SITES
    bindings=m.bindings
    community_port=m.report.get('community_port')
    shared=bindings['kPreUpdate']
    community_entry=community_port['labels'][str(0x2b19ca0)] if community_port else 0x2b19ca0
    continuation=shared+20
    player_getter=SITES[0x107e9a0][m.version]
    timer=SITES[0xec98cc][m.version]
    _,writes=parse_pchtxt(a.patch.read_text())
    for w in writes:
        if w.enabled:m.cpu.mem_write(BASE+w.off,w.data)
    m.run(m.report['runtime_symbols']['patch_initialize'])
    assert struct.unpack('<QII',m.cpu.mem_read(m.anchor,16))==(HEAP,2,0)
    offsets={};types={}
    with (a.layout/'native/descend-patch.o').open('rb') as stream:
        elf=ELFFile(stream);cursor=0;bases={}
        for i,s in enumerate(elf.iter_sections()):
            if not s['sh_size'] or not s['sh_flags']&2 or s['sh_flags']&4 or s.name in ('.data.patch_callback_roots','.data.patch_guard_root'):continue
            alignment=max(1,s['sh_addralign']);cursor=(cursor+alignment-1)&-alignment
            bases[i]=cursor;cursor+=s['sh_size']
        for s in elf.get_section_by_name('.symtab').iter_symbols():
            if s['st_shndx'] in bases: offsets[s.name]=bases[s['st_shndx']]+s['st_value']
        for cu in elf.get_dwarf_info().iter_CUs():
            for d in cu.iter_DIEs():
                n=d.attributes.get('DW_AT_name')
                if d.tag=='DW_TAG_structure_type' and n and n.value==b'Event':
                    types={c.attributes['DW_AT_name'].value.decode():c.attributes['DW_AT_data_member_location'].value for c in d.iter_children() if c.tag=='DW_TAG_member' and 'DW_AT_data_member_location' in c.attributes}
    event=HEAP+next(v for k,v in offsets.items() if k.endswith('g_eventE'))
    node,context,owner,actor,holder,player,power,clipper=[HEAP+x for x in (0x3000,0x4000,0x5000,0x6000,0x7000,0x8000,0xb000,0xc000)]
    def put(at,fmt,*v):m.cpu.mem_write(at,struct.pack('<'+fmt,*v))
    def setup(state,elapsed,flags,owned):
        m.reset();m.set_ready();m.fixture_writes=[(node,node+0x100),(player,player+0x2000)]
        for at in (node,context,owner,actor,holder,player,power,clipper):m.cpu.mem_write(at,bytes(0x600))
        for at,val in ((node+8,context),(context+8,owner),(owner+504,actor),(actor+bindings['actor::kComponents'],holder),(holder+936,player),(holder+1144,power),(power+56,clipper)):put(at,'Q',val)
        put(node+0x20,'I',state);put(node+0x40,'f',elapsed);put(node+0x50,'f',150);put(node+0x4c,'f',18)
        put(player+0xae4,'I',flags)
        put(event+types['active'],'B',owned);put(event+types['actor'],'Q',actor)
        put(event+types['touched'],'Q',19200000)
        m.fake_calls={BASE+player_getter:player,BASE+timer:0}
        m.cpu.reg_write(GPRS[0],node)
    results=[]
    shrine_results=[]
    for state in (0,1,2,3):
        for elapsed in (0,149,150,180,300,600,1200):
            for flags in (0,0x80000):
                snapshots=[]
                for entry in (community_entry,shared):
                    setup(state,elapsed,flags,False)
                    m.stop_at=BASE+continuation
                    m.run(entry)
                    snapshots.append((bytes(m.cpu.mem_read(node,0x100)),bytes(m.cpu.mem_read(player,0x1000)),m.cpu.reg_read(arm.UC_ARM64_REG_PC),m.cpu.reg_read(arm.UC_ARM64_REG_SP),tuple(m.cpu.reg_read(r) for r in GPRS[19:30])))
                assert snapshots[0]==snapshots[1],(state,elapsed,flags)
                results.append((state,elapsed,flags))
                if m.report.get('shrine_gates'):
                    vanilla = next(g['vanilla'] for g in m.report['shrine_gates'] if g['address'] == shared)
                    snapshots=[]
                    for entry in (vanilla,shared):
                        setup(state,elapsed,flags,False)
                        put(HEAP+m.report['shrine_flag_offset'],'B',1)
                        m.stop_at=BASE+continuation
                        m.run(entry)
                        snapshots.append((bytes(m.cpu.mem_read(node,0x100)),bytes(m.cpu.mem_read(player,0x1000)),m.cpu.reg_read(arm.UC_ARM64_REG_PC),m.cpu.reg_read(arm.UC_ARM64_REG_SP),tuple(m.cpu.reg_read(r) for r in GPRS[19:30])))
                    assert snapshots[0]==snapshots[1],('shrine',state,elapsed,flags)
                    shrine_results.append((state,elapsed,flags))
    for state in (0,1,2,3):
        setup(state,600,0,True)
        before=bytes(m.cpu.mem_read(node,0x100));sp=m.cpu.reg_read(arm.UC_ARM64_REG_SP)
        m.run(shared)
        assert bytes(m.cpu.mem_read(node,0x100))==before
        assert m.cpu.reg_read(GPRS[0])==0 and m.cpu.reg_read(arm.UC_ARM64_REG_SP)==sp
    result={'upward_differential_cases':len(results),'shrine_vanilla_cases':len(shrine_results),'downward_bypass_cases':4,'initializer':'pass','verdict':'Offline pass; gameplay readout pending'}
    a.out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))


if __name__=='__main__':main()
