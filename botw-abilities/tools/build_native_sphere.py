# SPDX-License-Identifier: MIT
# Copyright (c) Clay Mullis
"""Port the supplied sphere's native actors and effects into the combined mod."""
from __future__ import annotations

import argparse
import copy
import hashlib
import json
from pathlib import Path
import re
import sys
import zipfile

import byml
import oead

ROOT = Path(__file__).resolve().parents[4]
sys.path[:0] = [str(ROOT / 'scripts'), str(ROOT / 'mods/code/totk-remote-bomb/tools')]
from totk_mod_tools.zstd_helper import TotkZstd
from build_actors import resource_path, rewrite, f32
from build_inventory import tag_rows
from import_audio import transcode, user_blocks, XLINK_SHA256
from build_release_restbl import read_forces
from native_sphere_budgets import NativeBudgets

SOURCE_SHA = 'b5794c7c0ae2e3b43ef06239585773d316a95e1b091086a63fdbaca70ddd712a'
PROJECT = 'TotkNativeSphere'
MODEL = 'SpObj_TimerBomb_A_01'
BLAST = 'Obj_NativeSphereBlast'
EFFECT_USER = 'TotkNativeSphereExplosion'
EFFECT_BANK = 'TotkNativeSphere'
SPHERES = ('Obj_RemoteBombSphere', 'Obj_RemoteBombSpherePlus')
ACTORS = {SPHERES[0]: MODEL, SPHERES[1]: MODEL,
          SPHERES[0] + '_Attachment': MODEL + '_ForAttachment',
          SPHERES[1] + '_Attachment': MODEL + '_ForAttachment', BLAST: 'ExpandField_TimeBomb'}


def pack(data: bytes) -> dict[str, bytes]:
    return {entry.name: bytes(entry.data) for entry in oead.Sarc(data).get_files()}


class AlignedBinaryWriter(byml.Writer):
    def _write_nonvalue_node(self, stream, data, offsets):
        super()._write_nonvalue_node(stream, data, offsets)
        if isinstance(data, bytes):
            stream.write(b'\0' * (-stream.tell() % 4))


def encode(data) -> bytes:
    raw = AlignedBinaryWriter(data, version=7).get_bytes()
    if byml.Byml(raw).parse() != data:
        raise ValueError('BYML round trip failed')
    return raw


def scale_shape(value, scale, key=''):
    if isinstance(value, dict):
        if key in ('Center', 'Min', 'Max', 'OffsetTranslation', 'Vertices', 'HalfExtents', 'Tensor',
                   'TransOffset', 'AttachTransOffsetArrow'):
            factor = scale * scale if key == 'Tensor' else scale
            return {k: f32(float(v) * factor) if k in 'XYZ' and isinstance(v, (int, float))
                    else scale_shape(v, scale, k) for k, v in value.items()}
        return {k: f32(float(v) * (scale**3 if k == 'Volume' else scale))
                if k in ('Radius', 'Height', 'ConvexRadius', 'Volume')
                and isinstance(v, (int, float)) else scale_shape(v, scale, k)
                for k, v in value.items()}
    if isinstance(value, list):
        return [scale_shape(item, scale, key) for item in value]
    return value


def extend_tags(table, additions):
    rows = [(path, bits, i) for i, (path, bits) in enumerate(tag_rows(table))]
    count, tags = len(rows), len(table['TagList'])
    stride = (count + 31) // 32 * 32
    ranks = int.from_bytes(table['RankTable'], 'little')
    for actor, donor in additions.items():
        source = next((row for row in rows if row[0][1] == donor), (None, 0, None))
        rows.append((['Work/Actor/', actor, '.engine__actor__ActorParam.gyml'], source[1], source[2]))
    rows.sort(key=lambda row: row[0][1])
    new_stride = (len(rows) + 31) // 32 * 32
    rank = bytearray(tags * new_stride // 8)
    for tag in range(tags):
        column = (ranks >> (tag * stride)) & ((1 << stride) - 1)
        for i, (_, _, old) in enumerate(rows):
            if old is not None and column >> old & 1:
                bit = tag * new_stride + i
                rank[bit // 8] |= 1 << (bit % 8)
    table['PathList'] = [part for path, _, _ in rows for part in path]
    bits = sum(bits << (i * tags) for i, (_, bits, _) in enumerate(rows))
    table['BitTable'] = bits.to_bytes((len(rows) * tags + 7) // 8, 'little')
    table['RankTable'] = bytes(rank)
    return table


def build(args):
    args.scale = float(json.loads(args.scale_file.read_text())['scale'])
    if hashlib.sha256(args.archive.read_bytes()).hexdigest() != SOURCE_SHA:
        raise ValueError('Supplied sphere ZIP differs from the inspected input')
    z = TotkZstd(args.romfs)
    out = args.build / 'romfs'
    work = args.build / 'native-sphere'
    work.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.archive) as archive:
        source = {n: archive.read(n) for n in archive.namelist() if not n.endswith('/')}
    source_packs = {}
    for actor in set(ACTORS.values()):
        name = f'Pack/Actor/{actor}.pack.zs'
        private = work / name
        private.parent.mkdir(parents=True, exist_ok=True)
        private.write_bytes(source[name])
        source_packs[actor] = pack(z.decompress(private))
    forces = dict(read_forces(args.build/'restbl-forces.tsv'))
    native_budgets = NativeBudgets(args.romfs,z,forces)
    parameter_budgets = []

    def emit(name, raw, multiplier=1):
        dest = out / name
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(z.compress(raw, name) if name.endswith('.zs') else raw)
        key = name.removesuffix('.zs')
        forces[key] = str(max(int(forces.get(key, 0)), len(raw) * multiplier + 65536))

    def table(name, data):
        previous = z.decompress(out/name)
        raw = encode(data)
        key = name.removesuffix('.zs')
        floor = int(forces.get(key, 0))
        dest = out/name
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(z.compress(raw, name))
        forces[key] = str(max(floor + max(0, len(raw)-len(previous))*10 + 65536,
                              len(raw)+65536))

    prepared = {}
    for actor, donor in ACTORS.items():
        records = dict(source_packs[donor])
        root_name = f'Actor/{donor}.engine__actor__ActorParam.bgyml'
        root = byml.Byml(records[root_name]).parse()
        if actor in SPHERES:
            current = pack(z.decompress(out / f'Pack/Actor/{actor}.pack.zs'))
            current_root = byml.Byml(current[f'Actor/{actor}.engine__actor__ActorParam.bgyml']).parse()
            def include(link):
                path = resource_path(link)
                if path not in current or path in records:
                    return
                records[path] = current[path]
                def visit(value):
                    if isinstance(value, str): include(value)
                    elif isinstance(value, dict):
                        for v in value.values(): visit(v)
                    elif isinstance(value, list):
                        for v in value: visit(v)
                if records[path][:2] in (b'YB', b'BY'):
                    visit(byml.Byml(records[path]).parse())
            for component in ('ActorNameRef', 'PouchContentRef', 'EventPerformerRef', 'PlacementMotionRef',
                              'ModelBindRef', 'ActorGameDataSetting', 'LODRef'):
                link = current_root['Components'][component]
                include(link)
                root['Components'][component] = link
            current_params = byml.Byml(current[resource_path(current_root['Components']['GameParameterTableRef'])]).parse()
            param_path = resource_path(root['Components']['GameParameterTableRef'])
            params = byml.Byml(records[param_path]).parse()
            if 'ThrowBoxParam' in current_params['Components']:
                link = current_params['Components']['ThrowBoxParam']
                include(link)
                params['Components']['ThrowBoxParam'] = link
                records[param_path] = encode(params)
            root['Category'] = current_root['Category']
            root['Components'].pop('EventMemberRef', None)
            root['Components'].pop('NavMeshObjectRef', None)
        records[root_name] = encode(root)
        if actor != BLAST:
            ai_path = resource_path(root['Components']['AIInfoRef'])
            ai = byml.Byml(records[ai_path]).parse()
            if ai['PostCalcAIRef'] != 'Work/AI/Root/SpObj_TimerBomb.root.ain':
                raise ValueError('Unexpected native bomb timer graph: ' + actor)
            # Keep native control/contact behavior. The scoped ExecuteSpObjTimerBomb
            # hook bypasses only the timer update for our eight remote-bomb actors.
        names = {}
        for name in records:
            if name.endswith('.bgyml'):
                parent, filename = name.rsplit('/', 1)
                names[name] = parent + '/' + actor + '_' + filename
            elif name == 'AI/Add_Effect.root.ainb':
                names[name] = 'AI/BtAb_Effct.root.ainb'
            else:
                names[name] = name
        names[root_name] = f'Actor/{actor}.engine__actor__ActorParam.bgyml'
        links = {'?' + a: '?' + b for a, b in names.items()}
        links.update({'Work/' + a.removesuffix('.bgyml') + '.gyml':
                      'Work/' + b.removesuffix('.bgyml') + '.gyml'
                      for a, b in names.items() if a.endswith('.bgyml')})
        links.update({'Work/AI/Root/Add_Effect.root.ain': 'Work/AI/Root/BtAb_Effct.root.ain',
                      'ExpandField_TimeBomb': BLAST,
                      'Work/Actor/ExpandField_TimeBomb.engine__actor__ActorParam.gyml':
                      f'Work/Actor/{BLAST}.engine__actor__ActorParam.gyml',
                      'SpObj_TimerBomb_A_01_ForAttachment': actor.removesuffix('_Attachment') + '_Attachment',
                      'Work/Actor/SpObj_TimerBomb_A_01_ForAttachment.engine__actor__ActorParam.gyml':
                      'Work/Actor/' + actor.removesuffix('_Attachment') + '_Attachment.engine__actor__ActorParam.gyml'})
        result = {}
        for name, raw in records.items():
            if name.endswith('.bgyml'):
                data = rewrite(byml.Byml(raw).parse(), links)
                if actor != BLAST and '/DropTableElementList/' in name:
                    drops = data.get('DropTableElement', [])
                    if drops and all(row.get('DropActorName') ==
                                     f'Work/Actor/{BLAST}.engine__actor__ActorParam.gyml'
                                     for row in drops):
                        # A blast is not recoverable loot. Native multishot suppresses
                        # resurrection drops from the additional arrow attachments.
                        data['IsResurrectionActor'] = False
                if '/ModelInfo/' in name and actor != BLAST:
                    data['ModelProjectName'] = PROJECT
                    data['FmdbName'] = MODEL
                    data['DebugModelScale'] = dict(X=f32(1), Y=f32(1), Z=f32(1))
                if '/ShapeParam/' in name and actor != BLAST:
                    data = scale_shape(data, args.scale)
                if '/AttachmentParam/' in name and actor != BLAST:
                    data = scale_shape(data, args.scale)
                if '/ELink/' in name and data.get('UserName') == MODEL:
                    data['UserName'] = EFFECT_USER
                raw = encode(data)
            if name == 'AI/Add_Effect.root.ainb':
                raw = raw.replace(b'Add_Effect.root\0', b'BtAb_Effct.root\0')
            result[names[name]] = raw
        writer = oead.SarcWriter(oead.Endianness.Little)
        for name, raw in result.items(): writer.files[name] = raw
        data = bytes(writer.write()[1])
        emit(f'Pack/Actor/{actor}.pack.zs', data)
        key=f'Pack/Actor/{actor}.pack'
        forces[key]=str(max(int(forces[key]),native_budgets.actor_pack(donor,len(data))))
        if pack(z.decompress(out/f'Pack/Actor/{actor}.pack.zs')) != result:
            raise ValueError('Native sphere pack round trip: ' + actor)
        for original, name in names.items():
            proof=native_budgets.parameter(original,name,len(records[original]),len(result[name]))
            forces[name]=str(max(int(forces.get(name,0)),proof['floor']))
            proof['floor']=int(forces[name])
            parameter_budgets.append(proof)
        prepared[actor] = {'donor': donor, 'bytes': len(data), 'members': len(result)}

    for family in ('ActorInfo', 'GameActorInfo'):
        name = f'RSDB/{family}.Product.121.rstbl.byml.zs'
        data = byml.Byml(z.decompress(out/name)).parse()
        stock = byml.Byml(z.decompress(args.romfs/name)).parse()
        for actor, donor in ACTORS.items():
            template = donor if any(row['__RowId'] == donor for row in stock) else (
                'ExpandField_GoronSage' if donor.startswith('ExpandField') else MODEL)
            native = copy.deepcopy(next(row for row in stock if row['__RowId'] == template))
            native['__RowId'] = actor
            old = next((row for row in data if row['__RowId'] == actor), None)
            if family == 'ActorInfo':
                native['ActorName'] = actor
                if actor != BLAST:
                    native['ModelProjectName'], native['FmdbName'] = PROJECT, MODEL
                    native['ModelAabbMin'] = dict(X=f32(-.541*args.scale), Y=f32(0), Z=f32(-.541*args.scale))
                    native['ModelAabbMax'] = dict(X=f32(.541*args.scale), Y=f32(1.348*args.scale), Z=f32(.541*args.scale))
                if old:
                    native['Category'] = old['Category']
                    native['InstanceHeapSize'] = type(native['InstanceHeapSize'])(max(int(old['InstanceHeapSize']),int(native['InstanceHeapSize'])))
            if old: data[data.index(old)] = native
            else: data.append(native)
        data.sort(key=lambda row: row['__RowId'])
        table(name, data)
    name = 'RSDB/Tag.Product.121.rstbl.byml.zs'
    tags = byml.Byml(z.decompress(out/name)).parse()
    known = set(tags['PathList'][1::3])
    additions = {actor: donor if donor in known else ('ExpandField_GoronSage' if donor.startswith('ExpandField') else MODEL)
                 for actor, donor in ACTORS.items() if actor not in SPHERES}
    table(name, extend_tags(tags, additions))

    for name, raw in source.items():
        if name.startswith('TexToGo/') and name.endswith('.txtg'):
            emit(name, raw)
    emit(f'Model/{PROJECT}.{MODEL}.bfres.mc', args.model.read_bytes())
    forces[f'Model/{PROJECT}.{MODEL}.bfres'] = str(max(args.expanded.stat().st_size*2+65536, 524288))
    emit(f'Model/{PROJECT}.anim.bfres.zs', args.animation.read_bytes())
    effect = work/'effect.esetb.byml.zs'
    effect.write_bytes(source['Effect/SpObj_TimerBomb_A_01.Nin_NX_NVN.esetb.byml.zs'])
    effect_name = f'Effect/{EFFECT_BANK}.Nin_NX_NVN.esetb.byml.zs'
    emit(effect_name, z.decompress(effect), 4)

    if hashlib.sha256(args.xlink.read_bytes()).hexdigest() != XLINK_SHA256:
        raise ValueError('Unreviewed XLink converter')
    resource = 'ELink2/elink2.Product.110.belnk.zs'
    binary, text, converted = work/'before.belnk', work/'before.txt', work/'after.belnk'
    binary.write_bytes(z.decompress(out/resource))
    names = work/'users.txt'; names.write_text(MODEL+'\n'+EFFECT_USER+'\n')
    transcode(args.xlink, binary, text, 'EXKing', names)
    original = text.read_text(encoding='utf-8')
    users = user_blocks(original)
    custom = users[MODEL].replace('  '+MODEL+' {', '  '+EFFECT_USER+' {', 1)
    candidate = work/'after.txt'
    candidate.write_text(original.rstrip()[:-1]+custom+'\n}\n',encoding='utf-8')
    transcode(args.xlink, candidate, converted, 'EXKing', names)
    check = work/'roundtrip.txt'
    transcode(args.xlink, converted, check, 'EXKing', names)
    restored = user_blocks(check.read_text(encoding='utf-8'))
    if any(restored.get(k) != v for k,v in users.items()) or restored[EFFECT_USER] != custom:
        raise ValueError('Existing native effect users changed')
    emit(resource, converted.read_bytes(), 2)
    name = 'Effect/EffectFileInfo.Product.110.Nin_NX_NVN.byml.zs'
    banks = byml.Byml(z.decompress(out/name)).parse()
    banks['BinaryDict'][EFFECT_USER] = EFFECT_BANK
    banks['EsetbList'] = sorted([*banks['EsetbList'], EFFECT_BANK])
    table(name,banks)
    (args.build/'restbl-forces.tsv').write_text(''.join(f'{p}\t{n}\n' for p,n in sorted(forces.items())))
    receipt={'source_sha256':SOURCE_SHA,'scale':args.scale,'actors':prepared,'model':PROJECT,
             'parameter_budgets':parameter_budgets,
             'model_sha256':hashlib.sha256(args.model.read_bytes()).hexdigest(),
             'animation_sha256':hashlib.sha256(args.animation.read_bytes()).hexdigest(),
             'effect_user':EFFECT_USER,'effect_bank':EFFECT_BANK,'mode':'native sphere mannerisms; existing inventory and L detonation'}
    (args.build/'native-sphere-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print('Native sphere actors/effect bank built; original native users preserved')


if __name__ == '__main__':
    parser=argparse.ArgumentParser()
    for name in ('archive','romfs','build','model','expanded','animation','xlink'):
        parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--scale-file',type=Path,required=True)
    build(parser.parse_args())
