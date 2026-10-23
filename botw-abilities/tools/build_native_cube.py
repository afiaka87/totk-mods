# SPDX-License-Identifier: MIT
# Copyright (c) Clay Mullis
"""Extend the accepted native sphere recipe to the cube without rewriting the sphere."""
import argparse
import copy
import hashlib
import json
from pathlib import Path

import byml
import oead

from build_native_sphere import pack, encode, extend_tags, TotkZstd
from build_release_restbl import read_forces
from native_sphere_budgets import cloned_resource_floor

CUBES=('Obj_RemoteBombCube','Obj_RemoteBombCubePlus')
ACTORS=tuple(name+suffix for name in CUBES for suffix in ('','_Attachment'))
PROJECT='TotkNativeCube'
MODEL='SpObj_TimerBomb_A_01'


def build(args):
    z=TotkZstd(args.romfs); out=args.build/'romfs'
    floors={k:int(v) for k,v in read_forces(args.build/'restbl-forces.tsv')}
    proof=[]
    cube_shapes={}
    for actor in CUBES:
        records=pack(z.decompress(out/f'Pack/Actor/{actor}.pack.zs'))
        cube_shapes[actor]=next(byml.Byml(raw).parse() for name,raw in records.items()
                               if name.endswith('_BarrelBomb_Body_0.phive__ShapeParam.bgyml'))
    def emit(name,raw,budget):
        path=out/name; path.parent.mkdir(parents=True,exist_ok=True)
        path.write_bytes(z.compress(raw,name) if name.endswith('.zs') else raw)
        floors[name.removesuffix('.zs')]=max(floors.get(name.removesuffix('.zs'),0),budget)
    for actor in ACTORS:
        donor=actor.replace('Cube','Sphere')
        source_raw=z.decompress(out/f'Pack/Actor/{donor}.pack.zs')
        source=pack(source_raw)
        shape=cube_shapes[actor.removesuffix('_Attachment')]
        names={name:name.replace(donor,actor) for name in source}
        def convert(value):
            if isinstance(value,str):return value.replace(donor,actor).replace('TotkNativeSphere',PROJECT)
            if isinstance(value,dict):return {k:convert(v) for k,v in value.items()}
            if isinstance(value,list):return [convert(v) for v in value]
            return value
        result={}
        for name,raw in source.items():
            data=convert(byml.Byml(raw).parse()) if name.endswith('.bgyml') else None
            if data is not None:
                if '/ShapeParam/' in name and '__Tgt.phive__ShapeParam' in name:
                    data=copy.deepcopy(shape)
                if '/ELink/' in name:
                    # Both shapes share the accepted native explosion user and bank.
                    data=byml.Byml(raw).parse()
                result[names[name]]=encode(data)
            else:result[names[name]]=raw
            if name.endswith('.bgyml'):
                if name not in floors:raise ValueError('Missing accepted sphere allocation: '+name)
                floor=cloned_resource_floor(floors[name],len(raw),len(result[names[name]]))
                floors[names[name]]=max(floors.get(names[name],0),floor)
                proof.append(dict(source=name,clone=names[name],source_budget=floors[name],source_bytes=len(raw),
                                  cloned_bytes=len(result[names[name]]),floor=floors[names[name]]))
        writer=oead.SarcWriter(oead.Endianness.Little)
        for name,raw in result.items():writer.files[name]=raw
        raw=bytes(writer.write()[1])
        emit(f'Pack/Actor/{actor}.pack.zs',raw,floors[f'Pack/Actor/{donor}.pack']+max(0,len(raw)-len(source_raw))+65536)
        if pack(z.decompress(out/f'Pack/Actor/{actor}.pack.zs'))!=result:raise ValueError('Cube pack round trip failed')

    mesh=json.loads(args.reference.read_text(encoding='utf-8-sig'))['models'][0]
    vertices=[v['pos'] for s in mesh['shapes'] for v in s['vertices']]
    bounds=[{axis:byml.Float(op(v[i] for v in vertices)) for i,axis in enumerate('XYZ')} for op in (min,max)]
    for family in ('ActorInfo','GameActorInfo'):
        name=f'RSDB/{family}.Product.121.rstbl.byml.zs'; previous=z.decompress(out/name)
        table=byml.Byml(previous).parse()
        for actor in ACTORS:
            donor=actor.replace('Cube','Sphere')
            row=copy.deepcopy(next(r for r in table if r['__RowId']==donor));row['__RowId']=actor
            if family=='ActorInfo':
                row.update(ActorName=actor,ModelProjectName=PROJECT,FmdbName=MODEL,ModelAabbMin=bounds[0],ModelAabbMax=bounds[1])
            old=next((i for i,r in enumerate(table) if r['__RowId']==actor),None)
            if old is None:table.append(row)
            else:table[old]=row
        table.sort(key=lambda r:r['__RowId']);raw=encode(table)
        emit(name,raw,floors[name.removesuffix('.zs')]+max(0,len(raw)-len(previous))*10+65536)
    name='RSDB/Tag.Product.121.rstbl.byml.zs';previous=z.decompress(out/name)
    tags=byml.Byml(previous).parse();known=set(tags['PathList'][1::3])
    tags=extend_tags(tags,{actor:actor.replace('Cube','Sphere') for actor in ACTORS if actor not in known})
    raw=encode(tags);emit(name,raw,floors[name.removesuffix('.zs')]+max(0,len(raw)-len(previous))*10+65536)
    model=args.model/f'{PROJECT}.{MODEL}.bfres.mc'
    model_budget=max(524288,(args.model/'round-trip.bfres').stat().st_size*2+65536)
    emit(f'Model/{model.name}',model.read_bytes(),model_budget)
    floors[f'Model/{PROJECT}.{MODEL}.bfres']=model_budget
    # Keep the native material animation names; the cube's rigid vertices bind to the identity root.
    animation=z.decompress(out/'Model/TotkNativeSphere.anim.bfres.zs')
    emit(f'Model/{PROJECT}.anim.bfres.zs',animation,floors['Model/TotkNativeSphere.anim.bfres'])
    for path in sorted((args.textures/'TexToGo').glob('TotkNativeCube_*.txtg')):
        emit('TexToGo/'+path.name,path.read_bytes(),len(path.read_bytes())+65536)
    (args.build/'restbl-forces.tsv').write_text(''.join(f'{p}\t{n}\n' for p,n in sorted(floors.items())))
    receipt=dict(actors=list(ACTORS),model_sha256=hashlib.sha256(model.read_bytes()).hexdigest(),
                 geometry_sha256=hashlib.sha256(args.reference.read_bytes()).hexdigest(),parameter_budgets=proof,
                 textures=json.loads((args.textures/'texture-receipt.json').read_text()),
                 behavior='accepted native sphere death-drop and effects; original cube collider and artwork')
    (args.build/'native-cube-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(f'Native cube: four actors, original box collision, shared native blast, {len(proof)} allocation proofs')


if __name__=='__main__':
    parser=argparse.ArgumentParser()
    for key in ('build','romfs','model','textures','reference'):parser.add_argument('--'+key,type=Path,required=True)
    build(parser.parse_args())
